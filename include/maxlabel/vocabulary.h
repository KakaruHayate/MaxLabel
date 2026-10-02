#pragma once

// The model's phoneme inventory, and the check that a phoneme resolves.
//
// The aligner's vocabulary lives inside its GGUF.  MaxLabel is the step before
// the aligner and deliberately has no ggml, so it takes an exported symbol list
// instead of growing a GGUF reader — one symbol per line, '#' starts a comment.
//
// Without an inventory the check cannot run, and says so rather than reporting
// every phoneme as unknown: a tool that flags everything is as useless as one
// that flags nothing.

#include <cstddef>
#include <string>
#include <vector>

namespace maxlabel {

class Vocabulary {
public:
    // Reads one symbol per line, skipping blanks and '#' comments.  Returns
    // false (and fills `error`) when the file cannot be read.
    bool load(const std::string & path, std::string * error = nullptr);

    // Replaces the inventory directly — the tests use this, and a caller that
    // already has the symbols in hand should not have to write them out.
    void set_symbols(std::vector<std::string> symbols);

    bool empty() const { return symbols_.empty(); }
    std::size_t size() const { return symbols_.size(); }
    const std::vector<std::string> & symbols() const { return symbols_; }

    // The token id for a symbol, or -1 when it is not in the inventory.
    int lookup(const std::string & symbol) const;

private:
    std::vector<std::string> symbols_;
};

// The non-lexical symbols the aligner always knows: breath, pause, silence.
// These are not in the phoneme inventory, so they are checked separately.
const std::vector<std::string> & default_global_symbols();

struct PhonemeCheck {
    bool        checkable = false;   // an inventory was available at all
    bool        known = false;       // and this phoneme resolves against it
    std::string symbol;              // what was looked up
};

// Whether `phoneme` resolves: literally, or as "<language>/<phoneme>" for one
// of `languages`, or as one of the non-lexical symbols.
PhonemeCheck check_phoneme(const Vocabulary & vocabulary,
                           const std::string & phoneme,
                           const std::vector<std::string> & languages,
                           const std::vector<std::string> & global_symbols =
                               default_global_symbols());

}  // namespace maxlabel
