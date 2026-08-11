#include "core/reviews/review_proposal_plan.h"

#include "core/element_factory.h"
#include "sacm_adapter/gsn_role_tag.h"

#include <algorithm>
#include <map>
#include <set>

namespace core::reviews {

namespace {

sacm_adapter::TextField ToAdapterTextField(ElementTextField field) {
    switch (field) {
    case ElementTextField::Name:
        return sacm_adapter::TextField::Name;
    case ElementTextField::Description:
        return sacm_adapter::TextField::Description;
    case ElementTextField::Content:
        break;
    }
    return sacm_adapter::TextField::Content;
}

// The parser's relationship token as the seam's SACM kind. The seam takes SACM's
// vocabulary deliberately, so the GSN direction swap stays with the caller that
// owns the GSN reading -- here, the patch service, whose output we mirror.
bool RelationshipKindFor(const std::string& type, sacm_adapter::RelationshipKind& out) {
    if (type == "assertedinference") {
        out = sacm_adapter::RelationshipKind::AssertedInference;
        return true;
    }
    if (type == "assertedcontext") {
        out = sacm_adapter::RelationshipKind::AssertedContext;
        return true;
    }
    if (type == "assertedevidence") {
        out = sacm_adapter::RelationshipKind::AssertedEvidence;
        return true;
    }
    return false;
}

bool IsRelationship(const SacmElement& element) {
    return element.type == "assertedinference" || element.type == "assertedcontext" ||
           element.type == "assertedevidence";
}

const SacmElement* Find(const AssuranceCase& model, const std::string& id) {
    for (const SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

ProposalPlan::Relationship AsRelationship(const SacmElement& element) {
    return ProposalPlan::Relationship{
        .id = element.id,
        .type = element.type,
        .sources = element.source_refs,
        .targets = element.target_refs,
        .reasoning = element.reasoning_ref,
        .is_counter = element.is_counter,
    };
}

// The seam kind a created POD element maps to. The patch service writes the
// element type and the assertion declaration, and those two together carry the
// GSN kind -- an "assumed" claim is an Assumption, a "justification" claim is a
// Justification. Solution and Context are indistinguishable here and do not need
// to be: both are an ArtifactReference, and what separates them is the
// relationship that attaches them.
bool SeamKindFor(const SacmElement& element, sacm_adapter::NewElementKind& out) {
    if (element.type == "claim") {
        if (element.assertion_declaration.empty() || element.assertion_declaration == "asserted") {
            out = sacm_adapter::NewElementKind::Claim;
        } else if (element.assertion_declaration == "assumed") {
            out = sacm_adapter::NewElementKind::Assumption;
        } else if (element.assertion_declaration == "justification" || element.assertion_declaration == "axiomatic") {
            out = sacm_adapter::NewElementKind::Justification;
        } else {
            return false;
        }
        return true;
    }
    if (element.type == "argumentreasoning") {
        out = sacm_adapter::NewElementKind::ArgumentReasoning;
        return true;
    }
    if (element.type == "artifactreference") {
        out = sacm_adapter::NewElementKind::ArtifactReference;
        return true;
    }
    return false;
}

// Whether a created element's POD `content` has anywhere to go. This mirrors
// `content_maps_to_description` on the adapter side, and must keep mirroring it:
// the patch service routes a created element's text to `content` for exactly the
// kinds that map to a Description and to `description` for the rest, so a
// narrower rule here would decline proposals the seam can express perfectly well.
bool KindCanHoldText(sacm_adapter::NewElementKind kind) {
    return kind == sacm_adapter::NewElementKind::Claim || kind == sacm_adapter::NewElementKind::Assumption ||
           kind == sacm_adapter::NewElementKind::Justification ||
           kind == sacm_adapter::NewElementKind::ArgumentReasoning;
}

void CollectTextWrites(const SacmElement& before, const SacmElement& after, std::vector<ProposalPlan::TextWrite>& out) {
    const auto collect = [&](ElementTextField field,
                             const std::string& before_scalar,
                             const std::string& after_scalar,
                             const std::map<std::string, std::string>& before_langs,
                             const std::map<std::string, std::string>& after_langs) {
        // The scalar is the primary language; the map carries every language
        // including it. Writing per language is what the seam accepts, and the
        // primary is written under "en" the way the legacy mutator records it.
        if (before_scalar != after_scalar) {
            out.push_back(ProposalPlan::TextWrite{
                .element_id = after.id, .field = field, .language = "en", .value = after_scalar});
        }
        for (const auto& [language, value] : after_langs) {
            if (language == "en")
                continue; // already covered by the scalar
            const auto found = before_langs.find(language);
            if (found == before_langs.end() || found->second != value) {
                out.push_back(ProposalPlan::TextWrite{
                    .element_id = after.id, .field = field, .language = language, .value = value});
            }
        }
    };
    collect(ElementTextField::Name, before.name, after.name, before.name_langs, after.name_langs);
    collect(ElementTextField::Content, before.content, after.content, before.content_langs, after.content_langs);
    collect(ElementTextField::Description,
            before.description,
            after.description,
            before.description_langs,
            after.description_langs);
}

// Fields no seam in this plan can write. Reported rather than silently skipped:
// a proposal that changed one of these would otherwise appear to apply and then
// not have.
std::string UnsupportedFieldChange(const SacmElement& before, const SacmElement& after) {
    if (before.type != after.type)
        return "element " + after.id + " changed type";
    if (before.gid != after.gid)
        return "element " + after.id + " changed gid";
    if (before.gsn_identifier != after.gsn_identifier)
        return "element " + after.id + " changed its GSN identifier";
    if (before.is_abstract != after.is_abstract)
        return "element " + after.id + " changed isAbstract";
    if (before.assertion_declaration != after.assertion_declaration)
        return "element " + after.id + " changed its assertion declaration";
    if (before.structure_ref != after.structure_ref)
        return "element " + after.id + " changed its structure reference";
    if (before.meta_claim_refs != after.meta_claim_refs)
        return "element " + after.id + " changed its meta claims";
    return {};
}

} // namespace

ProposalPlan
PlanProposalFromDiff(const AssuranceCase& before, const AssuranceCase& after, const std::string& package_for_created) {
    ProposalPlan plan;

    const auto decline = [&plan](std::string reason) {
        if (plan.unrepresentable_reason.empty())
            plan.unrepresentable_reason = std::move(reason);
    };

    for (const SacmElement& updated : after.elements) {
        const SacmElement* original = Find(before, updated.id);

        if (original == nullptr) {
            if (IsRelationship(updated)) {
                sacm_adapter::RelationshipKind kind = sacm_adapter::RelationshipKind::AssertedInference;
                if (!RelationshipKindFor(updated.type, kind)) {
                    decline("no seam creates a " + updated.type + " (" + updated.id + ")");
                    continue;
                }
                // A counter relationship is a dialectic challenge, which
                // `apply_add_relationship` does not create -- `apply_challenge`
                // does, together with its counter element. The proposal vocabulary
                // has no challenge operation, so this is a guard rather than a
                // path: reaching it means the patch service grew one.
                if (updated.is_counter) {
                    decline("relationship " + updated.id + " is a counter relationship");
                    continue;
                }
                // A strategy attached before it has any sub-goal produces an
                // AssertedInference whose only end is `reasoning`, which violates
                // source [1..*] (clause 11.13) and `apply_add_relationship`
                // refuses. Declining HERE rather than discovering it at write time
                // is the whole point of planning first: by then the elements are
                // already in the document and the refusal is a hard failure with
                // no way back, instead of a clean fall-through to the bridge.
                if (updated.source_refs.empty() || updated.target_refs.empty()) {
                    decline("relationship " + updated.id + " has no source or no target");
                    continue;
                }
                plan.created_relationships.push_back(AsRelationship(updated));
                continue;
            }
            sacm_adapter::NewElementKind kind = sacm_adapter::NewElementKind::Claim;
            if (!SeamKindFor(updated, kind)) {
                decline("no seam creates a " + updated.type + " (" + updated.id + ")");
                continue;
            }
            if (!updated.content.empty() && !KindCanHoldText(kind)) {
                decline("element " + updated.id + " is a " + updated.type + ", which has no description to hold text");
                continue;
            }
            if (package_for_created.empty()) {
                decline("no ArgumentPackage resolved for created element " + updated.id);
                continue;
            }
            plan.created.push_back(ProposalPlan::CreatedElement{
                .id = updated.id,
                .package_id = package_for_created,
                .kind = kind,
                .name = updated.name,
                .text = updated.content,
                .language = "en",
            });
            if (updated.undeveloped)
                plan.flag_writes.push_back(ProposalPlan::FlagWrite{.element_id = updated.id, .undeveloped = true});
            // A created element's translations are written after it exists.
            SacmElement empty;
            empty.id = updated.id;
            SacmElement created_so_far = empty;
            created_so_far.name = updated.name;
            created_so_far.content = updated.content;
            CollectTextWrites(created_so_far, updated, plan.text_writes);
            continue;
        }

        if (IsRelationship(updated)) {
            // A relationship whose ends moved is a retarget, which this plan does
            // not express -- the proposal vocabulary has no move operation
            // (#261), so an endpoint change here means something unexpected.
            if (original->source_refs != updated.source_refs || original->target_refs != updated.target_refs ||
                original->reasoning_ref != updated.reasoning_ref) {
                decline("relationship " + updated.id + " changed its endpoints");
            }
            continue;
        }

        const std::string unsupported = UnsupportedFieldChange(*original, updated);
        if (!unsupported.empty()) {
            decline(unsupported);
            continue;
        }
        CollectTextWrites(*original, updated, plan.text_writes);
        if (original->undeveloped != updated.undeveloped) {
            plan.flag_writes.push_back(
                ProposalPlan::FlagWrite{.element_id = updated.id, .undeveloped = updated.undeveloped});
        }
    }

    for (const SacmElement& original : before.elements) {
        if (Find(after, original.id) == nullptr)
            plan.deleted_ids.push_back(original.id);
    }

    return plan;
}

bool ApplyProposalPlanToLibrary(sacm_adapter::LibraryDocument& document,
                                const ProposalPlan& plan,
                                std::string& out_error) {
    // Elements first: a relationship naming one of them cannot be created until
    // it exists, and the library resolves endpoints on write.
    for (const ProposalPlan::CreatedElement& created : plan.created) {
        const sacm_adapter::AddChildOutcome outcome = sacm_adapter::apply_create_element(
            document,
            created.package_id,
            created.kind,
            sacm_adapter::CreateElementFields{
                .element_id = created.id, .name = created.name, .text = created.text, .language = created.language});
        if (!outcome.supported || !outcome.applied) {
            out_error = "creating " + created.id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    for (const ProposalPlan::Relationship& relationship : plan.created_relationships) {
        sacm_adapter::RelationshipKind kind = sacm_adapter::RelationshipKind::AssertedInference;
        if (!RelationshipKindFor(relationship.type, kind)) {
            out_error = "no seam creates a " + relationship.type + " (" + relationship.id + ")";
            return false;
        }
        const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_add_relationship(
            document, relationship.id, kind, relationship.sources, relationship.targets, relationship.reasoning);
        if (!outcome.supported || !outcome.applied) {
            out_error = "creating relationship " + relationship.id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    for (const ProposalPlan::TextWrite& write : plan.text_writes) {
        const sacm_adapter::EditOutcome outcome = sacm_adapter::apply_text_edit(
            document, write.element_id, ToAdapterTextField(write.field), write.language, write.value);
        // A DECLINE is fatal here, not a reason to skip: the planner already
        // rejected every shape the seam cannot express, so a decline at this point
        // means the plan and the seam disagree, and skipping would drop a change
        // the reviewer approved.
        if (!outcome.supported || !outcome.applied) {
            out_error = "writing text on " + write.element_id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    for (const ProposalPlan::FlagWrite& write : plan.flag_writes) {
        const sacm_adapter::EditOutcome outcome =
            sacm_adapter::apply_set_undeveloped(document, write.element_id, write.undeveloped);
        if (!outcome.supported || !outcome.applied) {
            out_error = "setting undeveloped on " + write.element_id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    // Deletions last: a relationship the proposal replaced must exist until its
    // replacement is in, or the document passes through a state with a claim
    // supported by nothing.
    for (const std::string& id : plan.deleted_ids) {
        const sacm_adapter::DeleteOutcome outcome = sacm_adapter::apply_delete_element(document, id);
        if (!outcome.supported || !outcome.applied) {
            out_error = "deleting " + id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    return true;
}

} // namespace core::reviews
