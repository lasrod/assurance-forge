#pragma once

// The tool surface Assurance Forge exposes over MCP.
//
// Every tool in this slice is READ-ONLY. The write surface (proposal drafts a
// human accepts) is a later phase; nothing here mutates the model, the audit
// store, or any file.

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mcp {

class Session;

// MCP distinguishes a *tool* failure from a JSON-RPC protocol error: a tool
// failure is a successful call whose result carries `isError`, so the model can
// see what went wrong and adjust rather than the connection faulting.
struct ToolResult {
    nlohmann::json payload;
    bool           is_error = false;

    static ToolResult Ok(nlohmann::json payload);
    static ToolResult Error(std::string message);
};

struct ToolDefinition {
    std::string    name;
    std::string    description;
    nlohmann::json input_schema;
    // True when the tool returns assurance-case content, and must therefore be
    // refused until the user has enabled MCP. Set this on every new tool that
    // reads the model -- it is the whole consent gate.
    bool           returns_case_content = true;

    std::function<ToolResult(Session&, const nlohmann::json& arguments)> handler;
};

const std::vector<ToolDefinition>& BuiltinTools();

// Null when no tool has that name.
const ToolDefinition* FindTool(std::string_view name);

} // namespace mcp
