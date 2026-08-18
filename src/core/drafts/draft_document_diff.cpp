#include "core/drafts/draft_document_diff.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace core::drafts {

namespace {

// Every field of the POD that carries argument meaning, each named the way the
// rest of the application names it.
//
// The list is explicit rather than a memberwise compare because a new field on
// `SacmElement` must be a deliberate decision here: silently comparing it would
// start reporting changes the UI has no way to explain, and silently omitting it
// would hide a real edit. A field added without touching this list shows up as
// "the draft changed nothing" for an element the user just edited, which is the
// failure this comment exists to prevent.
void CollectChangedFields(const core::SacmElement& accepted,
                          const core::SacmElement& draft,
                          std::vector<std::string>& out) {
    const auto note = [&out](const char* field) { out.emplace_back(field); };

    if (accepted.name != draft.name || accepted.name_langs != draft.name_langs)
        note("name");
    if (accepted.content != draft.content || accepted.content_langs != draft.content_langs)
        note("content");
    if (accepted.description != draft.description || accepted.description_langs != draft.description_langs)
        note("description");
    if (accepted.gsn_identifier != draft.gsn_identifier)
        note("gsn_identifier");
    if (accepted.undeveloped != draft.undeveloped)
        note("undeveloped");
    if (accepted.is_abstract != draft.is_abstract)
        note("is_abstract");
    if (accepted.assertion_declaration != draft.assertion_declaration)
        note("assertion_declaration");
    if (accepted.is_counter != draft.is_counter)
        note("is_counter");
    if (accepted.source_refs != draft.source_refs)
        note("source_refs");
    if (accepted.target_refs != draft.target_refs)
        note("target_refs");
    if (accepted.meta_claim_refs != draft.meta_claim_refs)
        note("meta_claim_refs");
    if (accepted.reasoning_ref != draft.reasoning_ref)
        note("reasoning_ref");
    if (accepted.structure_ref != draft.structure_ref)
        note("structure_ref");
    if (accepted.category_refs != draft.category_refs)
        note("category_refs");
    if (accepted.external_reference != draft.external_reference)
        note("external_reference");
    if (accepted.origin_ref != draft.origin_ref)
        note("origin_ref");
    // `type` and `gid` are identity, not content. A change to either is a
    // different element wearing a familiar id, which this comparison cannot
    // express and the draft cannot produce: neither is editable through any
    // operation a contributor has.
}

std::unordered_map<std::string, const core::SacmElement*> IndexById(const core::AssuranceCase& model) {
    std::unordered_map<std::string, const core::SacmElement*> index;
    index.reserve(model.elements.size());
    for (const core::SacmElement& element : model.elements) {
        if (element.id.empty())
            continue;
        // First wins. A duplicate id is a malformed document rather than
        // something to reconcile here, and picking the first keeps this
        // deterministic instead of order-dependent on the container.
        index.emplace(element.id, &element);
    }
    return index;
}

} // namespace

std::vector<std::string> ChangedElementFields(const core::SacmElement& accepted, const core::SacmElement& draft) {
    std::vector<std::string> fields;
    CollectChangedFields(accepted, draft, fields);
    std::sort(fields.begin(), fields.end());
    return fields;
}

DraftChangeIndex ChangeIndexFromDiff(const DraftDocumentDiff& diff) {
    DraftChangeIndex index;
    for (const DraftDocumentChange& change : diff.changes) {
        if (change.change == DraftElementChange::Unchanged)
            continue;
        DraftElementEntry entry;
        entry.change = change.change;
        index.elements.emplace(change.element_id, std::move(entry));
    }
    index.removed = diff.removed;
    index.added_count = diff.added_count;
    index.modified_count = diff.modified_count;
    index.removed_count = diff.removed_count;
    return index;
}

const DraftDocumentChange* DraftDocumentDiff::Find(const std::string& element_id) const {
    for (const DraftDocumentChange& change : changes) {
        if (change.element_id == element_id)
            return &change;
    }
    return nullptr;
}

DraftDocumentDiff DiffAcceptedAgainstDraft(const core::AssuranceCase& accepted, const core::AssuranceCase& draft) {
    DraftDocumentDiff diff;

    const std::unordered_map<std::string, const core::SacmElement*> accepted_by_id = IndexById(accepted);
    const std::unordered_map<std::string, const core::SacmElement*> draft_by_id = IndexById(draft);

    // Collected into a map first so the result is ordered by id rather than by
    // whichever document happened to list an element first. A reviewer reading
    // the same draft twice must see the same list in the same order.
    std::map<std::string, DraftDocumentChange> by_id;

    for (const core::SacmElement& element : draft.elements) {
        if (element.id.empty())
            continue;
        const auto found = accepted_by_id.find(element.id);
        if (found == accepted_by_id.end()) {
            DraftDocumentChange change;
            change.element_id = element.id;
            change.change = DraftElementChange::Added;
            by_id.emplace(element.id, std::move(change));
            continue;
        }
        std::vector<std::string> fields = ChangedElementFields(*found->second, element);
        if (fields.empty())
            continue;
        DraftDocumentChange change;
        change.element_id = element.id;
        change.change = DraftElementChange::Modified;
        change.fields = std::move(fields);
        by_id.emplace(element.id, std::move(change));
    }

    for (const core::SacmElement& element : accepted.elements) {
        if (element.id.empty() || draft_by_id.count(element.id) != 0)
            continue;
        DraftDocumentChange change;
        change.element_id = element.id;
        change.change = DraftElementChange::Removed;
        // Appended only when this id was actually recorded, so the two halves of
        // a removal cannot disagree. `removed_count` is derived from the map
        // below, which takes the first occurrence of a duplicate id the way
        // `IndexById` does; pushing unconditionally would report one removal and
        // hand the renderer two elements to draw.
        if (by_id.emplace(element.id, std::move(change)).second)
            diff.removed.push_back(element);
    }

    diff.changes.reserve(by_id.size());
    for (auto& [id, change] : by_id) {
        switch (change.change) {
        case DraftElementChange::Added:
            ++diff.added_count;
            break;
        case DraftElementChange::Modified:
            ++diff.modified_count;
            break;
        case DraftElementChange::Removed:
            ++diff.removed_count;
            break;
        case DraftElementChange::Unchanged:
            break;
        }
        diff.changes.push_back(std::move(change));
    }

    // `removed` is filled in accepted-document order above; sort it to match
    // `changes` so two views of the same removal list agree.
    std::sort(diff.removed.begin(),
              diff.removed.end(),
              [](const core::SacmElement& left, const core::SacmElement& right) { return left.id < right.id; });

    return diff;
}

} // namespace core::drafts
