#pragma once

// MaxLabel core: the on-disk project model.
//
// A project is a directory.  Every file of a segment shares its basename —
// song.wav / song.pfml / song.txt / song.lab / song.json — so there is no
// manifest to keep in sync and no project file to corrupt.  The read priority
// is the one the aligner uses (.pfml, then .txt, then .lab); PFML is the only
// file this tool writes.

#include "maxlabel/annotate.h"

#include <string>
#include <vector>

namespace maxlabel {

// Which sidecar supplied the segment's text.
enum class TextSource { None, Pfml, Json, Text, Lab };

const char * to_string(TextSource source);

// One editable line.
struct Segment {
    std::string id;          // basename shared by every file of the segment
    // Where the segment lives.  Not derived from audio_path: a segment with no
    // audio has none, and deriving it from one that does would put a saved file
    // wherever the process happens to be running.
    std::string directory;
    std::string audio_path;  // empty when no audio is paired
    std::string pfml_path;
    std::string txt_path;
    std::string lab_path;
    std::string json_path;

    TextSource  source = TextSource::None;
    std::string text;        // the lyric line, for display and editing
    std::string lab;         // the legacy syllable line, when there was one
    bool        reviewed = false;   // MinLabel's json "isCheck"

    // What a Han run falls back to.  Empty means "undetermined", which is the
    // honest answer for a pure-kanji line: see language.h.
    std::string default_language;

    // Language spans over `text`.  Empty for a .pfml source, whose language is
    // already written into the fragment.
    std::vector<LangSpan> spans;

    // Word boundaries the author fixed by hand; the aligner otherwise segments
    // by its own dictionary.
    std::vector<WordBoundary> words;

    // Final phonemes the author wrote, where the dictionary is wrong or the
    // sound is not a word at all.  Covers picking a reading, writing the
    // phonemes outright, and inserting a discrete sound.
    std::vector<Override> overrides;

    // The editable content.  Empty when the segment has no text yet (audio
    // only, waiting to be typed in).
    std::string pfml;

    // Set by load(): whether `pfml` parses.  A fragment that does not parse is
    // a sample the aligner would skip silently, so this is worth surfacing
    // before anything else.
    bool        pfml_valid = true;
    std::string error;
};

struct Project {
    std::string            directory;
    std::vector<Segment>   segments;
};

// Scan a directory (non-recursive).  Any audio file, or any sidecar without
// audio, becomes a segment.
Project scan(const std::string & directory);

// Read a segment's sidecars and fill in text / lab / pfml / pfml_valid.
void load(Segment & segment);

// Validate `segment.pfml` and write it to <id>.pfml.  Throws
// tifa_ggml::InvalidArgument when the fragment does not parse.
void save(const Segment & segment);

// (Re)detect `segment.spans` from `text` + `default_language`.  This discards
// any manual spans, so it is what a "re-split" action calls.
void detect_spans(Segment & segment);

// Pin the language of the run covering [begin, end) and regenerate the PFML.
// This is the manual override the whole segmentation design exists to feed:
// the automatic pass decides kana/hangul/latin, a human decides the Han.
// Out-of-range or empty ranges are ignored.
void set_span_language(Segment & segment, std::size_t begin, std::size_t end,
                       const std::string & language);

// Regenerate `segment.pfml` from `text` + `spans`.  A no-op for a segment with
// no spans (a .pfml source keeps the fragment it was loaded from).
void rebuild_pfml(Segment & segment);

// Fix the word boundary over [begin, end) — the manual segmentation the
// aligner's dictionary would otherwise decide.  Out-of-range or empty ranges
// are ignored.  An existing boundary that overlaps is replaced.
void add_word(Segment & segment, std::size_t begin, std::size_t end);

// Drop every word boundary touching `position` (either edge counts), so the
// same selection toggles a boundary off again.
void remove_word_at(Segment & segment, std::size_t position);

void clear_words(Segment & segment);

// Pin the final phonemes for the run [begin, end) — picking a reading the
// dictionary got wrong, or writing the phonemes outright.  An existing
// override over the same range is replaced.
void set_override(Segment & segment, std::size_t begin, std::size_t end,
                  const std::string & language, const std::string & script,
                  const std::vector<std::string> & phonemes);

// Insert a discrete sound at `position` — a nasal pad the singer added, or a
// breath.  These are not words, so there is no text range to pin them to.
void insert_phoneme(Segment & segment, std::size_t position,
                    const std::vector<std::string> & phonemes);

// Drop every override touching `position`, and every override covering the
// run [begin, end) when `begin < end`.  The same selection toggles off again.
void remove_override_at(Segment & segment, std::size_t position);
void remove_overrides_in(Segment & segment, std::size_t begin, std::size_t end);

void clear_overrides(Segment & segment);

// The language a byte offset falls in, or "" when nothing decides it.
std::string language_at(const Segment & segment, std::size_t position);

// Parse-only PFML validation.  Throws tifa_ggml::InvalidArgument carrying the
// byte offset when the fragment is malformed.
void validate(const std::string & pfml);

// Escape a plain-text transcript so it can sit inside a PFML fragment.
std::string escape_text(const std::string & text);

// The audio extensions the scanner recognises.
bool is_audio_extension(const std::string & extension);

}  // namespace maxlabel
