// Where the tool looks for its data files — see include/maxlabel/models.h.

#include "maxlabel/models.h"

#include <filesystem>
#include <string>

namespace maxlabel {

namespace fs = std::filesystem;

namespace {

std::string & directory() {
    static std::string value = MAXLABEL_DEFAULT_MODEL_DIR;
    return value;
}

}  // namespace

const std::string & model_directory() { return directory(); }

void set_model_directory(std::string path) {
    if (path.empty()) return;
    directory() = std::move(path);
}

std::string executable_directory(const std::string & argv0) {
    if (argv0.empty()) return std::string();
    std::error_code error;
    // argv[0] may be a bare name found on PATH, in which case there is no
    // directory to speak of and the caller keeps its default.
    fs::path path(argv0);
    if (path.has_parent_path()) {
        fs::path parent = fs::weakly_canonical(path, error);
        if (error) parent = path;
        return parent.parent_path().string();
    }
    return std::string();
}

bool use_bundled_models(const std::string & argv0) {
    const std::string base = executable_directory(argv0);
    if (base.empty()) return false;
    const fs::path bundled = fs::path(base) / "models";
    std::error_code error;
    if (!fs::is_directory(bundled, error)) return false;
    set_model_directory(bundled.string());
    return true;
}

}  // namespace maxlabel
