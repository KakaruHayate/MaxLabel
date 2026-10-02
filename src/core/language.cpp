// Language spans over a lyric line.
//
// The detection itself follows GPT-SoVITS's text frontend and lives in
// langseg.cpp; this file turns its pieces into the byte-offset spans the rest
// of MaxLabel annotates against.

#include "maxlabel/language.h"

#include "maxlabel/langseg.h"

#include <string>
#include <utility>
#include <vector>

namespace maxlabel {

namespace {

// The languages MaxLabel can route.  Anything else is reported as undetermined
// rather than asserted — the aligner has no converter for it either way, and
// guessing would only produce plausible wrong phonemes.
bool is_routable(const std::string & language) {
    return language == "zh" || language == "yue" || language == "ja" ||
           language == "ko" || language == "en";
}

}  // namespace

std::vector<LangSpan> detect_languages(const std::string & text,
                                       const std::string & default_language) {
    // The script fallback: it settles kana and hangul on its own and defers
    // everything else to the pipeline's own chains.  Swapping in a fastText
    // detector here is the whole of what the model changes.
    static const ScriptDetector detector;

    std::vector<LangSpan> spans;
    std::size_t at = 0;
    for (const LangPiece & piece : segment_languages(text, default_language, detector)) {
        if (piece.text.empty()) continue;

        LangSpan span;
        span.begin = at;
        span.end = at + piece.text.size();
        at = span.end;

        const bool routable = is_routable(piece.lang);
        span.language = routable ? piece.lang : std::string();
        span.ambiguous = !routable;

        // The pipeline already merges same-language neighbours, but a caller
        // that swaps in a different detector should not have to care.
        if (!spans.empty() && spans.back().language == span.language &&
            spans.back().ambiguous == span.ambiguous && spans.back().end == span.begin) {
            spans.back().end = span.end;
            continue;
        }
        spans.push_back(std::move(span));
    }
    return spans;
}

bool has_undetermined(const std::vector<LangSpan> & spans) {
    for (const LangSpan & span : spans) {
        if (span.ambiguous || span.language.empty()) return true;
    }
    return false;
}

}  // namespace maxlabel
