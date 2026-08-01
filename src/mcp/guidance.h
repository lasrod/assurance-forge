#pragma once

// SCCG as MCP resources and prompts, so an agent has the house rules before it
// writes anything rather than being corrected afterwards.
//
// Three mechanisms carry SCCG, weakest first:
//
//   1. **Resources** -- the catalog, readable on demand. A client that supports
//      resources can pull the guidelines into context at the start of a session.
//   2. **Prompts** -- pre-written openings for the three things users actually
//      ask for, each carrying the guidance relevant to that job. This is the one
//      that answers "be aware of the rules before it starts": the user picks
//      `draft_argument_from_standard` in their client and the rules arrive with
//      it.
//   3. **Checks on staged work** -- in `core/sccg/staged_checks.h`, returned in
//      the result of every staging call. The only one that is enforcement rather
//      than advice, and deliberately narrow (see that header).
//
// A client supporting none of these still gets (3), which is why the checks
// exist at all.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace mcp {

struct ResourceDefinition {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

struct PromptArgument {
    std::string name;
    std::string description;
    bool required = false;
};

struct PromptDefinition {
    std::string name;
    std::string description;
    std::vector<PromptArgument> arguments;
};

const std::vector<ResourceDefinition>& BuiltinResources();
const std::vector<PromptDefinition>& BuiltinPrompts();

// Reads one resource. `found` distinguishes "no such uri" from "the catalog
// could not be loaded", which are different problems with different fixes.
std::string ReadResource(const std::string& uri, bool& found, std::string& error);

// Builds the message text for a prompt, with the SCCG guidance for that job
// already in it. Empty when the name is unknown.
std::string BuildPrompt(const std::string& name, const nlohmann::json& arguments);

} // namespace mcp
