// BudouX — see include/maxlabel/budoux.h.
//
// A transcription of budoux/parser.py.  The two things that are easy to get
// subtly wrong, and are therefore called out where they happen: the base score
// is a half-integer for some models, and the boundary test is strictly `> 0`.

#include "maxlabel/budoux.h"

#include "mini_json.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace maxlabel {

namespace {

const char * const kFeatures[] = {
    "UW1", "UW2", "UW3", "UW4", "UW5", "UW6",
    "BW1", "BW2", "BW3",
    "TW1", "TW2", "TW3", "TW4",
};

// Byte offsets of every code point, so a feature string can be taken by code
// point range.  BudouX indexes by code point; slicing bytes directly would cut
// a multi-byte character in half.
std::vector<std::pair<std::size_t, std::size_t>> code_points(const std::string & text) {
    std::vector<std::pair<std::size_t, std::size_t>> points;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t begin = i;
        const auto byte = [&](std::size_t k) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(text[k]));
        };
        const std::uint32_t b0 = byte(i);
        std::size_t extra = 0;
        if (b0 < 0x80) extra = 0;
        else if ((b0 & 0xE0) == 0xC0) extra = 1;
        else if ((b0 & 0xF0) == 0xE0) extra = 2;
        else if ((b0 & 0xF8) == 0xF0) extra = 3;
        else extra = 0;
        i += extra + 1;
        if (i > text.size()) i = text.size();
        points.emplace_back(begin, i);
    }
    return points;
}

}  // namespace

const BudouX::Table & BudouX::table(const char * name) const {
    static const Table empty;
    const auto it = tables_.find(name);
    return it == tables_.end() ? empty : it->second;
}

bool BudouX::load(const std::string & path, std::string * error) {
    tables_.clear();
    base_score_ = 0.0;
    ready_ = false;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) *error = "cannot read the BudouX model " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    json::Value model;
    try {
        model = json::parse(buffer.str());
    } catch (const std::exception & failure) {
        if (error != nullptr) *error = std::string(path) + ": " + failure.what();
        return false;
    }
    if (!model.is_object()) {
        if (error != nullptr) *error = path + " is not a BudouX model (expected an object)";
        return false;
    }

    double total = 0.0;
    for (const std::pair<std::string, json::Value> & feature : model.object) {
        if (!feature.second.is_object()) continue;
        Table weights;
        for (const std::pair<std::string, json::Value> & entry : feature.second.object) {
            if (!entry.second.is_number()) continue;
            const int weight = static_cast<int>(entry.second.number);
            weights.emplace(entry.first, weight);
            total += static_cast<double>(weight);
        }
        tables_.emplace(feature.first, std::move(weights));
    }
    if (tables_.empty()) {
        if (error != nullptr) *error = path + " has no feature tables";
        return false;
    }

    // -sum * 0.5, in floating point: zh-hans sums to an odd number, so the
    // base score is x.5 and integer division would be off by half a point —
    // enough to flip a boundary test that is a strict comparison.
    base_score_ = -total * 0.5;
    ready_ = true;
    return true;
}

std::vector<std::string> BudouX::parse(const std::string & text) const {
    if (text.empty()) return {};
    if (!ready_) return { text };

    const std::vector<std::pair<std::size_t, std::size_t>> points = code_points(text);
    const std::size_t count = points.size();
    if (count < 2) return { text };

    // The feature string for code point range [from, to).
    const auto slice = [&](std::size_t from, std::size_t to) {
        return text.substr(points[from].first, points[to - 1].second - points[from].first);
    };
    const auto weight = [&](const char * name, const std::string & key) {
        const Table & t = table(name);
        const auto it = t.find(key);
        return it == t.end() ? 0 : it->second;
    };

    std::vector<std::size_t> chunk_starts{ 0 };   // code point index of each chunk
    for (std::size_t i = 1; i < count; ++i) {
        double score = base_score_;
        if (i > 2) score += weight("UW1", slice(i - 3, i - 2));
        if (i > 1) score += weight("UW2", slice(i - 2, i - 1));
        score += weight("UW3", slice(i - 1, i));
        score += weight("UW4", slice(i, i + 1));
        if (i + 1 < count) score += weight("UW5", slice(i + 1, i + 2));
        if (i + 2 < count) score += weight("UW6", slice(i + 2, i + 3));

        if (i > 1) score += weight("BW1", slice(i - 2, i));
        score += weight("BW2", slice(i - 1, i + 1));
        if (i + 1 < count) score += weight("BW3", slice(i, i + 2));

        if (i > 2) score += weight("TW1", slice(i - 3, i));
        if (i > 1) score += weight("TW2", slice(i - 2, i + 1));
        if (i + 1 < count) score += weight("TW3", slice(i - 1, i + 2));
        if (i + 2 < count) score += weight("TW4", slice(i, i + 3));

        // Strictly greater, as in the original.
        if (score > 0.0) chunk_starts.push_back(i);
    }

    std::vector<std::string> chunks;
    chunks.reserve(chunk_starts.size());
    for (std::size_t k = 0; k < chunk_starts.size(); ++k) {
        const std::size_t from = chunk_starts[k];
        const std::size_t to = (k + 1 < chunk_starts.size()) ? chunk_starts[k + 1] : count;
        chunks.push_back(slice(from, to));
    }
    return chunks;
}

}  // namespace maxlabel
