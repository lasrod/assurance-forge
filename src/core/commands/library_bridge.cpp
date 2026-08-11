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
} // namespace

bool ApplyLegacyOrRefuse(CommandContext& ctx, const LibraryBridgeMutator& mutate, std::string& error) {
    if (ctx.library_document != nullptr) {
        error = "This edit cannot be expressed in SACM 2.3 through the library, and the "
                "compatibility path that used to carry it has been removed. The document is unchanged.";
        return false;
    }
    return mutate(ctx.model, ctx.package, error);
}

bool CanApplyLibraryPrimary(const CommandContext& ctx) {
    return ctx.library_document != nullptr;
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
