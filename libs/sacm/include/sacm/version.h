#pragma once

#include <string_view>

namespace sacm {

// Version of this library.
std::string_view library_version();

// Version of the OMG SACM standard this library implements ("2.3").
std::string_view standard_version();

} // namespace sacm
