// The optional G2P pipeline — see include/maxlabel/g2p_context.h.

#include "maxlabel/g2p_context.h"

#include <fstream>
#include <sstream>

namespace maxlabel {

struct G2PContext::Impl {
    std::unique_ptr<tifa_ggml::G2PPipeline> pipeline;
};

G2PContext::G2PContext() : impl_(std::make_unique<Impl>()) {}
G2PContext::~G2PContext() = default;
G2PContext::G2PContext(G2PContext &&) noexcept = default;
G2PContext & G2PContext::operator=(G2PContext &&) noexcept = default;

bool G2PContext::load(const std::string & config_json_path, const std::string & dict_dir,
                      std::string * error) {
    std::ifstream in(config_json_path, std::ios::binary);
    if (!in) {
        if (error != nullptr) *error = "cannot read the g2p config " + config_json_path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return loadConfig(buffer.str(), dict_dir, error);
}

bool G2PContext::loadConfig(const std::string & config_json, const std::string & dict_dir,
                            std::string * error) {
    impl_->pipeline.reset();
    try {
        impl_->pipeline = std::make_unique<tifa_ggml::G2PPipeline>(
            tifa_ggml::G2PPipeline::from_config(config_json, dict_dir));
    } catch (const std::exception & failure) {
        if (error != nullptr) *error = failure.what();
        impl_->pipeline.reset();
        return false;
    }
    return true;
}

bool G2PContext::ready() const { return impl_ != nullptr && impl_->pipeline != nullptr; }

std::vector<tifa_ggml::G2PWordCandidates>
G2PContext::candidates(const std::string & text, const std::vector<std::string> & languages) const {
    if (!ready() || text.empty()) return {};
    try {
        return tifa_ggml::candidates(*impl_->pipeline, text, languages);
    } catch (const std::exception &) {
        // A fragment the configured converters do not claim is not an error
        // here: the author is free to type the phonemes instead.
        return {};
    }
}

}  // namespace maxlabel
