#include "sacm/metadata/namespaces.h"

namespace sacm::metadata::namespaces {

bool is_xmi_namespace(std::string_view uri) {
    // Matches http://www.omg.org/spec/XMI/<date> and the older
    // http://www.omg.org/XMI, without matching a SACM namespace.
    return uri == kXmi || uri == "http://www.omg.org/XMI" ||
           uri.find("/spec/XMI/") != std::string_view::npos;
}

std::string_view standard_version_name(StandardVersion version) {
    switch (version) {
        case StandardVersion::V2_0:
            return "2.0";
        case StandardVersion::V2_1:
            return "2.1";
        case StandardVersion::V2_2:
            return "2.2";
        case StandardVersion::V2_3:
            return "2.3";
        case StandardVersion::Unknown:
            break;
    }
    return "unknown";
}

StandardVersion detect_standard_version(std::string_view uri) {
    // Our own pin encodes an OMG publication date, not a revision number.
    if (uri == kSacm) {
        return StandardVersion::V2_3;
    }
    // Every other family in the wild spells the revision in the path:
    // http://omg.sacm/2.2/argumentation, http://www.omg.org/spec/SACM/2.2/...,
    // http://example.org/sacm/2.3.
    for (const auto& [needle, version] :
         std::initializer_list<std::pair<std::string_view, StandardVersion>>{
             {"/2.3", StandardVersion::V2_3},
             {"/2.2", StandardVersion::V2_2},
             {"/2.1", StandardVersion::V2_1},
             {"/2.0", StandardVersion::V2_0},
         }) {
        if (uri.find(needle) != std::string_view::npos) {
            return version;
        }
    }
    return StandardVersion::Unknown;
}

bool is_accepted_sacm_namespace(std::string_view uri) {
    if (uri == kSacm) {
        return true;
    }
    if (uri.find("/spec/SACM/") != std::string_view::npos) {
        return true;
    }
    // The EMF reference implementation (github.com/wrwei/SACM, Apache-2.0)
    // declares one namespace per metamodel package rather than one per
    // document: http://omg.sacm/2.2/{base,assurancecase,argumentation,
    // artifact,terminology}. SACM 2.3 does not determine an instance-document
    // namespace at all (the normative MOF model carries no nsURI), so this
    // dialect is no less conformant than our own pin -- and it is what the
    // mainstream SACM tooling emits. Accept the whole family on import;
    // strict export still emits the single pinned namespace.
    if (uri.starts_with(kEmfReferencePrefix)) {
        return true;
    }
    // Repository fixtures use this example namespace.
    return uri == "http://example.org/sacm/2.3";
}

}  // namespace sacm::metadata::namespaces
