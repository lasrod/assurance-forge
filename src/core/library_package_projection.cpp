#include "core/library_package_projection.h"

#include "core/sacm_argument_sync.h"
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

} // namespace core
