#pragma once

// Which argument file a project currently has open.
//
// A project can hold several SACM arguments. The application knows which one the
// user is looking at; a separate process -- the MCP server -- otherwise has to
// guess, and guessing "the first one" meant an AI client reasoning about
// `main.sacm` while the user edited `main2.sacm`, proposing changes against an
// argument they were not looking at.
//
// Deliberately a sidecar rather than a field in `af.proj`. Two reasons: this is
// per-user session state, not project data, so it has no business in a manifest
// that gets shared or committed; and the manifest is hash-tracked, so touching it
// on every file switch would churn hashes for something that is not part of the
// assurance case.
//
// Single writer: the application writes it, everything else reads. That is the
// rule that keeps the project files consistent, learned the hard way when two
// processes both wrote the manifest and the later save silently reverted the
// earlier one.

#include <filesystem>
#include <string>

namespace core {

// `<project_root>/.af-session.json`.
std::filesystem::path ActiveArgumentFilePath(const std::filesystem::path& project_root);

// The project-relative path of the active argument, or empty when there is no
// sidecar, it cannot be read, or it names nothing. Callers fall back to their own
// default; this never invents one.
std::string ReadActiveArgument(const std::filesystem::path& project_root);

// Records `relative_path` as the active argument. Failure is not worth
// interrupting a file open for -- the cost is an external reader falling back to
// its default -- so callers may ignore the result.
bool WriteActiveArgument(const std::filesystem::path& project_root,
                         const std::filesystem::path& relative_path, std::string& error);

} // namespace core
