#pragma once

#include <string_view>

// Pinned XMI serialization conventions for SACM 2.3. Single source of truth;
// rationale and derivation: docs/sacm/sacm-2.3-metamodel-inventory.md
// ("XMI serialization conventions"). Correcting a pin is a change here plus
// a golden-fixture refresh.
namespace sacm::metadata::namespaces {

// Strict export namespace. SACM 2.3 does not determine an instance-document
// namespace URI -- the normative MOF model declares no nsURI on any package,
// so XMI's schema-production rules have nothing to derive one from. This value
// is therefore a project choice, not a normative constant; see
// docs/sacm/sacm-2.3-metamodel-inventory.md. Export can override it via
// io::SaveOptions::namespace_uri.
inline constexpr std::string_view kSacm = "http://www.omg.org/spec/SACM/20220301";
inline constexpr std::string_view kSacmPrefix = "sacm";

// Namespace family used by the EMF reference implementation
// (github.com/wrwei/SACM), which declares one namespace per metamodel package:
// http://omg.sacm/2.2/base, /assurancecase, /argumentation, /artifact,
// /terminology. Accepted on import as a first-class dialect.
inline constexpr std::string_view kEmfReferencePrefix = "http://omg.sacm/";

inline constexpr std::string_view kXmi = "http://www.omg.org/spec/XMI/20131001";
// Real files use several XMI namespace URIs -- EMF-based tooling emits the
// older "http://www.omg.org/XMI". They are all the XMI infrastructure
// namespace, so xmi:id and xmi:version must be recognized under any of them.
bool is_xmi_namespace(std::string_view uri);
inline constexpr std::string_view kXmiPrefix = "xmi";
inline constexpr std::string_view kXmiVersion = "2.0";

inline constexpr std::string_view kXsi = "http://www.w3.org/2001/XMLSchema-instance";
inline constexpr std::string_view kXsiPrefix = "xsi";

// The SACM standard revision a document declares. This library implements 2.3;
// older revisions load in tolerant mode and are reported rather than silently
// accepted, because "it parsed" is not the same as "it is a 2.3 document".
//
// The enum exists mainly as a seam. SACM 2.4 is in active revision at the OMG
// and will change the Argumentation package (see
// docs/sacm/sacm-gsn-metamodel-gaps.md), so the version dimension is made
// explicit at the API boundary now, while adding a value is still cheap.
enum class StandardVersion {
    Unknown,
    V2_0,
    V2_1,
    V2_2,
    V2_3,
};

std::string_view standard_version_name(StandardVersion version);

// Best-effort revision detection from a namespace URI. Returns Unknown when the
// URI carries no recognizable version, which includes our own pinned URI --
// that one encodes an OMG publication date rather than a revision number, so it
// is mapped explicitly.
StandardVersion detect_standard_version(std::string_view uri);

// True when `uri` is acceptable as a SACM namespace on import in tolerant
// mode: the pinned URI, any URI containing "/spec/SACM/", the EMF
// reference-implementation family, or the example namespace used by repository
// fixtures.
bool is_accepted_sacm_namespace(std::string_view uri);

// True only for the pinned strict namespace.
inline bool is_strict_sacm_namespace(std::string_view uri) {
    return uri == kSacm;
}

} // namespace sacm::metadata::namespaces
