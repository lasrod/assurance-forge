#include "core/commands/library_bridge.h"

#include "core/library_package_projection.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"
#include "sacm/sacm_serializer.h"

namespace core::commands {

bool BridgeLegacyMutationToLibrary(sacm_adapter::LibraryDocument& document,
                                   const LibraryBridgeMutator& mutate, std::string& error) {
    parser::AssuranceCase      model   = sacm_adapter::project_case(document);
    sacm::AssuranceCasePackage package = core::project_library_package(document);
    if (!mutate(model, package, error))
        return false;
    if (!sacm_adapter::reload_document(document, sacm::serialize_sacm(package))) {
        error = "Library bridge re-derive (reload_document) failed.";
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
