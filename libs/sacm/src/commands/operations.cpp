#include "sacm/commands/operations.h"

namespace sacm::commands {

std::string_view operation_name(const Operation& operation) {
    return std::visit(
        [](const auto& op) -> std::string_view {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, CreateAssuranceCasePackage>) {
                return "CreateAssuranceCasePackage";
            } else if constexpr (std::is_same_v<T, CreateArgumentPackage>) {
                return "CreateArgumentPackage";
            } else if constexpr (std::is_same_v<T, CreateClaim>) {
                return "CreateClaim";
            } else if constexpr (std::is_same_v<T, CreateTerminologyPackage>) {
                return "CreateTerminologyPackage";
            } else if constexpr (std::is_same_v<T, CreateCategory>) {
                return "CreateCategory";
            } else if constexpr (std::is_same_v<T, CreateTerm>) {
                return "CreateTerm";
            } else if constexpr (std::is_same_v<T, CreateExpression>) {
                return "CreateExpression";
            } else if constexpr (std::is_same_v<T, SetCitation>) {
                return "SetCitation";
            } else if constexpr (std::is_same_v<T, SetName>) {
                return "SetName";
            } else if constexpr (std::is_same_v<T, SetDescription>) {
                return "SetDescription";
            } else if constexpr (std::is_same_v<T, AddTaggedValue>) {
                return "AddTaggedValue";
            } else {
                return "DeleteElement";
            }
        },
        operation);
}

}  // namespace sacm::commands
