#include "core/commands/library_bridge.h"

#include "core/library_package_projection.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "sacm/sacm_serializer.h"

namespace core::commands {

bool BridgeLegacyMutationToLibrary(sacm_adapter::LibraryDocument& document,
                                   const LibraryBridgeMutator& mutate, std::string& error,
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
    if (!mutate(model, package, error))
        return false;
    // ...and re-derive while KEEPING what the projection could not carry. The
    // legacy package has no field for unknown/foreign XML, so a plain reload
    // would erase on every bridged edit exactly the vendor content a tolerant
    // load preserved -- silent data loss on the commands users run most.
    if (!sacm_adapter::reload_document_keeping_compatibility_content(
            document, sacm::serialize_sacm(package))) {
        error = rederive_failure_context.empty()
                    ? std::string("Library bridge re-derive (reload_document) failed.")
                    : "Bridge re-derive (reload_document) failed at " +
                          std::string(rederive_failure_context);
        return false;
    }
    return true;
}

bool ApplyLibraryPrimaryOrLegacy(CommandContext& ctx, const LibraryBridgeMutator& mutate,
                                 std::string& error) {
    if (ctx.library_document != nullptr && ctx.allow_library_primary) {
        if (!BridgeLegacyMutationToLibrary(*ctx.library_document, mutate, error))
            return false;
        ctx.library_primary = true;
        return true;
    }
    return mutate(ctx.model, ctx.package, error);
}

} // namespace core::commands
