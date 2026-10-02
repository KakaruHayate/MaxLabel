// Reading PFML back into the model — see include/maxlabel/import_pfml.h.

#include "maxlabel/import_pfml.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace maxlabel {

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string trim(const std::string & s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_whitespace(const std::string & s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_space(s[i])) ++i;
        const std::size_t b = i;
        while (i < s.size() && !is_space(s[i])) ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

// Appends one code point as UTF-8.
void append_utf8(std::string & out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

std::string decode_entities(const std::string & s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        const std::size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back('&');
            continue;
        }
        const std::string entity = s.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string digits = entity.substr(hex ? 2 : 1);
            const std::uint32_t code = static_cast<std::uint32_t>(
                std::stoul(digits, nullptr, hex ? 16 : 10));
            append_utf8(out, code);
        } else {
            out.push_back('&');
            continue;
        }
        i = semi;
    }
    return out;
}

struct Tag {
    std::string name;
    bool closing = false;
    bool self_closing = false;
    std::vector<std::pair<std::string, std::string>> attributes;

    const std::string * attribute(const char * key) const {
        for (const std::pair<std::string, std::string> & entry : attributes) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }
};

// The scanner.  Deliberately small: it only has to read what MaxLabel writes,
// and anything else is reported as an error rather than guessed at.
class Reader {
public:
    explicit Reader(const std::string & source) : s_(source) {}

    ImportedFragment run() {
        std::size_t i = 0;
        std::string text;
        std::vector<std::pair<std::string, std::size_t>> open_scopes;   // language, start
        std::vector<std::size_t> open_words;

        const auto close_scope = [&](std::size_t end) {
            if (open_scopes.empty()) return;
            const std::pair<std::string, std::size_t> & top = open_scopes.back();
            if (!top.first.empty() && end > top.second) {
                LangSpan span;
                span.begin     = top.second;
                span.end       = end;
                span.language  = top.first;
                span.manual    = true;   // it was written down, so it was decided
                span.ambiguous = false;
                out_.spans.push_back(std::move(span));
            }
            open_scopes.pop_back();
        };

        const auto close_word = [&](std::size_t end) {
            if (open_words.empty()) return;
            const std::size_t begin = open_words.back();
            open_words.pop_back();
            if (end > begin) out_.words.push_back(WordBoundary{ begin, end });
        };

        while (i < s_.size()) {
            if (s_[i] != '<') {
                const std::size_t start = i;
                while (i < s_.size() && s_[i] != '<') ++i;
                text += decode_entities(s_.substr(start, i - start));
                continue;
            }
            Tag tag;
            if (!read_tag(i, tag)) {
                out_.error = "malformed markup";
                return finish(text);
            }

            if (tag.name == "scope") {
                if (tag.closing) {
                    close_scope(text.size());
                } else {
                    const std::string * language = tag.attribute("language");
                    open_scopes.emplace_back(language == nullptr ? std::string() : *language,
                                             text.size());
                }
            } else if (tag.name == "word") {
                if (tag.closing) {
                    close_word(text.size());
                } else {
                    // A word with a text attribute contributes that text now;
                    // its phonemes become an override over exactly that range.
                    const std::string * word_text = tag.attribute("text");
                    const std::string * phonemes = tag.attribute("phonemes");
                    const std::string * script = tag.attribute("script");
                    const std::string * language = tag.attribute("language");

                    const std::size_t begin = text.size();
                    if (word_text != nullptr) text += *word_text;
                    const std::size_t end = text.size();

                    if (phonemes != nullptr) {
                        Override override_;
                        override_.begin    = begin;
                        override_.end      = end;
                        override_.language = language == nullptr ? std::string() : *language;
                        override_.script   = script == nullptr ? std::string() : *script;
                        override_.phonemes = split_whitespace(*phonemes);
                        out_.overrides.push_back(std::move(override_));
                    } else if (!tag.self_closing) {
                        open_words.push_back(begin);
                    }
                }
            } else if (tag.name == "phoneme") {
                if (!tag.closing) {
                    const std::string * symbol = tag.attribute("symbol");
                    if (symbol != nullptr && !symbol->empty()) {
                        Override inserted;
                        inserted.begin = text.size();
                        inserted.end   = text.size();
                        inserted.phonemes = { *symbol };
                        out_.overrides.push_back(std::move(inserted));
                    }
                }
            } else {
                // <reading>, <path>, <group> and anything else are containers
                // the editor does not model; their character data still belongs
                // to the text, but their structure would be lost, so say so.
                if (!tag.closing && !tag.self_closing) {
                    out_.error = "the fragment uses <" + tag.name +
                                 ">, which the editor does not model";
                }
            }
        }

        while (!open_scopes.empty()) close_scope(text.size());
        while (!open_words.empty()) close_word(text.size());
        return finish(text);
    }

private:
    ImportedFragment finish(std::string text) {
        out_.text = std::move(text);
        return std::move(out_);
    }

    bool read_tag(std::size_t & i, Tag & tag) {
        if (s_[i] != '<') return false;
        ++i;
        if (i < s_.size() && s_[i] == '/') {
            tag.closing = true;
            ++i;
        }
        const std::size_t begin = i;
        while (i < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i])) ||
                                 s_[i] == '-' || s_[i] == '_')) {
            ++i;
        }
        tag.name = s_.substr(begin, i - begin);
        if (tag.name.empty()) return false;

        while (i < s_.size()) {
            while (i < s_.size() && is_space(s_[i])) ++i;
            if (i >= s_.size()) return false;
            if (s_[i] == '/') {
                tag.self_closing = true;
                ++i;
                continue;
            }
            if (s_[i] == '>') {
                ++i;
                return true;
            }
            const std::size_t key_begin = i;
            while (i < s_.size() && s_[i] != '=' && !is_space(s_[i]) && s_[i] != '>' &&
                   s_[i] != '/') {
                ++i;
            }
            const std::string key = s_.substr(key_begin, i - key_begin);
            while (i < s_.size() && is_space(s_[i])) ++i;
            if (i >= s_.size() || s_[i] != '=') return false;
            ++i;
            while (i < s_.size() && is_space(s_[i])) ++i;
            if (i >= s_.size() || (s_[i] != '"' && s_[i] != '\'')) return false;
            const char quote = s_[i++];
            const std::size_t value_begin = i;
            while (i < s_.size() && s_[i] != quote) ++i;
            if (i >= s_.size()) return false;
            tag.attributes.emplace_back(key, decode_entities(s_.substr(value_begin, i - value_begin)));
            ++i;
        }
        return false;
    }

    const std::string & s_;
    ImportedFragment out_;
};

}  // namespace

ImportedFragment import_pfml(const std::string & pfml) {
    ImportedFragment result;
    if (trim(pfml).empty()) return result;
    if (pfml.find('<') == std::string::npos) {
        // Plain text is a valid fragment; there is just nothing to reconstruct.
        result.text = pfml;
        return result;
    }
    Reader reader(pfml);
    return reader.run();
}

}  // namespace maxlabel
