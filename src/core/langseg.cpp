// GPT-SoVITS's language segmentation, ported — see include/maxlabel/langseg.h.
//
// The one invariant everything else depends on: the pieces' texts, joined in
// order, are exactly the input.  Offsets into the transcript are how the rest
// of MaxLabel addresses it, so a port that drops or reorders a character would
// silently shift every annotation.

#include "maxlabel/langseg.h"

#include "maxlabel/languages.h"
#include "maxlabel/models.h"
#ifdef MAXLABEL_HAS_FASTTEXT
#include "maxlabel/fasttext_detector.h"
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace maxlabel {

namespace {

// ---------------------------------------------------------------------------
// UTF-8, by code point.  The Python original indexes `str`, i.e. code points;
// slicing on code point boundaries gives the same substrings as byte slicing,
// so bytes are used throughout and the two agree.
// ---------------------------------------------------------------------------

char32_t decode(const std::string & s, std::size_t & i) {
    const auto byte = [&](std::size_t k) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(s[k]));
    };
    const std::uint32_t b0 = byte(i);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    std::size_t extra = 0;
    std::uint32_t value = 0;
    if ((b0 & 0xE0) == 0xC0) { extra = 1; value = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; value = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; value = b0 & 0x07; }
    else { ++i; return 0xFFFD; }

    if (i + extra >= s.size()) {
        ++i;
        return 0xFFFD;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const std::uint32_t b = byte(i + k);
        if ((b & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        value = (value << 6) | (b & 0x3F);
    }
    i += extra + 1;
    return value;
}

std::size_t advance(const std::string & s, std::size_t i) {
    decode(s, i);
    return i;
}

bool in_range(char32_t c, char32_t lo, char32_t hi) { return c >= lo && c <= hi; }

// ---------------------------------------------------------------------------
// The character classes the original uses
// ---------------------------------------------------------------------------

// split_lang/split/utils.py: `contains_ja_kana` is [\u3040-\u30ff々].
bool is_ja_kana(char32_t c) {
    return in_range(c, 0x3040, 0x30FF) || c == 0x3005;
}

// zh_ja_pattern = [\u4e00-\u9fff\u3040-\u30ff々]
bool is_zh_ja(char32_t c) { return in_range(c, 0x4E00, 0x9FFF) || is_ja_kana(c); }

// hangul_pattern = [\uac00-\ud7af]
bool is_hangul(char32_t c) { return in_range(c, 0xAC00, 0xD7AF); }

// Python's str.isdigit() is Unicode-aware.  The common digit blocks are here;
// anything rarer is treated as an ordinary character rather than guessed at.
bool is_digit(char32_t c) {
    return in_range(c, U'0', U'9') || in_range(c, 0xFF10, 0xFF19) ||
           in_range(c, 0x0660, 0x0669) || in_range(c, 0x06F0, 0x06F9);
}

bool is_space(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0x3000 ||
           c == 0x00A0 || in_range(c, 0x2000, 0x200A);
}

bool is_newline(char32_t c) { return c == U'\n' || c == U'\r'; }

// full_cjk's allowed non-CJK characters: [0-9、-〜。！？.!?… /]
bool is_cjk_keep(char32_t c) {
    return is_digit(c) || c == 0x3001 || c == U'-' || c == 0x301C || c == 0x3002 ||
           c == 0xFF01 || c == 0xFF1F || c == U'.' || c == U'!' || c == U'?' ||
           c == 0x2026 || c == U' ' || c == U'/';
}

// full_cjk's CJK ranges, exactly as the original lists them.
bool is_cjk_ideograph(char32_t c) {
    return in_range(c, 0x4E00, 0x9FFF) || in_range(c, 0x3400, 0x4DB5) ||
           in_range(c, 0x20000, 0x2A6DD) || in_range(c, 0x2A700, 0x2B73F) ||
           in_range(c, 0x2B740, 0x2B81F) || in_range(c, 0x2B820, 0x2CEAF) ||
           in_range(c, 0x2CEB0, 0x2EBEF) || in_range(c, 0x30000, 0x3134A) ||
           in_range(c, 0x31350, 0x323AF) || in_range(c, 0x2EBF0, 0x2EE5D);
}

// split_jako's separator class, used inside the ja and ko patterns.
bool is_jako_separator(char32_t c) {
    return is_digit(c) || c == 0x3001 || c == U'-' || c == 0x301C || c == 0x3002 ||
           c == 0xFF01 || c == 0xFF1F || c == U'.' || c == U'!' || c == U'?' ||
           c == 0x2026 || c == U' ';
}

// The ja pattern: [\u3041-\u3096\u3099\u309a\u30a1-\u30fa\u30fc]
bool is_ja_kana_narrow(char32_t c) {
    return in_range(c, 0x3041, 0x3096) || c == 0x3099 || c == 0x309A ||
           in_range(c, 0x30A1, 0x30FA) || c == 0x30FC;
}

// The ko pattern: [\u1100-\u11ff\u3130-\u318f\uac00-\ud7af]
bool is_ko_narrow(char32_t c) {
    return in_range(c, 0x1100, 0x11FF) || in_range(c, 0x3130, 0x318F) ||
           in_range(c, 0xAC00, 0xD7AF);
}

bool contains_ja_kana(const std::string & text) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (is_ja_kana(decode(text, i))) return true;
    }
    return false;
}

// The original tests `text[-1] in ["。", "."]`.
bool ends_with_stop(const std::string & text) {
    if (text.empty()) return false;
    if (text.back() == '.') return true;
    return text.size() >= 3 && text.compare(text.size() - 3, 3, "\xE3\x80\x82") == 0;
}

// The language a detector's answer maps to, or "x" when the table has no home
// for it.  The mapping is data (languages.h), which is what keeps traditional
// Chinese unresolved instead of claimed as Mandarin, and what makes adding a
// language a one-line change.
std::string map_language(const std::string & detected) {
    if (detected.empty() || detected == "x") return "x";
    const LanguageInfo * language = LanguageTable::builtin().for_detector_id(detected);
    return language == nullptr ? "x" : language->id;
}

// What a detector says about a run, mapped onto our own ids; "x" when there is
// no detector, or no home for its answer.
std::string detect(const SegmentationContext & context, const std::string & run) {
    // `full_en` is decided here rather than only in getTexts, because a run it
    // would have claimed must not first be merged into a neighbouring unknown:
    // two unknowns are not known to be the same language.
    if (is_full_en(run)) return "en";
    if (context.detector == nullptr) return "x";
    return map_language(context.detector->detect(run));
}

// The original's `full_cjk` keeps only the CJK characters of a run, which for
// the runs it is applied to (Han sections) is everything.  Applied to a run
// that also holds something else it would silently drop the rest — and the
// pieces have to reconstruct the transcript — so the remainder is kept as an
// unknown run rather than discarded.
std::vector<LangPiece> cjk_and_rest(const LangPiece & piece) {
    std::vector<LangPiece> out;
    std::string current;
    bool current_is_cjk = false;
    const auto flush = [&] {
        if (current.empty()) return;
        out.push_back(LangPiece{ current_is_cjk ? "zh" : "x", current });
        current.clear();
    };

    std::size_t i = 0;
    while (i < piece.text.size()) {
        const std::size_t at = i;
        const char32_t c = decode(piece.text, i);
        const bool keep = is_cjk_ideograph(c) || is_cjk_keep(c);
        if (!current.empty() && keep != current_is_cjk) flush();
        current_is_cjk = keep;
        current += piece.text.substr(at, i - at);
    }
    flush();
    return out;
}

// split-lang's `_parse_zh_ja`: the Japanese parser first, then the Chinese one
// on each of its chunks.  The point is that the detector sees word-sized
// pieces, which is what it is good at, rather than a whole run.
std::vector<std::string> sub_split_zh_ja(const std::string & run,
                                         const SegmentationContext & context) {
    if (context.japanese != nullptr && context.japanese->ready()) {
        std::vector<std::string> chunks = context.japanese->parse(run);
        if (context.chinese != nullptr && context.chinese->ready()) {
            std::vector<std::string> refined;
            for (const std::string & chunk : chunks) {
                const std::vector<std::string> pieces = context.chinese->parse(chunk);
                refined.insert(refined.end(), pieces.begin(), pieces.end());
            }
            chunks = std::move(refined);
        }
        if (!chunks.empty()) return chunks;
    }
    return { run };
}

// append to the previous piece when the language matches, exactly as
// `merge_lang` does — that is what keeps the pieces contiguous.
void merge_piece(std::vector<LangPiece> & out, const LangPiece & piece) {
    if (piece.text.empty()) return;
    if (!out.empty() && out.back().lang == piece.lang) {
        out.back().text += piece.text;
        return;
    }
    out.push_back(piece);
}

}  // namespace

bool is_full_en(const std::string & text) {
    // ^(?=.*[A-Za-z])[A-Za-z0-9\s\u0020-\u007E\u2000-\u206F\u3000-\u303F\uFF00-\uFFEF]+$
    if (text.empty()) return false;
    bool has_letter = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const char32_t c = decode(text, i);
        if (in_range(c, U'A', U'Z') || in_range(c, U'a', U'z')) {
            has_letter = true;
            continue;
        }
        if (is_digit(c) || is_space(c) || in_range(c, 0x20, 0x7E) ||
            in_range(c, 0x2000, 0x206F) || in_range(c, 0x3000, 0x303F) ||
            in_range(c, 0xFF00, 0xFFEF)) {
            continue;
        }
        return false;
    }
    return has_letter;
}

std::string keep_cjk(const std::string & text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t at = i;
        const char32_t c = decode(text, i);
        if (is_cjk_ideograph(c) || is_cjk_keep(c)) out += text.substr(at, i - at);
    }
    return out;
}

std::vector<LangPiece> split_jako(const std::string & tag_lang, const LangPiece & item) {
    // The original's regex is a kana (or hangul) run that may be interrupted by
    // separators and continue; everything outside those runs keeps `item.lang`.
    const bool japanese = tag_lang == "ja";
    const auto is_target = [&](char32_t c) {
        return japanese ? is_ja_kana_narrow(c) : is_ko_narrow(c);
    };

    struct CodePoint {
        std::size_t at;
        std::size_t next;
        char32_t value;
    };
    std::vector<CodePoint> points;
    {
        std::size_t i = 0;
        while (i < item.text.size()) {
            const std::size_t at = i;
            const char32_t value = decode(item.text, i);
            points.push_back(CodePoint{ at, i, value });
        }
    }

    // Byte ranges of the matching runs.
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    std::size_t k = 0;
    while (k < points.size()) {
        if (!is_target(points[k].value)) {
            ++k;
            continue;
        }
        const std::size_t begin = points[k].at;
        std::size_t end = points[k].next;
        std::size_t cursor = k + 1;
        while (cursor < points.size()) {
            // Consecutive target characters always extend the run.
            if (is_target(points[cursor].value)) {
                end = points[cursor].next;
                ++cursor;
                continue;
            }
            // A separator run extends it only if more of the target follows —
            // that is what the `(?:sep+ target*)*` group in the regex means.
            std::size_t probe = cursor;
            while (probe < points.size() && is_jako_separator(points[probe].value)) ++probe;
            if (probe == cursor || probe >= points.size() || !is_target(points[probe].value)) break;
            while (probe < points.size() && is_target(points[probe].value)) {
                end = points[probe].next;
                ++probe;
            }
            cursor = probe;
        }
        runs.push_back({ begin, end });
        while (k < points.size() && points[k].at < end) ++k;
    }

    std::vector<LangPiece> out;
    std::size_t tag = 0;
    for (const std::pair<std::size_t, std::size_t> & run : runs) {
        if (run.first > tag) {
            out.push_back(LangPiece{ item.lang, item.text.substr(tag, run.first - tag) });
        }
        out.push_back(LangPiece{ tag_lang, item.text.substr(run.first, run.second - run.first) });
        tag = run.second;
    }
    if (tag < item.text.size()) {
        out.push_back(LangPiece{ item.lang, item.text.substr(tag) });
    }
    return out;
}

std::string ScriptDetector::detect(const std::string & text) const {
    // Kana and hangul are settled by the script itself; everything else this
    // detector declines, leaving the pipeline's own chains to decide.
    if (contains_ja_kana(text)) return "ja";
    std::size_t i = 0;
    bool saw_hangul = false;
    while (i < text.size()) {
        if (is_hangul(decode(text, i))) saw_hangul = true;
    }
    if (saw_hangul) return "ko";
    if (is_full_en(text)) return "en";
    return "x";
}

std::vector<LangPiece> segment_languages(const std::string & text,
                                         const std::string & default_lang,
                                         const SegmentationContext & context) {
    if (text.empty()) return {};

    // --- pre-split, then label each run -------------------------------------
    // split-lang groups Chinese and Japanese together (ZH_JA) because they
    // share Han characters, and decides the whole run by whether it contains
    // kana.  Korean and everything else stand alone.
    std::vector<LangPiece> pieces;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t at = i;
        const char32_t first = decode(text, i);
        std::size_t end = i;
        while (end < text.size()) {
            std::size_t probe = end;
            const char32_t c = decode(text, probe);
            if (is_zh_ja(c) != is_zh_ja(first)) break;
            if (is_newline(c) != is_newline(first)) break;
            if (is_space(c) != is_space(first)) break;
            if (is_digit(c) != is_digit(first)) break;
            end = probe;
        }
        if (end <= at) end = i;   // always make progress
        const std::string run = text.substr(at, end - at);

        std::string lang;
        if (is_zh_ja(first)) {
            if (contains_ja_kana(run)) {
                // split-lang decides a zh/ja section by whether kana appears in
                // it, so a run containing kana is Japanese whole.  (Its own
                // kana re-merge pass exists to reach this same answer after
                // sub-splitting; deciding the run directly is the same result
                // without the round trip.)
                pieces.push_back(LangPiece{ "ja", run });
            } else {
                // Pure Han: sub-split so the detector sees words, then let it
                // answer for each.  Without a detector every piece falls to the
                // unknown chain, which is where the Chinese assumption lives.
                for (const std::string & chunk : sub_split_zh_ja(run, context)) {
                    pieces.push_back(LangPiece{ detect(context, chunk), chunk });
                }
            }
        } else {
            if (is_hangul(first)) lang = "ko";
            else if (is_digit(first)) lang = "digit";
            else if (is_space(first) || is_newline(first)) lang = "";  // neutral
            else lang = detect(context, run);
            pieces.push_back(LangPiece{ lang, run });
        }
        i = end;
    }

    // Neutral runs (spaces, newlines) belong to whichever piece they sit in:
    // a leading one goes to the following piece, the rest to the previous.
    std::vector<LangPiece> merged;
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        if (!pieces[k].lang.empty()) {
            merge_piece(merged, pieces[k]);
            continue;
        }
        if (merged.empty()) {
            // Keep it in front of the next piece rather than lose it.
            std::size_t next = k + 1;
            while (next < pieces.size() && pieces[next].lang.empty()) ++next;
            if (next < pieces.size()) {
                pieces[next].text = pieces[k].text + pieces[next].text;
            } else {
                merged.push_back(pieces[k]);
            }
            continue;
        }
        merged.back().text += pieces[k].text;
    }

#ifdef MAXLABEL_DEBUG_SEGMENT
    for (const LangPiece & p : pieces)
        std::fprintf(stderr, "  pre    [%s] %s\n", p.lang.c_str(), p.text.c_str());
    for (const LangPiece & p : merged)
        std::fprintf(stderr, "  merged [%s] %s\n", p.lang.c_str(), p.text.c_str());
#endif

    // --- GPT-SoVITS's getTexts ---------------------------------------------
    std::vector<LangPiece> lang_list;
    bool have_num = false;

    for (const LangPiece & piece : merged) {
        LangPiece item = piece;

        if (item.lang == "digit") {
            if (!default_lang.empty()) item.lang = default_lang;
            else have_num = true;
            merge_piece(lang_list, item);
            continue;
        }

        // Short English misdetected as something else.
        if (is_full_en(item.text)) {
            item.lang = "en";
            merge_piece(lang_list, item);
            continue;
        }

        if (!default_lang.empty()) {
            item.lang = default_lang;
            merge_piece(lang_list, item);
            continue;
        }

        // Kana inside a run labelled otherwise, then hangul inside what is left.
        std::vector<LangPiece> ja_list;
        if (item.lang != "ja") ja_list = split_jako("ja", item);
        if (ja_list.empty()) ja_list.push_back(item);

        std::vector<LangPiece> temp_list;
        for (const LangPiece & ja_item : ja_list) {
            std::vector<LangPiece> ko_list;
            if (ja_item.lang != "ko") ko_list = split_jako("ko", ja_item);
            if (ko_list.empty()) temp_list.push_back(ja_item);
            else temp_list.insert(temp_list.end(), ko_list.begin(), ko_list.end());
        }

        for (const LangPiece & temp_item : temp_list) {
            if (temp_item.lang != "x") {
                merge_piece(lang_list, temp_item);
                continue;
            }
            // Unknown: if it is Han, it is Chinese — that is the original's
            // answer, and the reason a Chinese dataset needs no manual work.
            if (keep_cjk(temp_item.text).empty()) {
                merge_piece(lang_list, temp_item);
                continue;
            }
            for (const LangPiece & part : cjk_and_rest(temp_item)) {
                merge_piece(lang_list, part);
            }
        }
    }

    // --- digits, then any remaining unknowns --------------------------------
    if (have_num) {
        std::vector<LangPiece> resolved;
        const std::vector<LangPiece> source = lang_list;
        for (std::size_t k = 0; k < source.size(); ++k) {
            LangPiece item = source[k];
            if (item.lang == "digit") {
                const bool has_prev = !resolved.empty();
                const bool has_next = k + 1 < source.size();
                if (!default_lang.empty()) item.lang = default_lang;
                else if (has_prev && !has_next) item.lang = resolved.back().lang;
                else if (!has_prev && has_next) item.lang = source[1].lang;
                else if (has_prev && has_next) {
                    const std::string & prev = resolved.back().lang;
                    const std::string & next = source[k + 1].lang;
                    const std::string & prev_text = resolved.back().text;
                    const std::string & next_text = source[k + 1].text;
                    const std::string punct = ",.!?，。！？";
                    if (prev == next) item.lang = prev;
                    else if (!prev_text.empty() &&
                             punct.find(prev_text.back()) != std::string::npos) {
                        item.lang = next;
                    } else if (!next_text.empty() && punct.find(next_text.front()) != std::string::npos) {
                        item.lang = prev;
                    } else if (ends_with_stop(item.text)) {
                        item.lang = prev;
                    } else if (prev_text.size() >= next_text.size()) {
                        item.lang = prev;
                    } else {
                        item.lang = next;
                    }
                } else {
                    item.lang = "zh";
                }
            }
            merge_piece(resolved, item);
        }
        lang_list = std::move(resolved);
    }

    {
        std::vector<LangPiece> resolved;
        const std::vector<LangPiece> source = lang_list;
        for (std::size_t k = 0; k < source.size(); ++k) {
            LangPiece item = source[k];
            if (item.lang == "x") {
                if (!resolved.empty()) item.lang = resolved.back().lang;
                else if (source.size() > 1) item.lang = source[1].lang;
                else item.lang = "zh";
            }
            merge_piece(resolved, item);
        }
        lang_list = std::move(resolved);
    }

    // The pieces must add up to the input.  Every annotation in MaxLabel is
    // addressed by offset into the transcript, so a port that drops or
    // reorders a character would shift all of them; refuse rather than return
    // something that does not reconstruct what it was given.
    {
        std::string joined;
        for (const LangPiece & piece : lang_list) joined += piece.text;
        if (joined != text) {
#if 1
            std::fprintf(stderr, "segment: reconstruction failed for [%s]\n", text.c_str());
            for (const LangPiece & piece : lang_list) {
                std::fprintf(stderr, "  [%s] %s\n", piece.lang.c_str(), piece.text.c_str());
            }
#endif
            return { LangPiece{ std::string(), text } };
        }
    }

    return lang_list;
}

const SegmentationContext & default_segmentation_context() {
    // Loaded once, on first use.  Anything missing degrades rather than fails:
    // no detector leaves the unknown chain to answer (which reads as Chinese),
    // and no BudouX models means a run is not sub-split — coarser, not broken.
    static const ScriptDetector script;
#ifdef MAXLABEL_HAS_FASTTEXT
    static const FastTextDetector fasttext = [] {
        FastTextDetector detector;
        // split-lang asks for "full"; prefer it when both are present, and
        // fall back to the compressed one rather than losing detection.
        if (!detector.load(model_directory() + "/lid.176.bin")) {
            detector.load(model_directory() + "/lid.176.ftz");
        }
        return detector;
    }();
#endif
    static const BudouX japanese = [] {
        BudouX parser;
        parser.load(model_directory() + "/budoux/ja.json");
        return parser;
    }();
    static const BudouX chinese = [] {
        BudouX parser;
        parser.load(model_directory() + "/budoux/zh-hans.json");
        return parser;
    }();

    static const LangDetector * detector = &script;
#ifdef MAXLABEL_HAS_FASTTEXT
    if (fasttext.ready()) detector = &fasttext;
#endif
    static const SegmentationContext context{ detector, &japanese, &chinese };
    return context;
}

}  // namespace maxlabel
