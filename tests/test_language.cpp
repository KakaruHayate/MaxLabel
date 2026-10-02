// Self-checking test for language segmentation (the GPT-SoVITS pipeline).

#include "maxlabel/core.h"
#include "maxlabel/languages.h"
#include "maxlabel/langseg.h"
#include "maxlabel/language.h"
#include "maxlabel/models.h"
#ifdef MAXLABEL_HAS_FASTTEXT
#include "maxlabel/fasttext_detector.h"
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using maxlabel::LangSpan;

// The pipeline with the script detector and no BudouX models.  Deterministic,
// which is what makes it testable: what a fastText model answers is a property
// of the model rather than of this code, and is asserted separately below.
static std::vector<LangSpan> segment_script_only(const std::string & text,
                                                 const std::string & default_language) {
    static const maxlabel::ScriptDetector detector;
    static const maxlabel::SegmentationContext context{ &detector, nullptr, nullptr };
    return maxlabel::detect_languages(text, default_language, context);
}

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

// The span languages joined by ",", with "" for an undetermined run.  Indexed
// rather than guarded on emptiness, so a leading undetermined run is not
// swallowed.
static std::string langs(const std::vector<LangSpan> & spans) {
    std::string out;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (i != 0) out += ",";
        out += spans[i].language;
    }
    return out;
}

// The invariant the whole design rests on: the spans tile the text exactly.
static bool tiles(const std::string & text, const std::vector<LangSpan> & spans) {
    std::size_t at = 0;
    for (const LangSpan & span : spans) {
        if (span.begin != at || span.end <= span.begin || span.end > text.size()) return false;
        at = span.end;
    }
    return at == text.size();
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
    // Where the real data files are, captured before they are taken out of
    // reach below.
    const std::string data_dir = maxlabel::model_directory();

    // The segmentation tests are about this code, and this code's answers must
    // not depend on which model happens to be sitting next to the binary.
    // They did: CI downloads the 916 KB lite detector and the release job
    // downloads the 125 MB full one, and the two disagree about a short Han
    // run — so an exact span count passed in CI and failed on all three
    // platforms in the release.  A test whose result depends on the contents
    // of a directory is not a test.
    //
    // So the default context — the one `scan` builds a segment with — is
    // pointed at an empty directory: script detection only, nothing to
    // download, the same answer everywhere.  What a detector adds is asserted
    // on its own below, against whichever model is actually present.
    maxlabel::set_model_directory(
        (fs::temp_directory_path() / "maxlabel_no_models").string());

    // --- the language table -------------------------------------------------
    {
        const maxlabel::LanguageTable & table = maxlabel::LanguageTable::builtin();
        check(table.knows("zh") && table.knows("ja") && table.knows("en") &&
                  table.knows("ko") && table.knows("yue"),
              "table: the five languages are known");
        check(!table.knows("de"), "table: an unknown language is not routable");

        // The mapping is data, and this is what it buys: adding a language is
        // an entry, not a branch.
        check(table.for_detector_id("zh-cn") != nullptr &&
                  table.for_detector_id("zh-cn")->id == "zh",
              "table: a detector variant maps to its language");
        check(table.for_detector_id("yue") != nullptr &&
                  table.for_detector_id("yue")->id == "zh",
              "table: the source pipeline folds Cantonese into Chinese");
        check(table.for_detector_id("zh-tw") == nullptr,
              "table: traditional Chinese has no home, so it stays unknown");
        check(table.for_detector_id("de") == nullptr,
              "table: a language we do not route stays unknown");

        // Shortcut keys are unique, or two languages would fight over one.
        std::string keys;
        for (const maxlabel::LanguageInfo & language : table.all()) {
            if (language.shortcut == '\0') continue;
            check(keys.find(language.shortcut) == std::string::npos,
                  std::string("table: shortcut '") + language.shortcut + "' is unique");
            keys += language.shortcut;
        }

        // Extending it works, and does not disturb the built-in one.
        maxlabel::LanguageTable extended;
        extended.add(maxlabel::LanguageInfo{ "de", "German", { "de", "deu" }, '6' });
        check(extended.for_detector_id("deu") != nullptr, "table: an added language maps");
        check(maxlabel::LanguageTable::builtin().for_detector_id("de") == nullptr,
              "table: the built-in table is unchanged by a copy");
    }

    // --- the pieces the pipeline is built from ------------------------------
    check(maxlabel::is_full_en("hello world"), "full_en: plain english");
    check(maxlabel::is_full_en("I"), "full_en: a single letter counts");
    check(!maxlabel::is_full_en("你好"), "full_en: han is not english");
    check(!maxlabel::is_full_en("123"), "full_en: digits alone are not english");

    // keep_cjk drops non-CJK letters, which is how an unknown run becomes
    // Chinese in the original.
    check(maxlabel::keep_cjk("hello中文") == "中文", "keep_cjk: latin dropped");
    check(maxlabel::keep_cjk("9.15") == "9.15", "keep_cjk: digits and stops kept");
    check(maxlabel::keep_cjk("hello") == "", "keep_cjk: nothing CJK");

    // split_jako pulls kana out of a run labelled otherwise, and keeps the
    // pieces adding up to the input.
    {
        const maxlabel::LangPiece item{ "zh", "abcあいうdef" };
        const std::vector<maxlabel::LangPiece> parts = maxlabel::split_jako("ja", item);
        std::string joined;
        std::string sequence;
        for (const maxlabel::LangPiece & part : parts) {
            joined += part.text;
            if (!sequence.empty()) sequence += ",";
            sequence += part.lang;
        }
        check(joined == item.text, "split_jako: pieces reconstruct the input");
        check(sequence == "zh,ja,zh", "split_jako: kana lifted out with its neighbours");
    }

    // --- script that settles itself -----------------------------------------
    check(langs(segment_script_only("안녕", "")) == "ko", "hangul -> ko");
    check(langs(segment_script_only("hello", "")) == "en", "latin -> en");

    // A run with kana is Japanese: split-lang groups Han and kana into one
    // section and decides the whole thing by whether kana is present.  This is
    // the case that made the previous script-by-script split useless.
    {
        const std::vector<LangSpan> spans = segment_script_only("東京へ行く", "");
        check(langs(spans) == "ja", "kanji + kana -> the whole run is ja");
        check(!maxlabel::has_undetermined(spans), "kanji + kana -> decided, nothing to review");
        check(tiles("東京へ行く", spans), "kanji + kana -> spans tile the text");
    }

    // Pure Han the detector cannot name: the unknown chain sends it to Chinese,
    // which is what makes a Chinese dataset need no manual work.  It is a
    // deliberate assumption, and it is what the model would replace with a real
    // answer.
    {
        const std::vector<LangSpan> spans = segment_script_only("東京", "");
        check(langs(spans) == "zh", "pure han, no detector -> zh via the unknown chain");
        check(!maxlabel::has_undetermined(spans), "pure han -> no longer flagged for review");
    }
    check(langs(segment_script_only("你好世界", "zh")) == "zh", "han with -l zh -> zh");

    // The common case the user called out: Chinese with an English phrase.
    {
        const std::string text = "今天天气不错 I love you";
        const std::vector<LangSpan> spans = segment_script_only(text, "zh");
        check(langs(spans) == "zh,en", "chinese + english -> zh then en");
        check(tiles(text, spans), "chinese + english -> spans tile the text");
        check(!maxlabel::has_undetermined(spans), "chinese + english -> fully determined");
    }

    // Digits are resolved against their neighbours, as the original does.
    {
        const std::string text = "衬衫的价格是9.15便士";
        const std::vector<LangSpan> spans = segment_script_only(text, "");
        check(langs(spans) == "zh", "digits inside han resolve to zh and merge");
        check(tiles(text, spans), "digits -> spans tile the text");
    }

    // -l forces everything but English, which is the "I know this dataset" case.
    {
        const std::string text = "今天天气不错 I love you";
        const std::vector<LangSpan> spans = segment_script_only(text, "yue");
        check(langs(spans) == "yue,en", "-l yue -> the han run is yue, english stays english");
    }

    // --- serialization ------------------------------------------------------
    {
        const std::string pfml = maxlabel::text_to_pfml("今天天气不错 I love you", "zh");
        check(pfml.find("<scope language=\"zh\">") != std::string::npos, "pfml: zh scope");
        check(pfml.find("<scope language=\"en\">") != std::string::npos, "pfml: en scope");
        check(parses(pfml), "pfml: parses");
    }
    {
        const std::string pfml = maxlabel::text_to_pfml("<hi>", "");
        check(pfml.find("&lt;hi>") != std::string::npos, "pfml: markup in the text is escaped");
        check(parses(pfml), "pfml: escaped text parses");
    }

    // --- with a detector model, when one is installed -----------------------
    // The model is the only thing that can answer for a Han-only run; without
    // it the unknown chain reads as Chinese.  That difference is the whole
    // reason to ship one, so it is the thing worth asserting.
#ifdef MAXLABEL_HAS_FASTTEXT
    {
        // Whichever detector the build environment has: the release ships the
        // full model and CI keeps the lite one, and both have to answer this.
        maxlabel::FastTextDetector detector;
        std::string why;
        const bool loaded = detector.load(data_dir + "/lid.176.bin", &why) ||
                            detector.load(data_dir + "/lid.176.ftz", &why);
        if (loaded) {
            const maxlabel::SegmentationContext context{ &detector, nullptr, nullptr };
            const std::vector<LangSpan> spans =
                maxlabel::detect_languages("東京", "", context);
            // Not "says ja" — which of zh/ja a bare 東京 is belongs to the
            // model, and the two shipped models need not agree.  What matters
            // is that a detector means it gets answered at all rather than
            // being left undetermined for a person to settle.
            bool decided = !spans.empty();
            for (const LangSpan & span : spans) {
                if (span.language.empty()) decided = false;
            }
            check(decided,
                  "fastText: a kanji-only run is answered by the model, not left undetermined");
            check(tiles("東京", spans), "fastText: the spans tile the text");

            // And it must not disturb what the script already settles.
            const std::vector<LangSpan> kana =
                maxlabel::detect_languages("東京へ行く", "", context);
            check(langs(kana) == "ja", "fastText: a run with kana is still ja");
        } else {
            std::cout << "skip: no detector model (" << why << ")\n";
        }
    }
#endif

    // --- segment level ------------------------------------------------------
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
        check(!maxlabel::has_undetermined(segment.spans),
              "segment: nothing to review without a default language");
        check(segment.pfml.find("<scope language=\"en\">") != std::string::npos,
              "segment: pfml generated from the spans");
        check(parses(segment.pfml), "segment: generated pfml parses");

        // A manual pin survives as `manual`, and re-splitting clears it.
        const LangSpan han = segment.spans.front();
        maxlabel::set_span_language(segment, han.begin, han.end, "yue");
        check(segment.spans.front().language == "yue" && segment.spans.front().manual,
              "segment: manual override pinned and flagged");
        check(segment.pfml.find("<scope language=\"yue\">") != std::string::npos,
              "segment: pfml regenerated with the pinned language");

        maxlabel::detect_spans(segment);
        check(!segment.spans.front().manual, "segment: re-splitting discards manual spans");

        const std::size_t before = segment.spans.size();
        maxlabel::set_span_language(segment, 0, 100000, "zh");
        check(segment.spans.size() == before, "segment: an out-of-range pin is ignored");

        fs::remove_all(dir);
    }

    // --- word boundaries and overrides --------------------------------------
    {
        maxlabel::Segment segment;
        segment.text = "重来 I love you";
        segment.default_language = "zh";
        maxlabel::detect_spans(segment);
        check(tiles(segment.text, segment.spans), "segment: spans tile the text");

        maxlabel::add_word(segment, 0, 6);   // 重来
        check(segment.pfml.find("<word>重来</word>") != std::string::npos,
              "word: nested inside the scope");
        check(parses(segment.pfml), "word: generated pfml parses");

        maxlabel::set_override(segment, 0, 3, "zh", "chong", { "ch", "ong" });
        check(segment.pfml.find("<word text=\"重\" language=\"zh\" script=\"chong\" "
                                "phonemes=\"ch ong\"/>") != std::string::npos,
              "override: the pinned reading is written out");

        maxlabel::insert_phoneme(segment, 6, { "n" });
        check(segment.pfml.find("<phoneme symbol=\"n\"/>") != std::string::npos,
              "override: an inserted sound becomes <phoneme>");
        check(parses(segment.pfml), "override: pfml with an insert parses");

        maxlabel::clear_overrides(segment);
        check(segment.pfml.find("<phoneme") == std::string::npos, "override: all gone");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
