// The language table — see include/maxlabel/languages.h.

#include "maxlabel/languages.h"

#include <algorithm>

namespace maxlabel {

namespace {

LanguageTable make_builtin() {
    LanguageTable table;

    // The detector ids follow GPT-SoVITS's DEFAULT_LANG_MAP: it folds
    // Cantonese, Wu and Simplified-Chinese variants into zh, and deliberately
    // leaves traditional Chinese (zh-tw) unmapped so it falls to the unknown
    // chain rather than being claimed as Mandarin.  Adding a language here is
    // the whole of what it takes to support one.
    table.add(LanguageInfo{ "zh", "Chinese",
                            { "zh", "zh-cn", "cmn", "wuu", "yue" }, '1' });
    table.add(LanguageInfo{ "ja", "Japanese", { "ja", "jpn" }, '2' });
    table.add(LanguageInfo{ "en", "English", { "en", "eng" }, '3' });
    table.add(LanguageInfo{ "ko", "Korean", { "ko", "kor" }, '4' });
    // Nothing a detector returns maps here on its own — the map above sends
    // "yue" to zh — but the aligner has a Cantonese converter, so it is
    // selectable by hand and by -l.
    table.add(LanguageInfo{ "yue", "Cantonese", { "zh-yue", "cantonese" }, '5' });

    return table;
}

}  // namespace

const LanguageTable & LanguageTable::builtin() {
    static const LanguageTable table = make_builtin();
    return table;
}

const LanguageInfo * LanguageTable::find(const std::string & id) const {
    for (const LanguageInfo & language : languages_) {
        if (language.id == id) return &language;
    }
    return nullptr;
}

const LanguageInfo * LanguageTable::for_detector_id(const std::string & detector_id) const {
    if (detector_id.empty()) return nullptr;
    for (const LanguageInfo & language : languages_) {
        if (std::find(language.detector_ids.begin(), language.detector_ids.end(),
                      detector_id) != language.detector_ids.end()) {
            return &language;
        }
    }
    return nullptr;
}

void LanguageTable::add(LanguageInfo info) {
    if (info.id.empty()) return;
    const auto existing = std::find_if(
        languages_.begin(), languages_.end(),
        [&info](const LanguageInfo & language) { return language.id == info.id; });
    if (existing != languages_.end()) {
        *existing = std::move(info);
        return;
    }
    languages_.push_back(std::move(info));
}

}  // namespace maxlabel
