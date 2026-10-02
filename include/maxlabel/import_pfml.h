#pragma once

// Reading a PFML fragment back into the model the editor edits.
//
// The editor edits text with annotations on it, and writes PFML; so opening an
// existing .pfml has to go the other way.  Showing the markup in the text pane
// instead would not merely look wrong — the annotations would be empty, the
// next keystroke would re-derive language spans from the markup itself, and the
// saved fragment would be garbage.  A .pfml the tool wrote has to survive being
// opened and edited.
//
// Everything MaxLabel emits is reconstructible: a scope is a language span, a
// word is a boundary, a word with phonemes is an override, and a bare phoneme
// is an insertion at the offset it sat at.  Anything the tool did not emit
// (reading/path trees, comments) is reported rather than half-understood.

#include "maxlabel/annotate.h"
#include "maxlabel/language.h"

#include <string>
#include <vector>

namespace maxlabel {

struct ImportedFragment {
    std::string text;                   // the lyric, markup stripped
    std::vector<LangSpan> spans;        // from <scope>
    std::vector<WordBoundary> words;    // from <word> without phonemes
    std::vector<Override> overrides;    // from <word phonemes> and <phoneme>

    // Empty when the fragment was understood.  When it is not, the caller keeps
    // the fragment as it stands rather than replacing it with a half-reading.
    std::string error;
};

ImportedFragment import_pfml(const std::string & pfml);

}  // namespace maxlabel
