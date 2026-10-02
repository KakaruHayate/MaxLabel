// Self-checking test for language segmentation.

#include "maxlabel/core.h"
#include "maxlabel/language.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
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

    // Segment level: a plain transcript is segmented on load, and a manual
    // override pins one range and regenerates the PFML.
    {
        const fs::path dir = fs::temp_directory_path() / "maxlabel_lang_test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        {
            std::ofstream out(dir / "line.txt", std::ios::binary);
            out << "今天天气不错 I love you";
        }

        const maxlabel::Project project = maxlabel::scan(dir.string());
        check(project.segments.size() == 1, "segment: one segment scanned");
        maxlabel::Segment segment = project.segments.front();

        check(segment.spans.size() == 2, "segment: two spans detected on load");
        check(maxlabel::has_undetermined(segment.spans),
              "segment: the Han run is undetermined without a default language");
        check(segment.pfml.find("<scope language=\"en\">") != std::string::npos,
              "segment: pfml generated from the spans");
        check(parses(segment.pfml), "segment: generated pfml parses");

        // The default language settles the Han run.
        segment.default_language = "zh";
        maxlabel::detect_spans(segment);
        check(segment.spans.front().language == "zh",
              "segment: default_language decides the Han run");
        check(!maxlabel::has_undetermined(segment.spans),
              "segment: fully determined with a default language");

        // A manual pin survives as `manual`, and re-splitting clears it.
        segment.default_language.clear();
        maxlabel::detect_spans(segment);
        const LangSpan han = segment.spans.front();
        maxlabel::set_span_language(segment, han.begin, han.end, "yue");
        check(segment.spans.front().language == "yue" && segment.spans.front().manual,
              "segment: manual override pinned and flagged");
        check(segment.pfml.find("<scope language=\"yue\">") != std::string::npos,
              "segment: pfml regenerated with the pinned language");
        check(parses(segment.pfml), "segment: regenerated pfml parses");

        maxlabel::detect_spans(segment);
        check(!segment.spans.front().manual,
              "segment: re-splitting discards manual spans");

        // An out-of-range request is ignored rather than corrupting the spans.
        const std::size_t before = segment.spans.size();
        maxlabel::set_span_language(segment, 0, 100000, "zh");
        check(segment.spans.size() == before, "segment: an out-of-range pin is ignored");

        fs::remove_all(dir);
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
