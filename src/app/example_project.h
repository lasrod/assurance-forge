#pragma once

#include <filesystem>
#include <string>

namespace app {

// The example project a first-time user opens from the welcome screen: the
// kitchen-blender case from the assurance-forge-examples repository, shipped
// beside the executable at examples/kitchen-blender.
//
// The shipped copy is never opened in place. Opening a project writes to it --
// its manifest's hashes, the draft workspace -- and an installed copy may sit in
// a folder the user cannot write. The user gets a copy of their own instead.

inline constexpr const char* kExampleProjectFolderName = "kitchen-blender";

// The shipped example project's folder under `executable_dir`, or empty when
// this build does not carry one.
std::filesystem::path FindBundledExampleProject(const std::filesystem::path& executable_dir);

// Where the user's copy goes: "Assurance Forge Examples" in their Documents
// folder, or in their home folder where there is no Documents folder. Empty
// when neither can be found.
std::filesystem::path DefaultExampleCopyRoot();

struct ExampleCopyResult {
    bool success = false;
    // The af.proj of the user's copy, to open.
    std::filesystem::path manifest;
    // True when an earlier copy was found and reopened rather than made anew:
    // whatever the user changed in it is theirs, and is never overwritten.
    bool reused_existing = false;
    // Already translated: it goes straight to the status bar.
    std::string error;
};

// Copies `bundled_project` into `copy_root`, or finds the copy made last time.
ExampleCopyResult PrepareExampleProjectCopy(const std::filesystem::path& bundled_project,
                                            const std::filesystem::path& copy_root);

} // namespace app
