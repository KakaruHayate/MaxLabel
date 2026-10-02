#pragma once

// Building a G2P configuration from a model directory.
//
// The pronunciation candidates come from the dictionaries the aligner ships
// with, and those are ordinary files: dictionaries/*.txt for the word lists and
// cpp_pinyin/<language>/ for the pinyin engine.  A user who has a TIFA model
// directory therefore has everything needed for candidates, and should not have
// to write a g2p config by hand to get them.

#include <string>

namespace maxlabel {

// A g2p config JSON covering the languages whose dictionaries are present in
// `model_dir` (the layout the aligner ships: dictionaries/ and cpp_pinyin/).
//
// Returns an empty string when nothing usable is there — which is the signal to
// run without candidates rather than with a half-configured pipeline.
std::string build_g2p_config(const std::string & model_dir);

}  // namespace maxlabel
