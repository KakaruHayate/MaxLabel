#pragma once

// Language segmentation: splitting a lyric line into language spans.
//
// This is the piece the plan calls the tool's reason to exist.  The aligner
// routes a span to a converter by its language, and when the language is wrong
// the phonemes are wrong — silently, because nothing downstream knows the
// transcript meant something else.
//
// Two of the three decisions are settled by the script alone:
//
//   kana   -> ja        (Chinese does not write kana)
//   hangul -> ko
//   latin  -> en
//
// Han is the one the script cannot settle: 東京 is dōngjīng or tōkyō, 唔该 is
// Mandarin or Cantonese, and all of them are written with the same characters.
// Rather than guess, a Han run takes the caller's default language when there
// is one, and is otherwise reported as undetermined so a human decides.  A
// wrong guess here is worse than no guess: the aligner would happily convert
// it with the wrong language and produce plausible, wrong output.

#include <cstddef>
#include <string>
#include <vector>

namespace maxlabel {

struct LangSpan {
    std::size_t begin = 0;      // byte offsets into the segment text
    std::size_t end   = 0;
    std::string language;       // "zh" / "ja" / "en" / "ko"; "" = undetermined
    bool        manual = false;     // set by the author; re-splitting must keep it
    bool        ambiguous = false;  // the script could not decide this run
};

// Split `text` into language spans.
//
// `default_language` ("" when the project has none) is what a Han run falls
// back to — the common case is a Chinese dataset, where every Han run is
// Chinese, but a pure-kanji Japanese line looks identical, so the caller's
// answer is the only honest source for it.
std::vector<LangSpan> detect_languages(const std::string & text,
                                       const std::string & default_language = std::string());

// True when any span is undetermined, i.e. the fragment needs a human decision
// before it can be trusted.
bool has_undetermined(const std::vector<LangSpan> & spans);

// A fixed word boundary: the run of text the aligner must treat as one word.
// The aligner otherwise segments by its own dictionary, and this is how a
// human overrides that.
struct WordBoundary {
    std::size_t begin = 0;
    std::size_t end   = 0;
};

// Serialize text + spans + word boundaries into a PFML fragment: a run of one
// language becomes <scope language="...">…</scope>, a fixed word becomes
// <word>…</word> inside it, and an undetermined run stays bare text.
//
// PFML nests <word> inside <scope> and forbids the reverse, so a word boundary
// that crosses a language boundary cannot be expressed.  Such a word is
// reported through `error` (when non-null) and emitted as plain text rather
// than silently split or dropped.
std::string annotate_to_pfml(const std::string & text,
                             const std::vector<LangSpan> & spans,
                             const std::vector<WordBoundary> & words,
                             std::string * error = nullptr);

// Serialize text + spans only.
std::string spans_to_pfml(const std::string & text, const std::vector<LangSpan> & spans);

// Convenience: detect, then serialize.
std::string text_to_pfml(const std::string & text,
                         const std::string & default_language = std::string());

}  // namespace maxlabel
