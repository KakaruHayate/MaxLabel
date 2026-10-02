#pragma once

// BudouX: the word segmenter split-lang runs over a Chinese or Japanese run
// before asking the detector about the pieces.
//
// It is not a neural model.  It is a linear model over character n-grams — 13
// tables of integer weights, and a boundary is placed wherever the summed
// score is positive.  The models are JSON (ja.json 20 KB, zh-hans.json 64 KB,
// Apache-2.0) and nothing but data differs between languages.

#include <string>
#include <unordered_map>
#include <vector>

namespace maxlabel {

class BudouX {
public:
    // Loads a model file.  Returns false (and fills `error`) when it cannot be
    // read or is not a BudouX model.
    bool load(const std::string & path, std::string * error = nullptr);
    bool ready() const { return ready_; }

    // Splits `text` into chunks.  Concatenating them gives `text` back.
    //
    // A model that is not loaded returns the text as one chunk rather than
    // nothing: callers use this to sub-split before detection, and losing the
    // text there would lose it everywhere.
    std::vector<std::string> parse(const std::string & text) const;

private:
    using Table = std::unordered_map<std::string, int>;
    const Table & table(const char * name) const;

    std::unordered_map<std::string, Table> tables_;
    // The original derives this from the weights: -sum(all weights) * 0.5.
    // It is a half-integer for some models (zh-hans sums to an odd number), so
    // it has to be a floating-point value or the boundary test flips.
    double base_score_ = 0.0;
    bool ready_ = false;
};

}  // namespace maxlabel
