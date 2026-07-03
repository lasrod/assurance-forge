#pragma once

#include <string_view>

// Stable diagnostic codes. Once released a code's meaning must not change.
// Catalogue with severities and descriptions: docs/sacm/sacm-diagnostics-catalog.md.
namespace sacm::validation::codes {

// XML and security.
inline constexpr std::string_view kXmlMalformed = "SACM-XML-001";
inline constexpr std::string_view kXmlDoctypeRejected = "SACM-SEC-001";

// XMI structure.
inline constexpr std::string_view kXmiInvalidRoot = "SACM-XMI-001";
inline constexpr std::string_view kXmiUnknownNamespace = "SACM-XMI-002";
inline constexpr std::string_view kXmiUnknownElement = "SACM-XMI-003";
inline constexpr std::string_view kXmiMissingId = "SACM-XMI-004";
inline constexpr std::string_view kXmiMissingType = "SACM-XMI-005";
inline constexpr std::string_view kXmiStrictSaveRefused = "SACM-XMI-006";
inline constexpr std::string_view kXmiExternalReference = "SACM-XMI-007";

// Identity and references.
inline constexpr std::string_view kIdDuplicate = "SACM-ID-001";
inline constexpr std::string_view kIdInvalid = "SACM-ID-002";
inline constexpr std::string_view kRefDangling = "SACM-REF-001";
inline constexpr std::string_view kRefWrongType = "SACM-REF-002";

// Validation.
inline constexpr std::string_view kEnumInvalidLiteral = "SACM-ENUM-001";
inline constexpr std::string_view kMultiplicityViolation = "SACM-MULT-001";
inline constexpr std::string_view kCitationInvalid = "SACM-CITE-001";

// Commands.
inline constexpr std::string_view kCmdTargetNotFound = "SACM-CMD-001";
inline constexpr std::string_view kCmdDeleteReferenced = "SACM-CMD-002";
inline constexpr std::string_view kCmdPreviewExpired = "SACM-CMD-003";
inline constexpr std::string_view kCmdDuplicateId = "SACM-CMD-004";
inline constexpr std::string_view kCmdInvalidParent = "SACM-CMD-005";
inline constexpr std::string_view kCmdPackageNotEmpty = "SACM-CMD-006";
inline constexpr std::string_view kCmdExternalReferences = "SACM-CMD-007";

}  // namespace sacm::validation::codes
