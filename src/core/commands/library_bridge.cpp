#include "core/commands/library_bridge.h"

#include "core/library_package_projection.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "legacy_sacm/sacm_serializer.h"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace core::commands {

namespace {

// Every id the legacy `sacm::AssuranceCasePackage` can hold. Anything the
// document contains that is NOT here is deleted by the projection round trip
// below, because there is no field to put it in.
void IndexRepresentedIds(const sacm::AssuranceCasePackage& package, std::set<std::string>& ids) {
    const auto add = [&ids](const std::string& id) {
        if (!id.empty())
            ids.insert(id);
    };
    add(package.id);
    const auto index_terminology = [&](const sacm::TerminologyPackage& terminology) {
        add(terminology.id);
        for (const sacm::Category& category : terminology.categories)
            add(category.id);
        for (const sacm::Expression& expression : terminology.expressions)
            add(expression.id);
        for (const sacm::Term& term : terminology.terms)
            add(term.id);
    };

    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        add(argument_package.id);
        for (const sacm::Claim& claim : argument_package.claims)
            add(claim.id);
        for (const sacm::ArgumentReasoning& r : argument_package.argumentReasonings)
            add(r.id);
        for (const sacm::ArtifactReference& r : argument_package.artifactReferences)
            add(r.id);
        for (const sacm::AssertedInference& r : argument_package.assertedInferences)
            add(r.id);
        for (const sacm::AssertedContext& r : argument_package.assertedContexts)
            add(r.id);
        for (const sacm::AssertedEvidence& r : argument_package.assertedEvidences)
            add(r.id);
        for (const sacm::TerminologyPackage& t : argument_package.terminologyPackages)
            index_terminology(t);
    }
    for (const sacm::TerminologyPackage& terminology : package.terminologyPackages)
        index_terminology(terminology);
    for (const sacm::ArtifactPackage& artifact_package : package.artifactPackages) {
        add(artifact_package.id);
        for (const sacm::Artifact& artifact : artifact_package.artifacts)
            add(artifact.id);
    }
}

// Human-readable summary of what this edit would destroy, or empty when the
// projection accounts for every element in the document.
//
// SCOPE: the sweep runs over `list_document_elements` -- every element the
// LIBRARY holds, packages included -- not over the projected POD. The
// element-level sweep this replaced could not see a lost *package*: a nested
// ArgumentPackage is omitted by `project_case` (a container, not a drawn node),
// so a document carrying one passed the guard and the bridge deleted it
// silently. Utility elements are the one exclusion: clause 8.7 metadata is
// carried ON its element, so it has no standalone legacy representation to
// index and survives with its carrier.
std::string DescribeUnrepresentableElements(const sacm_adapter::LibraryDocument& document,
                                            const sacm::AssuranceCasePackage& package) {
    std::set<std::string> represented;
    IndexRepresentedIds(package, represented);

    std::vector<std::string> lost;
    for (const sacm_adapter::DocumentElement& element : sacm_adapter::list_document_elements(document)) {
        if (element.is_utility || element.id.empty())
            continue;
        if (represented.count(element.id) == 0)
            lost.push_back(sacm_adapter::sacm_class_name_for_pod_type(element.kind) + " '" + element.id + "'");
    }
    if (lost.empty())
        return {};

    constexpr std::size_t kMaxListed = 5;
    std::string summary;
    for (std::size_t i = 0; i < lost.size() && i < kMaxListed; ++i) {
        if (!summary.empty())
            summary += ", ";
        summary += lost[i];
    }
    if (lost.size() > kMaxListed)
        summary += " and " + std::to_string(lost.size() - kMaxListed) + " more";
    return summary;
}

} // namespace

bool BridgeLegacyMutationToLibrary(sacm_adapter::LibraryDocument& document,
                                   const LibraryBridgeMutator& mutate,
                                   std::string& error,
                                   std::string_view rederive_failure_context) {
    parser::AssuranceCase model = sacm_adapter::project_case(document);
    // The TAG-CARRYING projection, not the audit one.
    //
    // `project_library_package` exists for canonical hashing, where its own
    // contract permits it to "collapse packages as long as it does so
    // consistently on both audit sides", and it never restores vendor
    // TaggedValues. Both are fine for a hash and fatal here, because this
    // projection is not compared -- it is RELOADED, replacing the live
    // document. Bridging through it meant every bridged command (all text
    // edits, terminology, ACP CRUD, package removal, tree reorder) rebuilt the
    // document without its vendor tags: one rename erased all ten
    // `assuranceForge.acp` TaggedValues from an ACP-carrying case, and a case
    // with several argument packages collapsed into one, duplicating
    // artifact-reference ids until `reload_document` rejected the result and
    // the command failed outright.
    //
    // The canonical hash is unaffected: it re-projects from the document
    // through `project_library_package` at the point of hashing, so both audit
    // sides still see the same collapsed view they always did.
    sacm::AssuranceCasePackage package = core::project_library_package_with_tags(document);

    // REFUSE rather than silently delete. This projection is not compared, it is
    // RELOADED over the live document, so anything the legacy package cannot
    // express is gone -- and on a bridged command that reaches disk. Measured on
    // conforming SACM 2.3: a single rename deleted an ArgumentGroup (11.2), an
    // AssertedArtifactSupport (11.17) and an AssertedArtifactContext (11.18)
    // with no diagnostic.
    //
    // Refusing is deliberate and is the smaller half of a choice. Preserving
    // would mean either growing the legacy POD to cover all of SACM 2.3 -- a
    // model this migration exists to retire -- or new library API to extract and
    // re-adopt typed elements across the round trip. Failing the command turns a
    // silent, unannounced corruption of a safety argument into a visible refusal,
    // which is the trade this project makes everywhere else, and it is what the
    // "never silently modify or reinterpret safety arguments" constraint
    // requires. The real fix is retiring the bridge as commands go native.
    //
    // Cost, measured: none on ordinary cases. Every fixture in tests/data, and
    // every argument in the repository's sample projects, projects completely.
    // Only a document carrying one of the unrepresentable kinds is refused, and
    // for those the alternative was losing the content.
    if (const std::string unrepresentable = DescribeUnrepresentableElements(document, package);
        !unrepresentable.empty()) {
        error = "Refused: this edit goes through a conversion that cannot represent part of this "
                "case, and applying it would delete " +
                unrepresentable +
                ". The case is unchanged. This is a known limitation of the legacy edit path "
                "(SACM23-LIB-002): edits on a case carrying these elements are refused until "
                "the affected commands move off that path.";
        return false;
    }

    if (!mutate(model, package, error))
        return false;
    // ...and re-derive while KEEPING what the projection could not carry. The
    // legacy package has no field for unknown/foreign XML, so a plain reload
    // would erase on every bridged edit exactly the vendor content a tolerant
    // load preserved -- silent data loss on the commands users run most.
    if (!sacm_adapter::reload_document_keeping_compatibility_content(document, sacm::serialize_sacm(package))) {
        error = rederive_failure_context.empty()
                    ? std::string("Library bridge re-derive (reload_document) failed.")
                    : "Bridge re-derive (reload_document) failed at " + std::string(rederive_failure_context);
        return false;
    }
    return true;
}

bool ApplyLibraryPrimaryOrLegacy(CommandContext& ctx, const LibraryBridgeMutator& mutate, std::string& error) {
    if (ctx.library_document != nullptr && ctx.allow_library_primary) {
        if (!BridgeLegacyMutationToLibrary(*ctx.library_document, mutate, error))
            return false;
        ctx.library_primary = true;
        return true;
    }
    return mutate(ctx.model, ctx.package, error);
}

bool CanApplyLibraryPrimary(const CommandContext& ctx) {
    return ctx.library_document != nullptr && ctx.allow_library_primary;
}

std::string FormatLibraryDiagnostics(const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
    if (diagnostics.empty())
        return "(no library diagnostics)";
    std::string summary;
    for (const sacm_adapter::LoadDiagnostic& diagnostic : diagnostics) {
        if (!summary.empty())
            summary += "; ";
        summary += diagnostic.code + "/" + diagnostic.severity + ": " + diagnostic.message;
    }
    return summary;
}

std::string LibraryRejection(const std::string& seam, const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics) {
    return "The SACM library rejected " + seam + ": " + FormatLibraryDiagnostics(diagnostics);
}

} // namespace core::commands
