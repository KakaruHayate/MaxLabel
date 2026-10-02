// Self-checking test for the phoneme inventory and validation.

#include "maxlabel/vocabulary.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

int main() {
    maxlabel::Vocabulary vocabulary;
    check(vocabulary.empty(), "vocab: empty to start");

    vocabulary.set_symbols({ "zh/ong", "zh/zh", "ch", "en/iy", "en/r", "en/d", "zh/ong" });
    check(vocabulary.size() == 6, "vocab: duplicates collapse");
    check(vocabulary.lookup("zh/ong") >= 0, "vocab: lookup finds a symbol");
    check(vocabulary.lookup("nope") < 0, "vocab: lookup misses a stranger");

    // resolve_phoneme tries the literal first, then "<lang>/<phoneme>".
    check(maxlabel::check_phoneme(vocabulary, "zh/ong", {}).known,
          "check: literal symbol resolves");
    check(maxlabel::check_phoneme(vocabulary, "ong", { "zh" }).known,
          "check: bare symbol resolves through the language prefix");
    check(!maxlabel::check_phoneme(vocabulary, "ong", { "en" }).known,
          "check: the same symbol misses under another language");
    check(!maxlabel::check_phoneme(vocabulary, "qqq", { "zh" }).known,
          "check: an unknown symbol is flagged");

    // The non-lexical symbols are not in the inventory but the aligner knows
    // them, so they must not be flagged.
    check(maxlabel::check_phoneme(vocabulary, "AP", { "zh" }).known,
          "check: AP resolves as a global symbol");
    check(maxlabel::check_phoneme(vocabulary, "SP", {}).known,
          "check: SP resolves as a global symbol");
    check(!maxlabel::check_phoneme(vocabulary, "notasymbol", {}).known,
          "check: a stranger is still flagged");

    // Without an inventory the check cannot run, and must say so rather than
    // reporting everything as unknown.
    {
        const maxlabel::Vocabulary none;
        const maxlabel::PhonemeCheck result = maxlabel::check_phoneme(none, "anything", {});
        check(!result.checkable, "check: no inventory -> not checkable");
        check(!result.known, "check: no inventory -> not known");
    }

    // Loading from a symbol list, with comments and blanks.
    {
        const fs::path path = fs::temp_directory_path() / "maxlabel_vocab.txt";
        {
            std::ofstream out(path, std::ios::binary);
            out << "# the model vocabulary\n"
                   "zh/ong\n"
                   "\n"
                   "  en/iy  \n"
                   "AP\n";
        }
        maxlabel::Vocabulary loaded;
        std::string error;
        check(loaded.load(path.string(), &error), "vocab: file loads");
        check(error.empty(), "vocab: no error on a good file");
        check(loaded.size() == 3, "vocab: comments and blanks are skipped");
        check(loaded.lookup("en/iy") >= 0, "vocab: surrounding whitespace is trimmed");
        fs::remove(path);
    }
    {
        maxlabel::Vocabulary missing;
        std::string error;
        check(!missing.load("no/such/file.txt", &error), "vocab: a missing file fails");
        check(!error.empty(), "vocab: and says why");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
