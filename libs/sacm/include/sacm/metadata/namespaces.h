#pragma once

#include <string_view>

// Pinned XMI serialization conventions for SACM 2.3. Single source of truth;
// rationale and derivation: docs/sacm/sacm-2.3-metamodel-inventory.md
// ("XMI serialization conventions"). Correcting a pin is a change here plus
// a golden-fixture refresh.
namespace sacm::metadata::namespaces {

// Strict export namespace (OMG version URI for SACM 2.3).
inline constexpr std::string_view kSacm = "http://www.omg.org/spec/SACM/20220301";
inline constexpr std::string_view kSacmPrefix = "sacm";

inline constexpr std::string_view kXmi = "http://www.omg.org/spec/XMI/20131001";
inline constexpr std::string_view kXmiPrefix = "xmi";
inline constexpr std::string_view kXmiVersion = "2.0";

inline constexpr std::string_view kXsi = "http://www.w3.org/2001/XMLSchema-instance";
inline constexpr std::string_view kXsiPrefix = "xsi";

// True when `uri` is acceptable as a SACM namespace on import in tolerant
// mode: the pinned URI, any URI containing "/spec/SACM/", or the example
// namespace used by repository fixtures.
bool is_accepted_sacm_namespace(std::string_view uri);

// True only for the pinned strict namespace.
inline bool is_strict_sacm_namespace(std::string_view uri) { return uri == kSacm; }

}  // namespace sacm::metadata::namespaces
