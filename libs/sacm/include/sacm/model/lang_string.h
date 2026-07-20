#pragma once

#include "sacm/model/element_id.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sacm::model {

// SACM LangString (clause 8.3): a string with a language tag. Value type.
//
// ExpressionLangString (clause 8.4) is represented as a LangString whose
// `expression_ref` names an ExpressionElement in a TerminologyPackage; the
// XMI layer maps that field to the ExpressionLangString subtype. Per the
// clause 8.4 constraint, content should be empty when expression_ref is set.
struct LangString {
    std::string lang;
    std::string content;
    std::optional<ElementId> expression_ref;

    friend bool operator==(const LangString&, const LangString&) = default;
};

// SACM MultiLangString (clause 8.5): one meaning in one or more languages.
// value is [1..*] with unique language tags (validated, not enforced here).
struct MultiLangString {
    std::vector<LangString> values;

    bool empty() const { return values.empty(); }

    // Content for an exact language tag, if present.
    const std::string* find(std::string_view lang) const;

    // Content of the first entry (the primary language), or empty.
    const std::string& primary() const;

    // Sets/overwrites the entry for `lang`.
    void set(std::string_view lang, std::string_view content);

    friend bool operator==(const MultiLangString&, const MultiLangString&) = default;
};

}  // namespace sacm::model
