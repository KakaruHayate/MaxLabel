// Language segmentation — see include/maxlabel/language.h.

#include "maxlabel/language.h"

#include "maxlabel/core.h"

#include <cstdint>
#include <utility>

namespace maxlabel {

namespace {

enum class Script { Neutral, Han, Kana, Hangul, Latin, Other };

bool in_range(char32_t c, char32_t lo, char32_t hi) { return c >= lo && c <= hi; }

Script classify(char32_t c) {
    // Whitespace, digits and punctuation glue neighbouring runs together
    // rather than splitting them: "你好, world" is one Chinese run, a comma,
    // then one English run.
    if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r') return Script::Neutral;
    if (c < 0x80) {
        if ((c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z')) {
            return Script::Latin;
        }
        return Script::Neutral;
    }

    // CJK Unified Ideographs + Ext A + Compatibility Ideographs + Ext B and
    // beyond.  All of these are "Han": shared by Chinese, Japanese and Korean.
    if (in_range(c, 0x3400, 0x4DBF) || in_range(c, 0x4E00, 0x9FFF) ||
        in_range(c, 0xF900, 0xFAFF) || in_range(c, 0x20000, 0x3FFFF)) {
        return Script::Han;
    }
    // Hiragana, katakana, katakana phonetic extensions, halfwidth katakana.
    if (in_range(c, 0x3040, 0x309F) || in_range(c, 0x30A0, 0x30FF) ||
        in_range(c, 0x31F0, 0x31FF) || in_range(c, 0xFF66, 0xFF9D)) {
        return Script::Kana;
    }
    // Hangul jamo, compatibility jamo, jamo extended-A, syllables.
    if (in_range(c, 0x1100, 0x11FF) || in_range(c, 0x3130, 0x318F) ||
        in_range(c, 0xA960, 0xA97F) || in_range(c, 0xAC00, 0xD7FF)) {
        return Script::Hangul;
    }
    // Latin-1 supplement, Latin Extended-A/B, Latin Extended Additional,
    // Latin ligatures, fullwidth Latin.
    if (in_range(c, 0x00C0, 0x024F) || in_range(c, 0x1E00, 0x1EFF) ||
        in_range(c, 0xFB00, 0xFB06) || in_range(c, 0xFF21, 0xFF3A) ||
        in_range(c, 0xFF41, 0xFF5A)) {
        return Script::Latin;
    }
    // CJK punctuation, fullwidth forms and general punctuation are neutral:
    // they belong to whichever run they sit in.
    if (in_range(c, 0x3000, 0x303F) || in_range(c, 0xFE30, 0xFE4F) ||
        in_range(c, 0xFF00, 0xFF20) || in_range(c, 0xFF3B, 0xFF40) ||
        in_range(c, 0xFF5B, 0xFF65) || in_range(c, 0x2000, 0x206F)) {
        return Script::Neutral;
    }
    // Anything else (Cyrillic, Greek, ...) is a script we cannot route; it
    // gets its own run and a human decides.
    return Script::Other;
}

// Decodes one code point at `i`; malformed bytes decode as U+FFFD and consume
// one byte, so the walk always terminates.
char32_t decode_utf8(const std::string & s, std::size_t & i) {
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

std::string language_of(Script script, const std::string & default_language, bool & ambiguous) {
    switch (script) {
        case Script::Kana:   ambiguous = false; return "ja";
        case Script::Hangul: ambiguous = false; return "ko";
        case Script::Latin:  ambiguous = false; return "en";
        case Script::Han:
            // The one the script cannot settle.
            ambiguous = default_language.empty();
            return default_language;
        case Script::Other:  ambiguous = true;  return std::string();
        case Script::Neutral: break;
    }
    ambiguous = false;
    return std::string();
}

}  // namespace

std::vector<LangSpan> detect_languages(const std::string & text,
                                       const std::string & default_language) {
    std::vector<LangSpan> spans;

    Script run_script = Script::Neutral;
    std::size_t run_begin = 0;
    bool in_run = false;

    const auto flush = [&](std::size_t end) {
        if (!in_run) return;
        bool ambiguous = false;
        const std::string language = language_of(run_script, default_language, ambiguous);
        // Merge with the previous span when it resolved to the same language,
        // so "hi, there" is one English span rather than three.
        if (!spans.empty() && spans.back().language == language &&
            spans.back().ambiguous == ambiguous && spans.back().end == run_begin) {
            spans.back().end = end;
        } else {
            LangSpan span;
            span.begin     = run_begin;
            span.end       = end;
            span.language  = language;
            span.ambiguous = ambiguous;
            spans.push_back(std::move(span));
        }
        in_run = false;
    };

    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t at = i;
        const Script script = classify(decode_utf8(text, i));
        if (script == Script::Neutral) {
            // Neutral bytes join the open run; a leading neutral run is
            // carried along until something decides its language.
            if (!in_run) {
                run_begin  = at;
                run_script = Script::Neutral;
                in_run     = true;
            }
            continue;
        }
        if (in_run && run_script == script) continue;
        if (in_run && run_script == Script::Neutral) {
            // The pending neutral run adopts the script that follows it.
            run_script = script;
            continue;
        }
        flush(at);
        run_begin  = at;
        run_script = script;
        in_run     = true;
    }
    flush(text.size());

    // Drop a trailing run that is nothing but whitespace.
    while (!spans.empty()) {
        const LangSpan & last = spans.back();
        const std::string tail = text.substr(last.begin, last.end - last.begin);
        bool blank = true;
        for (const char c : tail) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { blank = false; break; }
        }
        if (!blank) break;
        spans.pop_back();
    }
    return spans;
}

bool has_undetermined(const std::vector<LangSpan> & spans) {
    for (const LangSpan & span : spans) {
        if (span.ambiguous || span.language.empty()) return true;
    }
    return false;
}

std::string spans_to_pfml(const std::string & text, const std::vector<LangSpan> & spans) {
    std::string out;
    std::size_t i = 0;
    while (i < spans.size()) {
        const LangSpan & span = spans[i];
        if (span.begin >= span.end || span.end > text.size()) {
            ++i;
            continue;
        }
        if (span.language.empty()) {
            // An undetermined run stays bare text: valid PFML that says "no
            // language here", rather than a scope asserting a wrong one.
            out += escape_text(text.substr(span.begin, span.end - span.begin));
            ++i;
            continue;
        }
        // Absorb the following spans that resolved to the same language, so a
        // run split by the detector does not become two adjacent scopes.
        std::size_t end = span.end;
        std::size_t next = i + 1;
        while (next < spans.size() && spans[next].language == span.language &&
               spans[next].begin == end && spans[next].end > spans[next].begin) {
            end = spans[next].end;
            ++next;
        }
        out += "<scope language=\"";
        out += span.language;
        out += "\">";
        out += escape_text(text.substr(span.begin, end - span.begin));
        out += "</scope>";
        i = next;
    }
    return out;
}

std::string text_to_pfml(const std::string & text, const std::string & default_language) {
    return spans_to_pfml(text, detect_languages(text, default_language));
}

}  // namespace maxlabel
