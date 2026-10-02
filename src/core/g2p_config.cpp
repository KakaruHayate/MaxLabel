// Building a G2P configuration from a model directory — see g2p_config.h.

#include "maxlabel/g2p_config.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace maxlabel {

namespace fs = std::filesystem;

namespace {

bool present(const fs::path & base, const std::string & relative) {
    std::error_code error;
    return fs::exists(base / relative, error);
}

// The converters and the dictionaries they read, in the layout the aligner
// ships.  A converter is only offered when its dictionary is actually there —
// a half-configured pipeline would silently route text to nothing.
struct Converter {
    std::string id;
    std::string language;
    std::vector<std::pair<std::string, std::string>> kwargs;   // name -> "@relative"
};

std::string to_json(const std::vector<Converter> & converters) {
    std::string out = "{\"converters\":[";
    for (std::size_t i = 0; i < converters.size(); ++i) {
        if (i != 0) out += ",";
        const Converter & converter = converters[i];
        out += "{\"id\":\"" + converter.id + "\",\"language\":\"" + converter.language +
               "\",\"kwargs\":{";
        for (std::size_t k = 0; k < converter.kwargs.size(); ++k) {
            if (k != 0) out += ",";
            out += "\"" + converter.kwargs[k].first + "\":\"@" +
                   converter.kwargs[k].second + "\"";
        }
        out += "}}";
    }
    out += "]}";
    return out;
}

}  // namespace

std::string build_g2p_config(const std::string & model_dir) {
    if (model_dir.empty()) return std::string();
    const fs::path base(model_dir);

    std::vector<Converter> converters;
    // Each entry is only offered when its dictionary is present; the engine
    // directory is what cpp-pinyin reads its phrase tables from.
    if (present(base, "dictionaries/ds-zh-pinyin-lite.txt") &&
        present(base, "cpp_pinyin/mandarin")) {
        converters.push_back(Converter{
            "chinese-pinyin", "zh",
            { { "dict_path", "dictionaries/ds-zh-pinyin-lite.txt" },
              { "engine_dict_path", "cpp_pinyin/mandarin" } } });
    }
    if (present(base, "dictionaries/japanese_dict_full.txt")) {
        converters.push_back(Converter{ "japanese-kana", "ja",
                                        { { "dict_path", "dictionaries/japanese_dict_full.txt" } } });
    }
    if (present(base, "dictionaries/jyutping_dict.txt") &&
        present(base, "cpp_pinyin/cantonese")) {
        converters.push_back(Converter{
            "yue-jyutping", "yue",
            { { "dict_path", "dictionaries/jyutping_dict.txt" },
              { "engine_dict_path", "cpp_pinyin/cantonese" } } });
    }
    if (present(base, "dictionaries/ds_cmudict-07b.txt")) {
        converters.push_back(Converter{ "dictionary", "en",
                                        { { "dict_path", "dictionaries/ds_cmudict-07b.txt" } } });
    }
    if (converters.empty()) return std::string();
    return to_json(converters);
}

}  // namespace maxlabel
