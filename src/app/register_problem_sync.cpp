#include "app/register_problem_sync.h"

#include "core/problems/problem_utils.h"
#include "ui/i18n/localization.h"

#include <string>
#include <utility>
#include <vector>

namespace app {
namespace {

constexpr const char* kRegisterProblemPrefix = "register-assessment:";
constexpr const char* kCseKindTag = "cse";
constexpr const char* kEvidenceKindTag = "evidence";

void AppendField(std::string& summary, const std::string& label, const std::string& value) {
    if (value.empty())
        return;
    if (!summary.empty())
        summary += "; ";
    summary += label + ": " + value;
}

// What the reviewer would lose, written out in the problem itself. An orphaned
// assessment is not in any table any more, so if the message does not carry the
// content there is no way left to read it before deciding.
std::string SummarizeCse(const core::registers::CseMetadata& metadata) {
    std::string summary;
    AppendField(summary, AF_TR("Claim Owner"), metadata.claim_owner);
    AppendField(summary, AF_TR("Evidence Owner"), metadata.evidence_owner);
    AppendField(summary, AF_TR("Safety Case Owner"), metadata.safety_case_owner);
    AppendField(summary, AF_TR("Claim Criteria"), metadata.claim_criteria);
    AppendField(summary, AF_TR("Evidence Criteria"), metadata.evidence_criteria);
    AppendField(summary, AF_TR("Assessment Status"), metadata.assessment_status);
    AppendField(summary, AF_TR("Notes"), metadata.notes);
    return summary;
}

std::string SummarizeEvidence(const core::registers::EvidenceMetadata& metadata) {
    std::string summary;
    AppendField(summary, AF_TR("Evidence Owner"), metadata.evidence_owner);
    AppendField(summary, AF_TR("Type"), metadata.type);
    AppendField(summary, AF_TR("Recency"), metadata.recency);
    AppendField(summary, AF_TR("Maturity"), metadata.maturity);
    AppendField(summary, AF_TR("Controlled Environment"), metadata.controlled_environment);
    AppendField(summary, AF_TR("Notes"), metadata.notes);
    return summary;
}

core::ProblemItem MakeOrphanProblem(const RegisterAssessmentRef& ref, std::string message) {
    core::ProblemItem problem;
    problem.id = kRegisterProblemPrefix + EncodeRegisterAssessmentPayload(ref);
    // The argument itself is sound; what needs a decision is a record about a
    // part of it that no longer exists.
    problem.severity = core::ProblemSeverity::Warning;
    problem.source = core::ProblemSource::ModelValidation;
    // No element to navigate to: the subject of the assessment is precisely what
    // is missing. The keys live in the message instead.
    problem.type = kRegisterAssessmentOrphanedProblemType;
    problem.message = std::move(message);
    problem.quick_fix_label = "Discard assessment";
    problem.quick_fix_payload = EncodeRegisterAssessmentPayload(ref);
    return problem;
}

} // namespace

std::string EncodeRegisterAssessmentPayload(const RegisterAssessmentRef& ref) {
    const char* kind_tag = ref.kind == RegisterAssessmentKind::Cse ? kCseKindTag : kEvidenceKindTag;
    return std::string(kind_tag) + ":" + ref.key;
}

bool DecodeRegisterAssessmentPayload(const std::string& payload, RegisterAssessmentRef& ref) {
    // Split on the first ':' only — a CSE key is itself "CSE:<claim>-><evidence>".
    const size_t separator = payload.find(':');
    if (separator == std::string::npos)
        return false;
    const std::string kind_tag = payload.substr(0, separator);
    const std::string key = payload.substr(separator + 1);
    if (key.empty())
        return false;
    if (kind_tag == kCseKindTag)
        ref.kind = RegisterAssessmentKind::Cse;
    else if (kind_tag == kEvidenceKindTag)
        ref.kind = RegisterAssessmentKind::Evidence;
    else
        return false;
    ref.key = key;
    return true;
}

void SyncRegisterProblems(core::ProblemsManager& problems_manager,
                          const parser::AssuranceCase* model,
                          const core::registers::RegisterStore* store) {
    core::ClearProblemsByIdPrefix(problems_manager, kRegisterProblemPrefix);
    if (!model || !store)
        return;

    const core::registers::OrphanedMetadata orphans = core::registers::FindOrphanedMetadata(
        *store, core::registers::DeriveCseLinks(*model), core::registers::DeriveEvidenceIds(*model));

    for (const std::string& cse_id : orphans.cse_ids) {
        const auto found = store->cse.find(cse_id);
        if (found == store->cse.end())
            continue;
        const RegisterAssessmentRef ref{RegisterAssessmentKind::Cse, cse_id};
        problems_manager.AddOrUpdateProblem(MakeOrphanProblem(
            ref,
            ui::i18n::trf("The assessment of {0} is kept, but the argument no longer links that claim to that "
                          "evidence. Stored: {1}",
                          cse_id,
                          SummarizeCse(found->second))));
    }

    for (const std::string& evidence_id : orphans.evidence_ids) {
        const auto found = store->evidence.find(evidence_id);
        if (found == store->evidence.end())
            continue;
        const RegisterAssessmentRef ref{RegisterAssessmentKind::Evidence, evidence_id};
        problems_manager.AddOrUpdateProblem(MakeOrphanProblem(
            ref,
            ui::i18n::trf("The assessment of evidence {0} is kept, but that evidence is no longer in the argument. "
                          "Stored: {1}",
                          evidence_id,
                          SummarizeEvidence(found->second))));
    }
}

} // namespace app
