#include "core/library_package_projection.h"

#include "core/audit/canonical_model_hash.h"
#include "core/sacm_argument_sync.h"
#include "sacm/sacm_serializer.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

namespace core {

sacm::AssuranceCasePackage project_library_package(const sacm_adapter::LibraryDocument& document) {
    // The argument content comes from the same POD projection the application
    // renders from, rebuilt into a package by the builder the event replayer
    // already uses -- so the audit's on-disk and replayed sides, both routed
    // through this, agree.
    const core::AssuranceCase projected = sacm_adapter::project_case(document);

    sacm::AssuranceCasePackage package;
    package.id = projected.id;
    package.name = projected.name;
    package.description = projected.description;
    RebuildSacmArgumentPackageFromParser(projected, package);
    // Attach terminology and artifact packages after the argument rebuild
    // (which only touches argumentPackages) so the canonical hash covers them
    // and audit coverage is not reduced.
    package.terminologyPackages = sacm_adapter::project_terminology_packages(document);
    package.artifactPackages = sacm_adapter::project_artifact_packages(document);
    return package;
}

std::optional<std::string> library_canonical_hash_from_xml(std::string_view xml) {
    sacm_adapter::LibraryDocument document;
    if (!sacm_adapter::reload_document(document, xml)) {
        return std::nullopt;
    }
    return core::audit::CanonicalModelHash(project_library_package(document));
}

std::optional<std::string> library_canonical_hash(const sacm::AssuranceCasePackage& package) {
    return library_canonical_hash_from_xml(sacm::serialize_sacm(package));
}

std::optional<std::string> library_canonical_hash_from_file(const std::filesystem::path& path) {
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    if (!loaded.ok || loaded.document == nullptr) {
        return std::nullopt;
    }
    return core::audit::CanonicalModelHash(project_library_package(*loaded.document));
}

std::optional<std::string> library_xmi_from_package(const sacm::AssuranceCasePackage& package) {
    sacm_adapter::LibraryDocument document;
    if (!sacm_adapter::reload_document(document, sacm::serialize_sacm(package))) {
        return std::nullopt;
    }
    sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(document);
    if (!saved.ok) {
        return std::nullopt;
    }
    return std::move(saved.xml);
}

} // namespace core
