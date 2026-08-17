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
//
// (1) and (2) share a weakness: they land only when the user asks. The
// authoring doctrine below is the answer -- a condensation delivered through
// the channels that reach the model unprompted (`initialize` instructions,
// pre-write read results), so the casual prompt still meets the house rules.

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

// A parameterized resource, published through `resources/templates/list`. The
// per-guideline uri was readable long before it was listed, which made it
// undiscoverable: no client could learn the `sccg://guideline/<id>` form
// without reading this codebase.
struct ResourceTemplateDefinition {
    std::string uri_template;
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
const std::vector<ResourceTemplateDefinition>& BuiltinResourceTemplates();
const std::vector<PromptDefinition>& BuiltinPrompts();

// The authoring doctrine: the rules an agent most needs while its hands are on
// the keyboard, one line per guideline, condensed from the catalog. The prompts
// and resources above reach only an agent whose user asked for them; this text
// travels in `initialize` instructions and on the pre-write read results, the
// channels that land when the user's prompt says nothing about SCCG. See
// docs/features/mcp-authoring-quality-plan.md, phase 1.
const std::string& AuthoringDoctrine();

// The guideline ids the doctrine names, in the order it states them. Exists so
// a test can hold every named id against the loaded catalog -- a condensation
// that names a guideline the catalog no longer has is quietly lying.
const std::vector<std::string>& AuthoringDoctrineGuidelineIds();

// Reads one resource. `found` distinguishes "no such uri" from "the catalog
// could not be loaded", which are different problems with different fixes.
std::string ReadResource(const std::string& uri, bool& found, std::string& error);

// Builds the message text for a prompt, with the SCCG guidance for that job
// already in it. Empty when the name is unknown.
std::string BuildPrompt(const std::string& name, const nlohmann::json& arguments);

} // namespace mcp
