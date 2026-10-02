#pragma once

// The language detector split-lang uses: fastText's lid.176.
//
// 176 languages, a supervised classifier over character n-grams.  It is the
// only part of the pipeline that is a model rather than a rule, and the only
// part that can answer for a Han-only run or a non-English Latin one.
//
// The weights are CC BY-SA 3.0 even though fastText's code is MIT, and "full"
// is 125 MB against "lite"'s 916 KB, so this takes a path rather than shipping
// one.  `full` is what split-lang hardcodes; `lite` is the same training run
// compressed and will occasionally disagree.

#include "maxlabel/langseg.h"

#include <memory>
#include <string>

namespace maxlabel {

class FastTextDetector : public LangDetector {
public:
    FastTextDetector();
    ~FastTextDetector() override;
    FastTextDetector(FastTextDetector &&) noexcept;
    FastTextDetector & operator=(FastTextDetector &&) noexcept;

    bool load(const std::string & model_path, std::string * error = nullptr);
    bool ready() const;

    // The most likely language, lower case and without fastText's `__label__`
    // prefix, or "x" when nothing scores.
    std::string detect(const std::string & text) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace maxlabel
