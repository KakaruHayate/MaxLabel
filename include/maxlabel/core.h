#pragma once

// MaxLabel core: the on-disk project model.
//
// A project is a directory.  Every file of a segment shares its basename —
// song.wav / song.pfml / song.txt / song.lab / song.json — so there is no
// manifest to keep in sync and no project file to corrupt.  The read priority
// is the one the aligner uses (.pfml, then .txt, then .lab); PFML is the only
// file this tool writes.

#include <string>
#include <vector>

namespace maxlabel {

// Which sidecar supplied the segment's text.
enum class TextSource { None, Pfml, Json, Text, Lab };

const char * to_string(TextSource source);

// One editable line.
struct Segment {
    std::string id;          // basename shared by every file of the segment
    std::string audio_path;  // empty when no audio is paired
    std::string pfml_path;
    std::string txt_path;
    std::string lab_path;
    std::string json_path;

    TextSource  source = TextSource::None;
    std::string text;        // the lyric line, for display and editing
    std::string lab;         // the legacy syllable line, when there was one
    bool        reviewed = false;   // MinLabel's json "isCheck"

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

// Parse-only PFML validation.  Throws tifa_ggml::InvalidArgument carrying the
// byte offset when the fragment is malformed.
void validate(const std::string & pfml);

// Escape a plain-text transcript so it can sit inside a PFML fragment.
std::string escape_text(const std::string & text);

// The audio extensions the scanner recognises.
bool is_audio_extension(const std::string & extension);

}  // namespace maxlabel
