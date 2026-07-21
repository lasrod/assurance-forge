#include "sacm_adapter/document_edit.h"

#include "sacm_adapter/library_document_access.h"

#include "sacm/commands/mutation.h"
#include "sacm/commands/operations.h"
#include "sacm/metadata/element_kind.h"
#include "sacm/model/document.h"
#include "sacm/model/element.h"
#include "sacm/model/element_id.h"
#include "sacm/model/lang_string.h"

#include <string>

namespace sacm_adapter {

namespace {

// The app's primary editor language. `core::SetElementTextField` writes the
// canonical `name`/`content` scalar only for this language, so it is the code
// the app treats as an element's primary text (mirrored by the projection,
// which maps a lang-less library entry to "en").
constexpr const char* kPrimaryLanguage = "en";

// Flatten a library MutationResult into the adapter's string-only EditOutcome.
EditOutcome applied_outcome(const sacm::commands::MutationResult& result) {
    EditOutcome outcome;
    outcome.supported = true;
    outcome.applied = result.applied;
    outcome.diagnostics.reserve(result.diagnostics.size());
    for (const sacm::validation::Diagnostic& diagnostic : result.diagnostics) {
        outcome.diagnostics.push_back(LoadDiagnostic{
            .code = diagnostic.code,
            .severity = std::string(sacm::validation::severity_name(diagnostic.severity)),
            .message = diagnostic.message,
        });
    }
    return outcome;
}

EditOutcome unsupported_outcome() {
    return EditOutcome{.supported = false, .applied = false, .diagnostics = {}};
}

// The app's `content` is a claim-like element's primary Description (clause
// 8.9). Other kinds carry their text elsewhere (Term/Expression `value`), where
// SetDescription would be wrong, so Content is only mapped for these kinds.
bool content_maps_to_description(sacm::metadata::ElementKind kind) {
    return kind == sacm::metadata::ElementKind::Claim ||
           kind == sacm::metadata::ElementKind::ArgumentReasoning;
}

// The library language whose entry a primary-language edit must overwrite.
// A legacy `content=` statement is stored lang-less (set("", ...)); editing it
// under "en" must overwrite that entry in place, not append a parallel "en"
// one that `primary()` would never return. So for a primary edit we target the
// front Description's existing language; other languages target themselves.
std::string effective_description_language(const sacm::model::ModelElement& element,
                                           const std::string& app_language) {
    if (app_language != kPrimaryLanguage) {
        return app_language;
    }
    if (!element.descriptions().empty()) {
        const std::vector<sacm::model::LangString>& values =
            element.descriptions().front()->content().values;
        if (!values.empty()) {
            return values.front().lang;
        }
    }
    // No statement yet: match the reader's lang-less convention for a created
    // Description so a later load round-trips identically.
    return "";
}

} // namespace

EditOutcome apply_text_edit(LibraryDocument& document, const std::string& element_id,
                            TextField field, const std::string& language,
                            const std::string& value) {
    sacm::model::Document& doc = LibraryDocumentAccess::mutable_document(document);
    const sacm::model::ElementId id(element_id);

    switch (field) {
    case TextField::Name: {
        // SetName replaces the element's single name LangString (clause 8.6).
        // Under a non-primary language that would drop the primary name, so
        // multi-language name edits wait for the slice that handles the
        // reserved "sacm.import.name" TaggedValue.
        if (language != kPrimaryLanguage) {
            return unsupported_outcome();
        }
        const sacm::commands::Operation operation = sacm::commands::SetName{
            .element = id,
            .name = value,
            .language = language,
        };
        return applied_outcome(doc.apply(operation));
    }
    case TextField::Content: {
        const auto* element = doc.find_as<sacm::model::ModelElement>(id);
        if (element == nullptr || !content_maps_to_description(element->kind())) {
            return unsupported_outcome();
        }
        const sacm::commands::Operation operation = sacm::commands::SetDescription{
            .element = id,
            .text = value,
            .language = effective_description_language(*element, language),
        };
        return applied_outcome(doc.apply(operation));
    }
    case TextField::Description:
        // The secondary-note Description (claim) and non-claim Descriptions are
        // not wired yet; see the header. Report unsupported so the caller keeps
        // the legacy edit authoritative.
        return unsupported_outcome();
    }
    return unsupported_outcome();
}

} // namespace sacm_adapter
