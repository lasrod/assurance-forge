#include "app/example_project.h"

#include "core/string_utils.h"
#include "ui/i18n/localization.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#endif

namespace app {
namespace {

constexpr const char* kManifestName = "af.proj";
constexpr const char* kCopyFolderName = "Assurance Forge Examples";
// The per-location audit history and draft workspace; never part of a copy.
constexpr const char* kWorkspaceFolderName = ".af";

bool IsProjectFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    return std::filesystem::is_regular_file(folder / kManifestName, ec);
}

#ifdef _WIN32
// The Documents folder the shell reports, which follows a OneDrive or policy
// redirection that %USERPROFILE%\Documents does not.
std::filesystem::path DocumentsFolder() {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &path)))
        result = std::filesystem::path(path);
    CoTaskMemFree(path);
    return result;
}
#else
std::filesystem::path DocumentsFolder() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
        return {};
    const std::filesystem::path documents = std::filesystem::path(home) / "Documents";
    std::error_code ec;
    return std::filesystem::is_directory(documents, ec) ? documents : std::filesystem::path(home);
}
#endif

// Copies a project's files, leaving out the .af/ folder: that is the audit
// history and draft workspace of wherever the project was last opened, and a
// fresh copy must start without one or it opens warning that its history does
// not match its SACM. A file already at the destination -- from a copy an
// earlier failure left half-done -- is kept, never replaced.
void CopyProjectFiles(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    ec.clear();
    std::filesystem::recursive_directory_iterator it(from, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const std::filesystem::path relative = it->path().lexically_relative(from);
        if (*relative.begin() == kWorkspaceFolderName) {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        const std::filesystem::path target = to / relative;
        if (it->is_directory()) {
            std::filesystem::create_directories(target, ec);
        } else {
            std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::skip_existing, ec);
        }
    }
}

} // namespace

std::filesystem::path FindBundledExampleProject(const std::filesystem::path& executable_dir) {
    if (executable_dir.empty())
        return {};
    const std::filesystem::path bundled = executable_dir / "examples" / kExampleProjectFolderName;
    return IsProjectFolder(bundled) ? bundled : std::filesystem::path{};
}

std::filesystem::path DefaultExampleCopyRoot() {
    const std::filesystem::path documents = DocumentsFolder();
    return documents.empty() ? std::filesystem::path{} : documents / kCopyFolderName;
}

ExampleCopyResult PrepareExampleProjectCopy(const std::filesystem::path& bundled_project,
                                            const std::filesystem::path& copy_root) {
    ExampleCopyResult result;
    if (!IsProjectFolder(bundled_project)) {
        result.error = AF_TR("The example project is not part of this installation.");
        return result;
    }
    if (copy_root.empty()) {
        result.error = AF_TR("No folder was found to copy the example project into.");
        return result;
    }

    const std::filesystem::path copy = copy_root / bundled_project.filename();
    if (IsProjectFolder(copy)) {
        result.success = true;
        result.reused_existing = true;
        result.manifest = copy / kManifestName;
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(copy, ec);
    if (ec) {
        result.error = ui::i18n::trf("Could not create {0}: {1}", core::PathToUtf8(copy), ec.message());
        return result;
    }
    CopyProjectFiles(bundled_project, copy, ec);
    if (ec || !IsProjectFolder(copy)) {
        result.error =
            ec ? ui::i18n::trf("Could not copy the example project to {0}: {1}", core::PathToUtf8(copy), ec.message())
               : ui::i18n::trf("Could not copy the example project to {0}", core::PathToUtf8(copy));
        return result;
    }
    result.success = true;
    result.manifest = copy / kManifestName;
    return result;
}

} // namespace app
