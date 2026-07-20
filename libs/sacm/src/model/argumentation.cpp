#include "sacm/model/argumentation.h"

namespace sacm::model {

std::string_view assertion_declaration_name(AssertionDeclaration declaration) {
    switch (declaration) {
        case AssertionDeclaration::Asserted:
            return "asserted";
        case AssertionDeclaration::NeedsSupport:
            return "needsSupport";
        case AssertionDeclaration::Assumed:
            return "assumed";
        case AssertionDeclaration::Axiomatic:
            return "axiomatic";
        case AssertionDeclaration::Defeated:
            return "defeated";
        case AssertionDeclaration::AsCited:
            return "asCited";
    }
    return "asserted";
}

std::optional<AssertionDeclaration> parse_assertion_declaration(std::string_view literal) {
    if (literal == "asserted") return AssertionDeclaration::Asserted;
    if (literal == "needsSupport") return AssertionDeclaration::NeedsSupport;
    if (literal == "assumed") return AssertionDeclaration::Assumed;
    if (literal == "axiomatic") return AssertionDeclaration::Axiomatic;
    if (literal == "defeated") return AssertionDeclaration::Defeated;
    if (literal == "asCited") return AssertionDeclaration::AsCited;
    return std::nullopt;
}

}  // namespace sacm::model
