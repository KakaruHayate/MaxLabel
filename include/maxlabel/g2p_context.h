#pragma once

// The optional G2P pipeline.
//
// When it is configured, the editor can offer the pronunciations the dictionary
// already knows and the human picks one; without it, phonemes are typed by
// hand.  Both paths write the same Override, so nothing downstream changes.
//
// The pipeline is built from a g2p config JSON plus a dictionary directory —
// the same pair tifa_ggml_g2p reads out of a model.  MaxLabel takes them as
// files rather than reading the GGUF, because reading a GGUF means ggml, and
// this tool is the step before the aligner precisely so it does not need it.

#include "maxlabel/vocabulary.h"

#include "tifa_ggml/g2p.h"

#include <memory>
#include <string>
#include <vector>

namespace maxlabel {

class G2PContext {
public:
    G2PContext();
    ~G2PContext();
    G2PContext(G2PContext &&) noexcept;
    G2PContext & operator=(G2PContext &&) noexcept;

    // Reads the config JSON and builds the pipeline.  Returns false (and fills
    // `error`) when the file cannot be read or the config is rejected.
    bool load(const std::string & config_json_path, const std::string & dict_dir,
              std::string * error = nullptr);

    bool ready() const;

    // The pronunciations the pipeline offers, per word.  Empty when the
    // pipeline is not configured, or when nothing claims the text.
    std::vector<tifa_ggml::G2PWordCandidates>
    candidates(const std::string & text, const std::vector<std::string> & languages) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace maxlabel
