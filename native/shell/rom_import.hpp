#pragma once
#include "platform.hpp"
#include <filesystem>
#include <system_error>
#include <utility>

namespace sc64::desktop {
// Asynchronous import of a file the user picked (a ROM dump or a game module). The caller serializes this
// state with the Android callback mutex. A completed import remains busy until take() transfers file
// ownership to the engine.
class RomImportState {
public:
    std::error_code initialize(const std::filesystem::path& storage) {
        auto first_error = finalize();
        root_ = std::filesystem::weakly_canonical(storage);
        alive_ = true; busy_ = false; result_ = {};
        std::error_code error;
        std::filesystem::directory_iterator entries(root_, error), end;
        if (error == std::errc::no_such_file_or_directory) return {};
        if (error) return error;
        while (entries != end) {
            auto cleanup_error = remove_owned(entries->path());
            if (cleanup_error && !first_error) first_error = cleanup_error;
            entries.increment(error);
            if (error) return error;
        }
        return first_error;
    }
    bool alive() const { return alive_; }
    bool busy() const { return busy_; }
    bool begin() {
        if (!alive_ || busy_) return false;
        busy_ = true; return true;
    }
    void cancel() { busy_ = false; }
    std::error_code complete(Sc64Selection result) {
        if (!alive_) { busy_ = false; return remove_owned(result.path); }
        auto error = remove_owned(result_.path);
        result_ = std::move(result);
        busy_ = !result_.path.empty() || !result_.error.empty();
        return error;
    }
    Sc64Selection take() {
        auto result = std::exchange(result_, {});
        if (!result.path.empty() || !result.error.empty()) busy_ = false;
        return result;
    }
    std::error_code finalize() {
        alive_ = false; busy_ = false;
        auto error = remove_owned(result_.path);
        result_ = {};
        return error;
    }
private:
    std::error_code remove_owned(const std::filesystem::path& path) const {
        if (path.empty() || root_.empty()) return {};
        const auto name = path.filename().string();
        const bool import = name.starts_with("sc64-import-") && name.ends_with(".tmp")
            && name.size() > sizeof("sc64-import-") - 1 + sizeof(".tmp") - 1;
        if (!import && name != ".sc64-rom.tmp") return {};
        std::error_code error;
        // Java/NativeActivity may spell /data/data and /data/user/0 differently.
        if (std::filesystem::weakly_canonical(path.parent_path(), error) != root_ || error) return error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory) return {};
        if (error || !std::filesystem::is_regular_file(status)) return error;
        std::filesystem::remove(path, error);
        return error;
    }
    std::filesystem::path root_;
    Sc64Selection result_;
    bool alive_ = false, busy_ = false;
};
}
