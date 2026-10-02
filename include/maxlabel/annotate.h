#pragma once

// Annotations over a lyric line, and their PFML spelling.
//
// Three independent layers sit on the same text:
//
//   language spans   which converter the aligner should route the run to
//   word boundaries  where a word starts and ends, against the dictionary
//   overrides        the final phonemes, when the dictionary is wrong or the
//                    sound is not a word at all (a nasal pad, a breath)
//
// They are separate because they answer different questions and are decided by
// different means — the script settles most of the first, a human settles the
// rest, and only the last one is about the sounds themselves.
//
// PFML nests <word> inside <scope> and forbids the reverse, so anything that
// crosses a language boundary has no spelling at all.  That is reported rather
// than silently split: a transcript that quietly disagrees with what the
// author marked is worse than one that says it cannot express it.

#include "maxlabel/language.h"

#include <cstddef>
#include <string>
#include <vector>

namespace maxlabel {

// A fixed word boundary: the run the aligner must treat as one word.
struct WordBoundary {
    std::size_t begin = 0;
    std::size_t end   = 0;
};

// Final phonemes for a run of text, replacing whatever the dictionary would
// have produced.
//
// This covers all three of the remaining editing needs:
//   - pick a reading (重 as chong rather than zhong): a range with phonemes
//   - write the phonemes outright: the same thing, typed by hand
//   - insert a discrete sound (a nasal pad, AP/SP): an empty range at the
//     position, which serializes as <phoneme> instead of <word>
struct Override {
    std::size_t begin = 0;   // begin == end means "insert at this position"
    std::size_t end   = 0;
    std::string language;    // "" = inherit the enclosing span's language
    std::string script;      // the pronunciation label (pinyin, romaji, ...)
    std::vector<std::string> phonemes;

    bool inserts() const { return begin == end; }
    bool empty() const { return phonemes.empty(); }
};

// Serialize text + annotations into a PFML fragment.
//
// A run of one language becomes <scope language="...">…</scope>; inside it a
// fixed word becomes <word>…</word>, an override becomes
// <word text="..." phonemes="..."/> and an insert becomes <phoneme .../>.  An
// undetermined run stays bare text.
//
// Anything that cannot be expressed — a word or override spanning two language
// runs — is reported through `error` (when non-null) and left as plain text.
std::string annotate_to_pfml(const std::string & text,
                             const std::vector<LangSpan> & spans,
                             const std::vector<WordBoundary> & words,
                             const std::vector<Override> & overrides = {},
                             std::string * error = nullptr);

// Serialize text + language spans only.
std::string spans_to_pfml(const std::string & text, const std::vector<LangSpan> & spans);

// Convenience: detect the languages of `text`, then serialize it.
std::string text_to_pfml(const std::string & text,
                         const std::string & default_language = std::string());

}  // namespace maxlabel
