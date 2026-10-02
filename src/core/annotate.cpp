// Annotation serialization — see include/maxlabel/annotate.h.

#include "maxlabel/annotate.h"

#include "maxlabel/core.h"

#include <algorithm>
#include <string>
#include <vector>

namespace maxlabel {

namespace {

// One thing to emit inside a span.  Word boundaries and range overrides cover
// a run; an insert is a zero-length mark at a position.
struct Mark {
    std::size_t begin = 0;
    std::size_t end   = 0;
    const Override * override_ = nullptr;   // null = a plain fixed word
    bool insert = false;
};

void append_phonemes(std::string & out, const std::vector<std::string> & phonemes) {
    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        if (i != 0) out.push_back(' ');
        out += phonemes[i];
    }
}

}  // namespace

std::string annotate_to_pfml(const std::string & text,
                             const std::vector<LangSpan> & spans,
                             const std::vector<WordBoundary> & words,
                             const std::vector<Override> & overrides,
                             std::string * error) {
    const auto report = [&](const std::string & message) {
        if (error != nullptr && error->empty()) *error = message;
    };

    std::vector<Mark> marks;
    marks.reserve(words.size() + overrides.size());
    for (const WordBoundary & word : words) {
        if (word.begin < word.end && word.end <= text.size()) {
            marks.push_back(Mark{ word.begin, word.end, nullptr, false });
        }
    }
    for (const Override & override_ : overrides) {
        if (override_.empty() || override_.begin > override_.end ||
            override_.end > text.size()) {
            continue;   // an override with no phonemes says nothing
        }
        if (override_.inserts()) {
            marks.push_back(Mark{ override_.begin, override_.begin, &override_, true });
        } else {
            marks.push_back(Mark{ override_.begin, override_.end, &override_, false });
        }
    }
    // Text order; an insert at the same position as a range comes first, since
    // the sound precedes the word it sits in front of.
    std::sort(marks.begin(), marks.end(), [](const Mark & a, const Mark & b) {
        if (a.begin != b.begin) return a.begin < b.begin;
        return a.insert && !b.insert;
    });

    // Everything inside [begin, end), in text order.  `include_end` lets the
    // final span own an insert sitting exactly at the end of the text.
    const auto emit_inside = [&](std::size_t begin, std::size_t end,
                                 const std::string & span_language, bool include_end) {
        std::string out;
        std::size_t pos = begin;

        for (const Mark & mark : marks) {
            if (mark.begin < begin) {
                // Starts earlier.  If it reaches into this span it cannot be
                // spelled here — <word> lives inside <scope>.
                if (mark.end > begin) {
                    report("an annotation crosses a language boundary (byte " +
                           std::to_string(mark.begin) + "); it was left as plain text");
                }
                continue;
            }
            if (mark.begin > end) break;
            if (mark.begin == end && !include_end) break;

            if (mark.insert) {
                if (mark.begin > pos) out += escape_text(text.substr(pos, mark.begin - pos));
                for (const std::string & phoneme : mark.override_->phonemes) {
                    out += "<phoneme symbol=\"";
                    out += escape_text(phoneme);
                    out += "\"/>";
                }
                pos = std::max(pos, mark.begin);
                continue;
            }

            if (mark.end > end) {
                report("an annotation crosses a language boundary (byte " +
                       std::to_string(mark.begin) + "); it was left as plain text");
                continue;
            }
            if (mark.begin < pos) continue;   // swallowed by an earlier mark
            if (mark.begin > pos) out += escape_text(text.substr(pos, mark.begin - pos));

            if (mark.override_ == nullptr) {
                out += "<word>";
                out += escape_text(text.substr(mark.begin, mark.end - mark.begin));
                out += "</word>";
            } else {
                const Override & override_ = *mark.override_;
                const std::string & language =
                    override_.language.empty() ? span_language : override_.language;
                out += "<word text=\"";
                out += escape_text(text.substr(mark.begin, mark.end - mark.begin));
                out += "\" language=\"";
                out += escape_text(language);
                out += "\" script=\"";
                out += escape_text(override_.script);
                out += "\" phonemes=\"";
                append_phonemes(out, override_.phonemes);
                out += "\"/>";
            }
            pos = mark.end;
        }

        if (pos < end) out += escape_text(text.substr(pos, end - pos));
        return out;
    };

    std::string out;
    std::size_t i = 0;
    while (i < spans.size()) {
        const LangSpan & span = spans[i];
        if (span.begin >= span.end || span.end > text.size()) {
            ++i;
            continue;
        }
        // Absorb the following spans that resolved to the same language, so a
        // run split by the detector does not become two adjacent scopes.
        std::size_t end = span.end;
        std::size_t next = i + 1;
        while (next < spans.size() && spans[next].language == span.language &&
               spans[next].begin == end && spans[next].end > spans[next].begin) {
            end = spans[next].end;
            ++next;
        }
        const bool last = next >= spans.size();

        if (span.language.empty()) {
            // Undetermined: bare text, no scope.  Valid PFML that says "no
            // language here", rather than a scope asserting a wrong one.
            out += emit_inside(span.begin, end, std::string(), last);
        } else {
            out += "<scope language=\"";
            out += span.language;
            out += "\">";
            out += emit_inside(span.begin, end, span.language, last);
            out += "</scope>";
        }
        i = next;
    }
    return out;
}

std::string spans_to_pfml(const std::string & text, const std::vector<LangSpan> & spans) {
    return annotate_to_pfml(text, spans, {}, {}, nullptr);
}

std::string text_to_pfml(const std::string & text, const std::string & default_language) {
    return spans_to_pfml(text, detect_languages(text, default_language));
}

}  // namespace maxlabel
