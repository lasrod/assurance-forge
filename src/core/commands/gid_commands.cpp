#include "core/commands/gid_commands.h"

#include "core/commands/library_bridge.h"
#include "core/sacm_identity.h"
#include "parser/model_utils.h"

namespace core::commands {

bool EnsureElementGidCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    const parser::SacmElement* element = parser::FindElementById(ctx.model, element_id_);
    if (element == nullptr) {
        out_error = "Could not find element '" + element_id_ + "' to assign a SACM gid.";
        return false;
    }
    // Already identified: a benign no-op that records no transaction. Return false
    // with an EMPTY out_error, matching the ACP/remove no-op convention (the bus
    // appends nothing; the caller reads empty-error-false as "nothing to do").
    if (!element->gid.empty()) {
        out_error.clear();
        return false;
    }

    // The gid is random, so generate it ONCE and cache it on the instance: a
    // re-apply of the same command object (as the flip test does to compare the
    // library-primary and legacy routings) reuses the value instead of minting a
    // divergent one. Each live command is applied exactly once; replay never
    // reconstructs this command (it forces the recorded gid via core::SetElementGid).
    if (generated_gid_.empty())
        generated_gid_ = core::GenerateUniqueElementGid(ctx.model);

    const LibraryBridgeMutator mutate =
        [&](parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        return core::SetElementGid(model, &package, element_id_, generated_gid_, err);
    };
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

    out_event.event_type = "SetElementGid";
    out_event.payload = nlohmann::ordered_json::object();
    out_event.payload["element_id"] = element_id_;
    out_event.payload["gid"] = generated_gid_;
    return true;
}

} // namespace core::commands
