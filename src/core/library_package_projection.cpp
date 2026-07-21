#include "core/library_package_projection.h"

#include "core/audit/canonical_model_hash.h"
#include "core/sacm_argument_sync.h"
#include "sacm/sacm_serializer.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <unordered_map>

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

sacm::AssuranceCasePackage project_library_package_with_tags(
    const sacm_adapter::LibraryDocument& document) {
    // Unlike the audit projection (project_library_package), which may collapse
    // packages as long as it does so consistently on both audit sides, the
    // application's sacm_package must faithfully preserve the argument-package
    // structure: ACP confidence arguments live in their own package, so a
    // collapse would merge them into the main package and drop their purpose tag
    // on reload. Rebuild one sacm::ArgumentPackage per library package.
    const core::AssuranceCase flat = sacm_adapter::project_case(document);

    sacm::AssuranceCasePackage package;
    package.id = flat.id;
    package.name = flat.name;
    package.description = flat.description;
    package.terminologyPackages = sacm_adapter::project_terminology_packages(document);
    package.artifactPackages = sacm_adapter::project_artifact_packages(document);

    std::unordered_map<std::string, const core::SacmElement*> element_by_id;
    for (const core::SacmElement& element : flat.elements) {
        element_by_id[element.id] = &element;
    }

    std::vector<sacm_adapter::ArgumentPackageShell> shells =
        sacm_adapter::project_argument_package_shells(document);
    for (sacm_adapter::ArgumentPackageShell& shell : shells) {
        core::AssuranceCase filtered;
        for (const std::string& element_id : shell.element_ids) {
            const auto it = element_by_id.find(element_id);
            if (it != element_by_id.end()) {
                filtered.elements.push_back(*it->second);
            }
        }
        sacm::AssuranceCasePackage temp;
        // Terminology packages let RebuildSacm classify terminology artifact
        // references. The target argument package starts fresh (identity only,
        // empty element lists), so RebuildSacm's hidden-term preservation
        // branches are no-ops -- it simply converts the filtered POD elements.
        temp.terminologyPackages = package.terminologyPackages;
        temp.argumentPackages.push_back(std::move(shell.identity));
        RebuildSacmArgumentPackageFromParser(filtered, temp);
        package.argumentPackages.push_back(std::move(temp.argumentPackages.front()));
    }
    if (package.argumentPackages.empty()) {
        // No argument packages in the library (unusual); fall back to the flat
        // rebuild so a single package is still produced.
        RebuildSacmArgumentPackageFromParser(flat, package);
    }

    // Restore the fields the POD drops: vendor TaggedValues (ACP, confidence
    // purpose, GSN role) and each artifact reference's referencedArtifact.
    sacm_adapter::copy_library_tags_onto_package(document, package);
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
