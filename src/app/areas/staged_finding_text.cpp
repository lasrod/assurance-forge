#include "app/areas/staged_finding_text.h"

#include "ui/i18n/localization.h"

namespace app::areas {

std::string StagedFindingText(const core::sccg::StagedFinding& finding) {
    // Keyed by check id, not guideline id: AR.1 backs three different findings
    // (a solution with children, a strategy developing into nothing, and a
    // support cycle) that need three different sentences. Where two of them
    // share a check id, `params[0]` chooses between them.
    if (finding.check_id == "check-evidence-trace") {
        return AF_TR("This claim has no support and is not marked undeveloped, so a reviewer cannot "
                     "tell whether evidence is missing or still to come. Give it support, or mark "
                     "it undeveloped to say so deliberately.");
    }
    if (finding.check_id == "check-explicit-strategy") {
        return AF_TR("This claim is broken into sub-claims with no reasoning step saying how they "
                     "were chosen or why together they support it. Add a strategy stating the "
                     "decomposition rule.");
    }
    // Two findings share this check id, because both really are the catalog's
    // role-misuse question. `params[0]` names which role, so the sentences stay
    // separately translatable rather than one being a parameterized shell of the
    // other -- they say different things, not the same thing about a different
    // element.
    if (finding.check_id == "check-element-role-misuse" && !finding.params.empty() && finding.params[0] == "strategy") {
        return AF_TR("This strategy develops into nothing. A decomposition step that produces no "
                     "sub-claims states an inference the argument never makes.");
    }
    if (finding.check_id == "check-element-role-misuse") {
        return AF_TR("This is a solution -- the artefact or observation the argument rests on -- so "
                     "it should be a leaf. Elements hanging beneath it are not carrying the role "
                     "the structure says they are.");
    }
    if (finding.check_id == "check-circular-support") {
        return AF_TR("These operations put a claim in its own support chain, so the argument supports "
                     "itself and establishes nothing.");
    }
    if (finding.check_id == "check-bounded-qualifiers" && !finding.params.empty()) {
        return ui::i18n::trf("This claim uses \"{0}\", which SCCG names as a term needing bounds. Say what "
                             "it means here -- against which hazards, in which operating conditions, to "
                             "what standard -- in the claim or in attached context.",
                             finding.params[0]);
    }
    if (finding.check_id == "check-single-property" && finding.params.size() == 2) {
        return ui::i18n::trf("This claim joins \"{0} and {1}\" -- two distinct properties needing different "
                             "evidence and review. Give each its own goal, so one can fail without hiding "
                             "the other.",
                             finding.params[0],
                             finding.params[1]);
    }
    if (finding.check_id == "check-claim-step-mixing" && finding.params.size() == 2) {
        return ui::i18n::trf("This claim chains \"{0} and {1}\" -- different logical steps answering "
                             "different review questions. Give each step its own claim, and let the "
                             "structure show the decomposition.",
                             finding.params[0],
                             finding.params[1]);
    }
    if (finding.check_id == "check-element-signposting" && !finding.params.empty()) {
        return ui::i18n::trf("This claim carries its own reasoning (\"{0}\"), so a reviewer cannot tell the "
                             "claim from the argument for it. State the claim alone; the reasoning belongs "
                             "in a strategy and the evidence in a solution.",
                             finding.params[0]);
    }
    if (finding.check_id == "check-promotional-language" && !finding.params.empty()) {
        return ui::i18n::trf("This text calls the work \"{0}\". Promotional language persuades nobody "
                             "reviewing a safety argument; state what was shown, and under which "
                             "assumptions.",
                             finding.params[0]);
    }
    if (finding.check_id == "check-evidence-control-attributes") {
        return AF_TR("This evidence reference carries no owner, version, date, or status, so a reviewer "
                     "cannot tell which artifact was assessed or whether it has changed since. Cite the "
                     "controlled version.");
    }
    if (finding.check_id == "check-evidence-citation-precision") {
        return AF_TR("This evidence names an artifact but no part of it, so a reviewer cannot find "
                     "the material that supports the claim. Cite the section, table, figure or "
                     "scenarios the argument rests on.");
    }
    if (finding.check_id == "check-evidence-state-fixed" && !finding.params.empty()) {
        return ui::i18n::trf("This evidence cites \"{0}\", which reads as live mutable content. Cite a fixed "
                             "version, revision, or archived snapshot, so the reviewed argument always "
                             "refers to the same content.",
                             finding.params[0]);
    }
    if (finding.check_id == "check-completeness-vs-absence") {
        return AF_TR("This text treats the absence of discovered evidence as support. Not finding something "
                     "does not establish the claim; argue from what the applied methods can show.");
    }
    // Unknown check: English is better than nothing, and the fallback showing
    // up in a review is the signal that a template is missing here.
    return finding.detail;
}

} // namespace app::areas
