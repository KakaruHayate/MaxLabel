// MaxLabel's PFML output has to be accepted by the aligner's parser, and that
// parser does not implement all of PFML 1.0 yet (issue #1 records five gaps in
// tifa.cpp <= v0.1.2).
//
// These lock the properties the editor must hold up its end of:
//
//   * no comments and no CDATA — tifa.cpp rejects both outright, and a sample
//     whose PFML does not parse is skipped with no fallback
//   * no unknown attributes — tifa.cpp silently ignores them today, but PFML
//     1.0 makes them an error, so emitting one would break on the fix
//   * no <scope> with an empty language, and language="" only where there is
//     no enclosing scope to inherit from (tifa.cpp treats language="" as
//     absent rather than as "clear")
//
// Every input is also run through validate_pfml, which is the export gate.

#include "maxlabel/annotate.h"
#include "maxlabel/import_pfml.h"
#include "maxlabel/core.h"

#include <cctype>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using maxlabel::LangSpan;
using maxlabel::Override;
using maxlabel::WordBoundary;

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

// Every attribute name appearing in a tag.  Quote-aware, so an '=' inside a
// value is not mistaken for the start of another attribute.
static std::vector<std::string> attribute_names(const std::string & pfml) {
    std::vector<std::string> names;
    std::size_t i = 0;
    while (i < pfml.size()) {
        if (pfml[i] != '<') {
            ++i;
            continue;
        }
        ++i;
        if (i < pfml.size() && (pfml[i] == '!' || pfml[i] == '?')) {
            names.push_back("SPECIAL");
            continue;
        }
        if (i < pfml.size() && pfml[i] == '/') ++i;

        bool in_quotes = false;
        std::string current;
        while (i < pfml.size()) {
            const char c = pfml[i];
            if (c == '"') {
                in_quotes = !in_quotes;
                if (!in_quotes && !current.empty()) {
                    names.push_back(current);
                    current.clear();
                }
                ++i;
                continue;
            }
            if (c == '>' && !in_quotes) {
                ++i;
                break;
            }
            if (!in_quotes) {
                if (c == '=') {
                    // The name is what precedes '='; it was accumulated below.
                    if (!current.empty()) {
                        names.push_back(current);
                        current.clear();
                    }
                    ++i;
                    continue;
                }
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
                    current.push_back(c);
                } else {
                    current.clear();
                }
            }
            ++i;
        }
    }
    return names;
}

static void verify(const std::string & what, const std::string & pfml) {
    const bool parses = [&] {
        try {
            maxlabel::validate(pfml);
        } catch (const std::exception &) {
            return false;
        }
        return true;
    }();

    check(parses, what + ": parses");
    check(pfml.find("<!--") == std::string::npos, what + ": no comment emitted");
    // The markup form, not the word: escaped text may legitimately contain the
    // letters "CDATA".
    check(pfml.find("<![CDATA[") == std::string::npos, what + ": no CDATA section emitted");
    check(pfml.find("<scope language=\"\"") == std::string::npos,
          what + ": no scope with an empty language");

    static const std::set<std::string> known = {
        "scope", "word", "phoneme", "language", "script", "phonemes", "symbol", "text"
    };
    for (const std::string & name : attribute_names(pfml)) {
        if (name == "SPECIAL") {
            check(false, what + ": a comment or processing instruction was emitted");
            continue;
        }
        if (known.count(name) == 0) {
            check(false, what + ": unknown attribute '" + name + "'");
            return;
        }
    }
}

static std::string annotate(const std::string & text,
                            const std::vector<LangSpan> & spans,
                            const std::vector<WordBoundary> & words,
                            const std::vector<Override> & overrides) {
    return maxlabel::annotate_to_pfml(text, spans, words, overrides, nullptr);
}

int main() {
    // Plain text, including the characters that have to be escaped.
    {
        const std::string text = "a < b & c";
        verify("plain text", maxlabel::text_to_pfml(text, ""));
        check(maxlabel::text_to_pfml(text, "").find("&lt;") != std::string::npos,
              "plain text: '<' is escaped, so it cannot start a comment");
    }

    // The languages, decided and undecided.
    {
        const std::string text = "今天天气不错 I love you";
        verify("mixed languages", maxlabel::text_to_pfml(text, "zh"));
        verify("undetermined han", maxlabel::text_to_pfml("東京", ""));
    }

    // A fixed word inside a scope.
    {
        const std::string text = "今天天气不错 I love you";
        const std::vector<LangSpan> spans = maxlabel::detect_languages(text, "zh");
        verify("fixed word", annotate(text, spans, { WordBoundary{ 0, 6 } }, {}));
    }

    // A pinned pronunciation: the direct-word form, with its attributes.
    {
        const std::string text = "重来";
        const std::vector<LangSpan> spans = maxlabel::detect_languages(text, "zh");
        Override pinned;
        pinned.begin = 0;
        pinned.end = 3;
        pinned.script = "chong";
        pinned.phonemes = { "ch", "ong" };
        verify("pinned pronunciation", annotate(text, spans, {}, { pinned }));
    }

    // An insertion, which has no text to point at.
    {
        const std::string text = "重来";
        const std::vector<LangSpan> spans = maxlabel::detect_languages(text, "zh");
        Override inserted;
        inserted.begin = 6;
        inserted.end = 6;
        inserted.phonemes = { "n" };
        const std::string pfml = annotate(text, spans, {}, { inserted });
        verify("inserted phoneme", pfml);
        check(pfml.find("<phoneme symbol=\"n\"/>") != std::string::npos,
              "inserted phoneme: emitted as <phoneme>");
    }

    // An override inside a scope must carry the scope's language rather than
    // an empty one: tifa.cpp reads language="" as "absent", i.e. inherit.
    {
        const std::string text = "重来";
        const std::vector<LangSpan> spans = maxlabel::detect_languages(text, "zh");
        Override pinned;
        pinned.begin = 0;
        pinned.end = 3;
        pinned.phonemes = { "ch", "ong" };
        const std::string pfml = annotate(text, spans, {}, { pinned });
        verify("override in a scope", pfml);
        check(pfml.find("language=\"zh\"") != std::string::npos,
              "override in a scope: the scope language is carried, not cleared");
        check(pfml.find("language=\"\"") == std::string::npos,
              "override in a scope: no empty language attribute");
    }

    // Text that looks like markup is escaped, not emitted as markup.
    {
        const std::string text = "<!-- x --> <![CDATA[y]]>";
        const std::string pfml = maxlabel::text_to_pfml(text, "");
        verify("markup-looking text", pfml);
        check(pfml.find("&lt;!--") != std::string::npos,
              "markup-looking text: the '<' is escaped, so it cannot open a comment");
        check(pfml.find("&lt;![CDATA[") != std::string::npos,
              "markup-looking text: and cannot open a CDATA section");
    }

    // Reading back what was written.  Every fragment the tool emits has to be
    // reconstructible, or editing a file this tool wrote would quietly change
    // it — and the check is a round trip rather than a field-by-field
    // comparison: the second export has to equal the first, however the
    // reading happens to be spelled.
    {
        const std::string text = "衬衫的价格是9.15便士";
        const std::vector<LangSpan> spans = maxlabel::detect_languages(text, "zh");
        Override pinned;
        pinned.begin    = 6;
        pinned.end      = 9;
        pinned.script   = "de";
        pinned.phonemes = { "d", "e" };
        Override inserted;
        inserted.begin    = 21;
        inserted.end      = 21;
        inserted.phonemes = { "n" };
        const std::string first = annotate(text, spans, {}, { pinned, inserted });
        verify("round trip: the first export", first);

        const maxlabel::ImportedFragment read = maxlabel::import_pfml(first);
        check(read.error.empty(), "round trip: the fragment is understood");
        check(read.text == text, "round trip: the text comes back");
        check(read.overrides.size() == 2, "round trip: both overrides come back");
        check(read.words.empty(), "round trip: an override is not also a plain word");

        const std::string second = annotate(read.text, read.spans, read.words,
                                            read.overrides);
        check(second == first, "round trip: the second export is identical");
    }

    // Two <phoneme> tags at one offset are one insertion of two sounds.
    // Reading them as two insertions stacked on the same point is what put two
    // identical blocks in the strip for a single decision, and left no way to
    // tell which of them was which.
    {
        const std::string fragment =
            "<scope language=\"zh\">a<phoneme symbol=\"n\"/><phoneme symbol=\"m\"/>b</scope>";
        verify("consecutive insertions", fragment);
        const maxlabel::ImportedFragment read = maxlabel::import_pfml(fragment);
        check(read.error.empty(), "consecutive insertions: understood");
        check(read.overrides.size() == 1,
              "consecutive insertions: they are one insertion, not two");
        if (read.overrides.size() == 1) {
            check(read.overrides.front().phonemes.size() == 2,
                  "consecutive insertions: carrying both sounds");
            check(read.overrides.front().inserts(), "consecutive insertions: at a point");
        }
        const std::string again = annotate(read.text, read.spans, {}, read.overrides);
        check(again == fragment, "consecutive insertions: the export is unchanged");

        // Separated by text they are two insertions, at two positions.
        const std::string apart =
            "<scope language=\"zh\">a<phoneme symbol=\"n\"/>b<phoneme symbol=\"m\"/>c</scope>";
        const maxlabel::ImportedFragment two = maxlabel::import_pfml(apart);
        check(two.overrides.size() == 2, "insertions separated by text stay two");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
