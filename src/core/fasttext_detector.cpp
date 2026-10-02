// The fastText language detector — see include/maxlabel/fasttext_detector.h.

#include "maxlabel/fasttext_detector.h"

#include <fasttext.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace maxlabel {

namespace {

// fast-langdetect's own preprocessing.  It is part of the behaviour, not an
// optimisation: a detector fed a different string gives a different answer.
constexpr std::size_t kMaxInputLength = 80;

std::string preprocess(const std::string & text) {
    std::string out;
    out.reserve(text.size());

    // Newlines become spaces, and the input is truncated by code point (the
    // original's limit counts Python characters).
    std::size_t code_points = 0;
    std::size_t i = 0;
    while (i < text.size() && code_points < kMaxInputLength) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        std::size_t width = 1;
        if ((b & 0xE0) == 0xC0) width = 2;
        else if ((b & 0xF0) == 0xE0) width = 3;
        else if ((b & 0xF8) == 0xF0) width = 4;
        if (i + width > text.size()) width = 1;

        if (width == 1 && (text[i] == '\n' || text[i] == '\r')) {
            out.push_back(' ');
        } else {
            out.append(text, i, width);
        }
        i += width;
        ++code_points;
    }

    // A string that is mostly capitals is lowercased, because lid.176 was
    // trained on ordinary text and reads shouting as noise.
    std::size_t letters = 0;
    std::size_t capitals = 0;
    for (const char c : out) {
        if (c >= 'A' && c <= 'Z') {
            ++letters;
            ++capitals;
        } else if (c >= 'a' && c <= 'z') {
            ++letters;
        }
    }
    if (out.size() > 5 && letters > 0 &&
        static_cast<double>(capitals) / static_cast<double>(letters) > 0.8) {
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    return out;
}

}  // namespace

struct FastTextDetector::Impl {
    fasttext::FastText model;
    bool ready = false;
};

FastTextDetector::FastTextDetector() : impl_(std::make_unique<Impl>()) {}
FastTextDetector::~FastTextDetector() = default;
FastTextDetector::FastTextDetector(FastTextDetector &&) noexcept = default;
FastTextDetector & FastTextDetector::operator=(FastTextDetector &&) noexcept = default;

bool FastTextDetector::load(const std::string & model_path, std::string * error) {
    impl_->ready = false;
    try {
        impl_->model.loadModel(model_path);
    } catch (const std::exception & failure) {
        // loadModel throws std::invalid_argument, with a message that already
        // names the file.
        if (error != nullptr) *error = failure.what();
        return false;
    }
    impl_->ready = true;
    return true;
}

bool FastTextDetector::ready() const { return impl_ != nullptr && impl_->ready; }

std::string FastTextDetector::detect(const std::string & text) const {
    if (!ready()) return "x";
    const std::string input = preprocess(text);
    if (input.empty()) return "x";

    std::istringstream stream(input);
    std::vector<std::pair<fasttext::real, std::string>> predictions;
    try {
        impl_->model.predictLine(stream, predictions, 1, 0.0f);
    } catch (const std::exception &) {
        return "x";
    }
    if (predictions.empty()) return "x";

    std::string label = predictions.front().second;
    static const std::string prefix = "__label__";
    if (label.compare(0, prefix.size(), prefix) == 0) label.erase(0, prefix.size());
    std::transform(label.begin(), label.end(), label.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return label;
}

}  // namespace maxlabel
