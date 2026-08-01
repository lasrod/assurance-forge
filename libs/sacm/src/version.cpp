#include "sacm/version.h"

namespace sacm {

std::string_view library_version() {
    return "0.1.0";
}

std::string_view standard_version() {
#ifdef SACM_STANDARD_VERSION
    return SACM_STANDARD_VERSION;
#else
    return "2.3";
#endif
}

} // namespace sacm
