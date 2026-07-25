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
// The document declares a SACM revision other than 2.3. Distinct from
// SACM-XMI-002: the namespace is recognized, it is simply an older revision.
// Conflating the two would make "we do not know this namespace" and "this is
// SACM 2.2" indistinguishable to a caller.
inline constexpr std::string_view kXmiOlderStandardVersion = "SACM-XMI-008";
// The document's root is not a SACM interchange package, but SACM packages were
// found inside it -- the shape real toolchains use when they embed SACM in a
// larger container (an ODE DDIPackage, say). A tolerant load reads the SACM out
// of it and says so: the file is NOT a conformant SACM 2.3 interchange
// document, everything outside the SACM packages is not represented, and saving
// produces a conformant SACM document rather than the original container.
// Strict rejects such a document outright (SACM-XMI-001).
inline constexpr std::string_view kXmiForeignContainerRoot = "SACM-XMI-009";

// Identity and references.
inline constexpr std::string_view kIdDuplicate = "SACM-ID-001";
inline constexpr std::string_view kIdInvalid = "SACM-ID-002";
inline constexpr std::string_view kRefDangling = "SACM-REF-001";
inline constexpr std::string_view kRefWrongType = "SACM-REF-002";
// The reference resolves to an element the document carries only as preserved
// compatibility content, so it cannot be type-checked. Distinct from
// SACM-REF-001: the target is present in the source, it is merely untyped, and
// conflating the two reports an intact argument as structurally broken.
inline constexpr std::string_view kRefPreservedTarget = "SACM-REF-003";

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
