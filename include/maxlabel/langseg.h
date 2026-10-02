#pragma once

// Language segmentation, following GPT-SoVITS's text frontend.
//
// GPT-SoVITS splits with `split-lang` (rule pre-split by Unicode block, then
// `budoux` for zh/ja, then a language detector) and then post-processes the
// result in `LangSegmenter.getTexts`: its own language map, `full_en` for short
// English misdetected as something else, `full_cjk` for a Han run the detector
// gave up on, `split_jako` to pull kana or hangul out of a run labelled
// otherwise, and resolution chains for digits and for unknowns.
//
// This file ports that decision logic.  Two pieces of the original are *not*
// here yet, and both are called out where they would apply:
//
//   * `budoux` sub-splitting inside a zh/ja run — it decides how finely a
//     Chinese or Japanese run is cut before detection.  Without it a run is
//     detected whole, which for a short lyric line usually merges back to the
//     same answer but is not identical.
//   * the detector model.  `split-lang` hardcodes fastText's `lid.176`
//     (`full` = 125 MB, `lite` = 916 KB); the weights are CC BY-SA 3.0 even
//     though the code is MIT.  `LangDetector` is the seam: supply a fastText
//     detector and the behaviour matches, supply the script fallback and the
//     Han-only runs fall through to the unknown chain — which resolves them to
//     Chinese, exactly as the original does when the model is unsure.

#include "maxlabel/budoux.h"
#include "maxlabel/language.h"

#include <memory>
#include <string>
#include <vector>

namespace maxlabel {

// One piece of the split, in text order.  `lang` is a language id, or one of
// the markers the pipeline uses internally: "x" (unknown), "digit".
struct LangPiece {
    std::string lang;
    std::string text;
};

// What the rules cannot settle: a Han-only run, or a run in neither CJK nor
// kana.  This is the one thing the model is consulted for.
class LangDetector {
public:
    virtual ~LangDetector() = default;
    // The language of `text`, lower case, or "x" when it cannot say.
    virtual std::string detect(const std::string & text) const = 0;
};

// The no-model fallback: Unicode script ranges.  Kana settles Japanese and
// hangul settles Korean without any model; everything else it declines to
// answer ("x"), leaving the decision to the pipeline's own chains.
class ScriptDetector : public LangDetector {
public:
    std::string detect(const std::string & text) const override;
};

// What the pipeline needs to run: the detector for what the rules cannot
// settle, and the BudouX models for sub-splitting a Chinese or Japanese run.
// Any of them may be absent, and the pipeline degrades rather than fails.
struct SegmentationContext {
    const LangDetector * detector = nullptr;
    const BudouX * japanese = nullptr;   // applied to a zh/ja run first
    const BudouX * chinese = nullptr;    // then to each chunk of that
};

// The context the tool uses by default: the script detector, and the BudouX
// models found in the model directory (see models.h).  Loaded once, on first
// use, so a missing model directory costs nothing until something asks.
const SegmentationContext & default_segmentation_context();

// The GPT-SoVITS pipeline.  `default_lang` is its `default_lang` argument:
// non-empty forces every non-English, non-digit piece to that language, which
// is what the tool does when the project's language is known.
std::vector<LangPiece> segment_languages(const std::string & text,
                                         const std::string & default_lang,
                                         const SegmentationContext & context);

// The spans for `text` under an explicit context.  `detect_languages` in
// language.h is this with the default context; this overload is what pins the
// pipeline's own behaviour in tests, without depending on which models happen
// to be installed on the machine.
std::vector<LangSpan> detect_languages(const std::string & text,
                                       const std::string & default_lang,
                                       const SegmentationContext & context);

// `full_en` / `full_cjk` / `split_jako`, exposed because they are the parts
// worth testing on their own.
bool is_full_en(const std::string & text);
std::string keep_cjk(const std::string & text);
std::vector<LangPiece> split_jako(const std::string & tag_lang, const LangPiece & item);

}  // namespace maxlabel
