#include "sacm/metadata/namespaces.h"

namespace sacm::metadata::namespaces {

bool is_accepted_sacm_namespace(std::string_view uri) {
    if (uri == kSacm) {
        return true;
    }
    if (uri.find("/spec/SACM/") != std::string_view::npos) {
        return true;
    }
    // Repository fixtures use this example namespace.
    return uri == "http://example.org/sacm/2.3";
}

}  // namespace sacm::metadata::namespaces
