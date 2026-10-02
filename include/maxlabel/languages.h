#pragma once

// The languages MaxLabel can route, and how a detector's answer maps onto them.
//
// Deliberately a table rather than a switch.  The set is small because it is
// what the aligner can route, not because the code only handles a few: adding
// a language should be a data change — one entry — and everything downstream
// (the editor's language buttons, the segmentation's mapping, the shortcut
// keys) is built from it.
//
// The detector's vocabulary is not the same as ours.  fastText's lid.176 has
// 176 labels; several of them mean the same thing to us (zh-cn, cmn, wuu and
// yue are all Mandarin-or-Cantonese in the source pipeline's map), and most of
// them mean nothing at all.  `detector_ids` is where that mapping lives, and a
// detector answer with no entry stays unknown — which is how traditional
// Chinese ends up unresolved rather than being claimed as Mandarin.

#include <cstddef>
#include <string>
#include <vector>

namespace maxlabel {

struct LanguageInfo {
    std::string id;                         // what goes into PFML, e.g. "zh"
    std::string label;                      // what a human reads, e.g. "Chinese"
    std::vector<std::string> detector_ids;  // ids a detector may return for it
    char shortcut = '\0';                   // the key that selects it in the editor
};

class LanguageTable {
public:
    // zh / ja / en / ko / yue, with the shortcut keys 1..5.
    static const LanguageTable & builtin();

    const std::vector<LanguageInfo> & all() const { return languages_; }

    // By our own id.
    const LanguageInfo * find(const std::string & id) const;

    // By what a detector said.  Null when the table has no home for it, which
    // the segmentation treats as unknown rather than guessing.
    const LanguageInfo * for_detector_id(const std::string & detector_id) const;

    // Whether this is an id MaxLabel can put in PFML.
    bool knows(const std::string & id) const { return find(id) != nullptr; }

    void add(LanguageInfo info);

private:
    std::vector<LanguageInfo> languages_;
};

}  // namespace maxlabel
