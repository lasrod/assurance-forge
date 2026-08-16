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

// Collects the per-language writes that turn `before`'s text into `after`'s.
//
// Returns a decline reason rather than emitting a write the seam would reject.
// SACM's name is ONE LangString (clause 8.6), so `apply_text_edit` declines a
// non-primary-language name; planning one anyway would pass the representability
// check and then hard-fail mid-apply, with elements already written and no way
// back -- the exact failure planning first exists to prevent.
std::string
CollectTextWrites(const SacmElement& before, const SacmElement& after, std::vector<ProposalPlan::TextWrite>& out) {
    std::string decline_reason;
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
                if (field == ElementTextField::Name) {
                    if (decline_reason.empty()) {
                        decline_reason = "element " + after.id + " changes its name in '" + language +
                                         "', and a SACM name is one LangString";
                    }
                    continue;
                }
                // A term's `content` is its ExpressionElement value -- one
                // string, no languages (clause 10.11). The seam writes it via
                // SetExpressionValue, which has no language to take, so a
                // translated value must decline HERE rather than hard-fail
                // mid-apply. The patch service already refuses to stage one;
                // this guards the shapes it cannot see, e.g. a hand-written
                // proposal file.
                if (field == ElementTextField::Content && after.type == "term") {
                    if (decline_reason.empty()) {
                        decline_reason = "term " + after.id + " changes its value in '" + language +
                                         "', and a term's value is one string; translate its definition instead";
                    }
                    continue;
                }
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
    return decline_reason;
}

// Fields no seam in this plan can write. Reported rather than silently skipped:
// a proposal that changed one of these would otherwise appear to apply and then
// not have.
// The terminology fields that changed between the two states, as a write, or
// nothing when they are identical. Only a term has them; a change to them on any
// other kind is a shape no operation produces and is declined by the caller
// rather than written somewhere it does not belong.
std::optional<ProposalPlan::TermFieldWrite> CollectTermFieldWrite(const SacmElement& before, const SacmElement& after) {
    ProposalPlan::TermFieldWrite write;
    write.term_id = after.id;
    write.write_categories = before.category_refs != after.category_refs;
    write.write_external_reference = before.external_reference != after.external_reference;
    write.write_origin = before.origin_ref != after.origin_ref;
    if (!write.write_categories && !write.write_external_reference && !write.write_origin)
        return std::nullopt;
    write.category_refs = after.category_refs;
    write.external_reference = after.external_reference;
    write.origin_ref = after.origin_ref;
    return write;
}

bool TerminologyFieldsDiffer(const SacmElement& before, const SacmElement& after) {
    return before.category_refs != after.category_refs || before.external_reference != after.external_reference ||
           before.origin_ref != after.origin_ref;
}

std::string UnsupportedFieldChange(const SacmElement& before, const SacmElement& after) {
    if (before.type != after.type)
        return "element " + after.id + " changed type";
    // Terminology fields belong to a term and are written by their own seams;
    // on anything else there is nowhere to put them.
    if (after.type != "term" && TerminologyFieldsDiffer(before, after))
        return "element " + after.id + " is a " + after.type + " and cannot carry terminology fields";
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

ProposalPlan PlanProposalFromDiff(const AssuranceCase& before,
                                  const AssuranceCase& after,
                                  const std::string& package_for_created,
                                  const std::string& terminology_package_for_created) {
    ProposalPlan plan;

    const auto decline = [&plan](std::string reason) {
        if (!reason.empty() && plan.unrepresentable_reason.empty())
            plan.unrepresentable_reason = std::move(reason);
    };

    // Strategy id -> the element it will support, for the bare-strategy shape
    // below. Collected while walking, applied to the created elements afterwards,
    // because the relationship can appear before the strategy it names.
    std::map<std::string, std::string> deferred_strategy_targets;

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
                // A strategy attached before it has any sub-goal produces an
                // AssertedInference whose only end is `reasoning`, which clause
                // 11.13 forbids. It is DEFERRED rather than declined: the strategy
                // records the goal it will support in a vendor tag, and the
                // inference materializes when the first sub-goal gives it a source
                // -- exactly what `apply_add_child` does for a new Strategy.
                // A strategy attached before it has any sub-goal produces an
                // AssertedInference whose only end is `reasoning`, which clause
                // 11.13 forbids. It is DEFERRED rather than declined: the strategy
                // records the goal it will support in a vendor tag, and the
                // inference materializes when the first sub-goal gives it a source
                // -- exactly what `apply_add_child` does for a new Strategy.
                if (updated.source_refs.empty() && !updated.reasoning_ref.empty() && updated.target_refs.size() == 1) {
                    deferred_strategy_targets[updated.reasoning_ref] = updated.target_refs.front();
                    continue;
                }
                if (updated.source_refs.empty() || updated.target_refs.empty()) {
                    decline("relationship " + updated.id + " has no source or no target");
                    continue;
                }
                plan.created_relationships.push_back(AsRelationship(updated));
                continue;
            }
            if (updated.type == "category") {
                plan.created_categories.push_back(ProposalPlan::CreatedCategory{
                    .id = updated.id,
                    .package_id = terminology_package_for_created,
                    .name = updated.name,
                    .description = updated.description,
                });
                continue;
            }
            if (updated.type == "term") {
                plan.created_terms.push_back(ProposalPlan::CreatedTerm{
                    .id = updated.id,
                    .package_id = terminology_package_for_created,
                    .value = updated.content,
                    .name = updated.name,
                    .definition = updated.description,
                    .category_refs = updated.category_refs,
                    .external_reference = updated.external_reference,
                    .origin_ref = updated.origin_ref,
                });
                // Definition translations are written after the term exists,
                // the way a created claim's are. Seeding the primaries here
                // keeps CollectTextWrites to the non-primary languages only.
                SacmElement term_so_far;
                term_so_far.id = updated.id;
                term_so_far.type = updated.type;
                term_so_far.name = updated.name;
                term_so_far.content = updated.content;
                term_so_far.description = updated.description;
                decline(CollectTextWrites(term_so_far, updated, plan.text_writes));
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
            decline(CollectTextWrites(created_so_far, updated, plan.text_writes));
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
        decline(CollectTextWrites(*original, updated, plan.text_writes));
        if (std::optional<ProposalPlan::TermFieldWrite> term_write = CollectTermFieldWrite(*original, updated);
            term_write.has_value()) {
            plan.term_field_writes.push_back(std::move(term_write.value()));
        }
        if (original->undeveloped != updated.undeveloped) {
            plan.flag_writes.push_back(
                ProposalPlan::FlagWrite{.element_id = updated.id, .undeveloped = updated.undeveloped});
        }
    }

    for (const SacmElement& original : before.elements) {
        if (Find(after, original.id) == nullptr)
            plan.deleted_ids.push_back(original.id);
    }

    for (ProposalPlan::CreatedElement& created : plan.created) {
        const auto deferred = deferred_strategy_targets.find(created.id);
        if (deferred == deferred_strategy_targets.end())
            continue;
        if (created.kind != sacm_adapter::NewElementKind::ArgumentReasoning) {
            decline("element " + created.id + " is the reasoning of an inference with no source");
            continue;
        }
        created.strategy_target = deferred->second;
        deferred_strategy_targets.erase(deferred);
    }
    // A deferral naming a strategy this proposal did not create would tag an
    // element the plan never touched, so it is declined rather than guessed at.
    for (const auto& [strategy_id, target] : deferred_strategy_targets) {
        decline("sourceless inference names existing reasoning " + strategy_id);
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
            sacm_adapter::CreateElementFields{.element_id = created.id,
                                              .name = created.name,
                                              .text = created.text,
                                              .language = created.language,
                                              .strategy_target = created.strategy_target});
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

    // Terminology before text writes, so a definition translation on a created
    // term has a term to land on. When the case has no TerminologyPackage yet,
    // the first term or category brings one into being -- the alternative is a
    // vocabulary that can define terms but only into a container that cannot
    // exist. Resolved through one helper so a group adding both a category and
    // a term does not create two packages.
    std::string created_terminology_package_id;
    const auto terminology_package_for =
        [&](const std::string& declared, const std::string& for_element, std::string& resolved) -> bool {
        if (!declared.empty()) {
            resolved = declared;
            return true;
        }
        if (created_terminology_package_id.empty()) {
            const sacm_adapter::TerminologyCreateOutcome outcome =
                sacm_adapter::apply_create_terminology_package(document, "Terminology", "");
            if (!outcome.supported || !outcome.applied) {
                out_error = "creating a terminology package for " + for_element + " failed" +
                            (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
                return false;
            }
            created_terminology_package_id = outcome.element_id;
        }
        resolved = created_terminology_package_id;
        return true;
    };

    // Categories first: a term naming one is refused until it resolves.
    for (const ProposalPlan::CreatedCategory& category : plan.created_categories) {
        std::string package_id;
        if (!terminology_package_for(category.package_id, category.id, package_id))
            return false;
        const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_category(
            document, package_id, category.name, category.description, category.id);
        if (!outcome.supported || !outcome.applied) {
            out_error = "creating category " + category.id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    for (const ProposalPlan::CreatedTerm& term : plan.created_terms) {
        std::string package_id;
        if (!terminology_package_for(term.package_id, term.id, package_id))
            return false;
        const sacm_adapter::TerminologyCreateOutcome outcome = sacm_adapter::apply_create_terminology_term(
            document,
            package_id,
            sacm_adapter::TerminologyTermFields{.value = term.value,
                                                .name = term.name,
                                                .description = term.definition,
                                                .category_refs = term.category_refs,
                                                .external_reference = term.external_reference,
                                                .origin = term.origin_ref},
            term.id);
        if (!outcome.supported || !outcome.applied) {
            out_error = "creating term " + term.id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    // An existing term's classification and provenance, one field at a time so
    // nothing else about the term -- least of all a translated definition -- is
    // disturbed on the way past.
    for (const ProposalPlan::TermFieldWrite& write : plan.term_field_writes) {
        if (write.write_categories) {
            const sacm_adapter::EditOutcome outcome =
                sacm_adapter::apply_set_term_categories(document, write.term_id, write.category_refs);
            if (!outcome.supported || !outcome.applied) {
                out_error = "setting categories on " + write.term_id + " failed" +
                            (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
                return false;
            }
        }
        if (write.write_external_reference) {
            const sacm_adapter::EditOutcome outcome =
                sacm_adapter::apply_set_term_external_reference(document, write.term_id, write.external_reference);
            if (!outcome.supported || !outcome.applied) {
                out_error = "setting the external reference on " + write.term_id + " failed" +
                            (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
                return false;
            }
        }
        if (write.write_origin) {
            const sacm_adapter::EditOutcome outcome =
                sacm_adapter::apply_set_term_origin(document, write.term_id, write.origin_ref);
            if (!outcome.supported || !outcome.applied) {
                out_error = "setting the origin on " + write.term_id + " failed" +
                            (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
                return false;
            }
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
            // `deleted_ids` is a before/after diff -- a set of absences, not a
            // sequence of independent commands. An earlier delete's reference
            // scrub can take a listed relationship with it (deleting a strategy
            // drops an inference the scrub leaves with no valid endpoints), and
            // the absence this entry asks for already holds. Failing here made
            // a draft that removed a strategy together with its dangling
            // relationship impossible to accept in any order.
            if (outcome.target_missing)
                continue;
            out_error = "deleting " + id + " failed" +
                        (outcome.diagnostics.empty() ? "" : ": " + outcome.diagnostics.front().message);
            return false;
        }
    }

    return true;
}

} // namespace core::reviews
