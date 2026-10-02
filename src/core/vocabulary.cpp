// Phoneme inventory and validation — see include/maxlabel/vocabulary.h.

#include "maxlabel/vocabulary.h"

#include "tifa_ggml/g2p.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace maxlabel {

namespace {

std::string trim(const std::string & s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

bool Vocabulary::load(const std::string & path, std::string * error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) *error = "cannot read " + path;
        return false;
    }
    std::vector<std::string> symbols;
    std::string line;
    while (std::getline(in, line)) {
        const std::string symbol = trim(line);
        if (symbol.empty() || symbol[0] == '#') continue;
        symbols.push_back(symbol);
    }
    set_symbols(std::move(symbols));
    return true;
}

void Vocabulary::set_symbols(std::vector<std::string> symbols) {
    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    symbols_ = std::move(symbols);
}

int Vocabulary::lookup(const std::string & symbol) const {
    const auto it = std::lower_bound(symbols_.begin(), symbols_.end(), symbol);
    if (it == symbols_.end() || *it != symbol) return -1;
    return static_cast<int>(it - symbols_.begin());
}

const std::vector<std::string> & default_global_symbols() {
    // configs/g2p.yaml's global_symbols in tifa.cpp / openvpi TIFA.
    static const std::vector<std::string> symbols = {
        "AP", "SP", "EP", "GS", "sil", "br", "pau"
    };
    return symbols;
}

PhonemeCheck check_phoneme(const Vocabulary & vocabulary,
                           const std::string & phoneme,
                           const std::vector<std::string> & languages,
                           const std::vector<std::string> & global_symbols) {
    PhonemeCheck out;
    out.symbol = phoneme;
    out.checkable = !vocabulary.empty();
    if (!out.checkable || phoneme.empty()) return out;

    // The non-lexical symbols are declared by the pipeline rather than looked
    // up by language, so they resolve on their own.  (tifa_ggml's
    // resolve_phoneme uses global_symbols only to decide whether to try a
    // language prefix — it does not make a symbol resolvable by itself, which
    // is why this is checked here and not left to it.)
    if (std::find(global_symbols.begin(), global_symbols.end(), phoneme) !=
        global_symbols.end()) {
        out.known = true;
        return out;
    }

    const tifa_ggml::G2PLookup lookup =
        [&vocabulary](const std::string & symbol) { return vocabulary.lookup(symbol); };
    out.known = tifa_ggml::resolve_phoneme(phoneme, languages, lookup, global_symbols, {}).id >= 0;
    return out;
}

}  // namespace maxlabel
