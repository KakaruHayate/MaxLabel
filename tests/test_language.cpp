// Self-checking test for language segmentation.

#include "maxlabel/core.h"
#include "maxlabel/language.h"

#include <iostream>
#include <string>
#include <vector>

using maxlabel::LangSpan;

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

// The span languages joined by ",", with "" for an undetermined run — which
// makes the interesting cases readable in a failure message.  Indexed rather
// than guarded on emptiness, so a leading undetermined run is not swallowed.
static std::string langs(const std::vector<LangSpan> & spans) {
    std::string out;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (i != 0) out += ",";
        out += spans[i].language;
    }
    return out;
}

static bool parses(const std::string & pfml) {
    try {
        maxlabel::validate(pfml);
    } catch (const std::exception &) {
        return false;
    }
    return true;
}

int main() {
    // Kana, hangul and latin are settled by the script.
    check(langs(maxlabel::detect_languages("안녕", "")) == "ko", "hangul -> ko");
    check(langs(maxlabel::detect_languages("hello", "")) == "en", "latin -> en");

    // Han is not: 東京 is dongjing or tokyo, and the characters are identical.
    {
        const std::vector<LangSpan> spans = maxlabel::detect_languages("東京");
        check(spans.size() == 1, "han only -> one span");
        check(spans[0].language.empty() && spans[0].ambiguous,
              "han only -> undetermined rather than guessed");
        check(maxlabel::has_undetermined(spans), "han only -> flagged as needing a human");
    }

    // With the project's language, the same run is decided.
    {
        const std::vector<LangSpan> spans = maxlabel::detect_languages("你好世界", "zh");
        check(langs(spans) == "zh", "han with -l zh -> zh");
        check(!maxlabel::has_undetermined(spans), "han with -l zh -> determined");
    }

    // The common case the user called out: Chinese with an English phrase.
    {
        const std::vector<LangSpan> spans =
            maxlabel::detect_languages("今天天气不错 I love you", "zh");
        check(langs(spans) == "zh,en", "chinese + english -> zh then en");
        check(!maxlabel::has_undetermined(spans), "chinese + english -> fully determined");
    }

    // A Japanese line: kana decides itself, the kanji still need an answer.
    {
        const std::vector<LangSpan> spans = maxlabel::detect_languages("東京へ行く");
        check(langs(spans) == ",ja,,ja",
              "kanji+kana without -l -> kana decides, kanji stay undetermined");
        check(maxlabel::has_undetermined(spans), "kanji+kana -> still needs a human");
    }
    {
        const std::vector<LangSpan> spans = maxlabel::detect_languages("東京へ行く", "ja");
        check(langs(spans) == "ja", "kanji+kana with -l ja -> one ja span");
        check(!maxlabel::has_undetermined(spans), "kanji+kana with -l ja -> determined");
    }

    // Serialization.
    {
        const std::string pfml = maxlabel::text_to_pfml("今天天气不错 I love you", "zh");
        check(pfml.find("<scope language=\"zh\">") != std::string::npos, "pfml: zh scope");
        check(pfml.find("<scope language=\"en\">") != std::string::npos, "pfml: en scope");
        check(parses(pfml), "pfml: parses");
    }
    {
        // An undetermined run stays bare text, which is valid PFML and says
        // "no language here" rather than asserting a wrong one.
        const std::string pfml = maxlabel::text_to_pfml("東京", "");
        check(pfml == "東京", "pfml: undetermined run stays bare text");
        check(parses(pfml), "pfml: bare text parses");
    }
    {
        // '&' and '<' are escaped; '>' needs no escaping in XML character data.
        const std::string pfml = maxlabel::text_to_pfml("<hi>", "");
        check(pfml.find("&lt;hi>") != std::string::npos, "pfml: markup in the text is escaped");
        check(parses(pfml), "pfml: escaped text parses");
    }

    // has_undetermined on a decided split.
    check(!maxlabel::has_undetermined(maxlabel::detect_languages("hello", "")),
          "has_undetermined: false for a decided split");

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
