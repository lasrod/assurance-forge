#include "app/native_file_dialogs.h"

#include "nfd.hpp"

#include <filesystem>
#include <system_error>

namespace app::dialogs {
namespace {

std::string AbsoluteFolderForDialog(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute_path = std::filesystem::weakly_canonical(path, ec);
    if (ec || absolute_path.empty()) {
        ec.clear();
        absolute_path = std::filesystem::absolute(path, ec);
    }
    if (ec || absolute_path.empty()) return path.u8string();
    return absolute_path.u8string();
}

std::string ExistingFolderForDialog(const std::string& raw_path) {
    std::error_code ec;
    std::filesystem::path fallback = std::filesystem::current_path(ec);
    if (ec || fallback.empty()) fallback = ".";

    if (raw_path.empty()) return AbsoluteFolderForDialog(fallback);

    std::filesystem::path path = std::filesystem::u8path(raw_path);
    if (std::filesystem::is_directory(path, ec)) return AbsoluteFolderForDialog(path);

    ec.clear();
    if (std::filesystem::is_regular_file(path, ec) && path.has_parent_path()) {
        return AbsoluteFolderForDialog(path.parent_path());
    }

    if (path.has_parent_path()) {
        ec.clear();
        if (std::filesystem::is_directory(path.parent_path(), ec)) {
            return AbsoluteFolderForDialog(path.parent_path());
        }
    }

    return AbsoluteFolderForDialog(fallback);
}

DialogResult RunDialog(nfdresult_t result,
                       const NFD::UniquePath& out_path,
                       std::string& selected_path,
                       std::string& error_message) {
    if (result == NFD_OKAY) {
        selected_path = out_path.get() ? out_path.get() : "";
        error_message.clear();
        return DialogResult::Selected;
    }
    if (result == NFD_CANCEL) {
        error_message.clear();
        return DialogResult::Cancelled;
    }

    const char* error = NFD::GetError();
    error_message = error && error[0] != '\0' ? error : "The native file dialog failed.";
    return DialogResult::Failed;
}

class NfdSession {
public:
    NfdSession() : result_(NFD::Init()) {}
    ~NfdSession() {
        if (result_ == NFD_OKAY) {
            NFD::Quit();
        }
    }

    bool ok() const { return result_ == NFD_OKAY; }

    std::string error_message() const {
        const char* error = NFD::GetError();
        return error && error[0] != '\0' ? error : "The native file dialog could not be initialized.";
    }

    NfdSession(const NfdSession&) = delete;
    NfdSession& operator=(const NfdSession&) = delete;

private:
    nfdresult_t result_ = NFD_ERROR;
};

}  // namespace

DialogResult BrowseForProjectParentFolder(const std::string& default_path,
                                          std::string& selected_path,
                                          std::string& error_message) {
    NfdSession session;
    if (!session.ok()) {
        error_message = session.error_message();
        return DialogResult::Failed;
    }

    const std::string default_folder = ExistingFolderForDialog(default_path);
    NFD::UniquePath out_path;
    const nfdresult_t result = NFD::PickFolder(out_path, default_folder.c_str());
    return RunDialog(result, out_path, selected_path, error_message);
}

DialogResult BrowseForProjectManifest(const std::string& default_path,
                                      std::string& selected_path,
                                      std::string& error_message) {
    NfdSession session;
    if (!session.ok()) {
        error_message = session.error_message();
        return DialogResult::Failed;
    }

    const std::string default_folder = ExistingFolderForDialog(default_path);
    nfdfilteritem_t filters[] = {{"Assurance Forge project", "proj"}};
    NFD::UniquePath out_path;
    const nfdresult_t result = NFD::OpenDialog(out_path, filters, 1, default_folder.c_str());
    return RunDialog(result, out_path, selected_path, error_message);
}

}  // namespace app::dialogs