// A small JSON reader — see mini_json.h.

#include "mini_json.h"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace maxlabel::json {

const Value * Value::find(const std::string & key) const {
    for (const std::pair<std::string, Value> & member : object) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string & text) : text_(text) {}

    Value parse() {
        skip_space();
        Value value = parse_value();
        skip_space();
        if (at_ != text_.size()) fail("trailing content");
        return value;
    }

private:
    const std::string & text_;
    std::size_t at_ = 0;

    [[noreturn]] void fail(const std::string & what) const {
        throw std::runtime_error("JSON: " + what + " at byte " + std::to_string(at_));
    }

    void skip_space() {
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++at_;
                continue;
            }
            break;
        }
    }

    bool take(char expected) {
        if (at_ < text_.size() && text_[at_] == expected) {
            ++at_;
            return true;
        }
        return false;
    }

    bool literal(const char * word) {
        const std::size_t length = std::char_traits<char>::length(word);
        if (text_.compare(at_, length, word) != 0) return false;
        at_ += length;
        return true;
    }

    Value parse_value() {
        if (at_ >= text_.size()) fail("unexpected end");
        const char c = text_[at_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') {
            Value value;
            value.kind = Value::Kind::String;
            value.text = parse_string();
            return value;
        }
        if (literal("true")) {
            Value value;
            value.kind = Value::Kind::Bool;
            value.boolean = true;
            return value;
        }
        if (literal("false")) {
            Value value;
            value.kind = Value::Kind::Bool;
            return value;
        }
        if (literal("null")) return Value{};
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        fail("unexpected character");
    }

    Value parse_object() {
        Value value;
        value.kind = Value::Kind::Object;
        take('{');
        skip_space();
        if (take('}')) return value;
        while (true) {
            skip_space();
            if (at_ >= text_.size() || text_[at_] != '"') fail("expected a key");
            std::string key = parse_string();
            skip_space();
            if (!take(':')) fail("expected ':'");
            skip_space();
            value.object.emplace_back(std::move(key), parse_value());
            skip_space();
            if (take(',')) continue;
            if (take('}')) return value;
            fail("expected ',' or '}'");
        }
    }

    Value parse_array() {
        Value value;
        value.kind = Value::Kind::Array;
        take('[');
        skip_space();
        if (take(']')) return value;
        while (true) {
            skip_space();
            value.array.push_back(parse_value());
            skip_space();
            if (take(',')) continue;
            if (take(']')) return value;
            fail("expected ',' or ']'");
        }
    }

    // Appends a code point as UTF-8.
    static void append_utf8(std::string & out, std::uint32_t code) {
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

    std::uint32_t parse_hex4() {
        if (at_ + 4 > text_.size()) fail("truncated \\u escape");
        std::uint32_t value = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = text_[at_++];
            value <<= 4;
            if (h >= '0' && h <= '9') value |= static_cast<std::uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') value |= static_cast<std::uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') value |= static_cast<std::uint32_t>(h - 'A' + 10);
            else fail("bad hex digit");
        }
        return value;
    }

    std::string parse_string() {
        take('"');
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_ >= text_.size()) fail("truncated escape");
            const char escape = text_[at_++];
            switch (escape) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case '/': out.push_back('/'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case 'u': {
                    std::uint32_t code = parse_hex4();
                    // A surrogate pair is two escapes; combine them.
                    if (code >= 0xD800 && code <= 0xDBFF && at_ + 1 < text_.size() &&
                        text_[at_] == '\\' && text_[at_ + 1] == 'u') {
                        at_ += 2;
                        const std::uint32_t low = parse_hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    append_utf8(out, code);
                    break;
                }
                default: fail("unknown escape");
            }
        }
        fail("unterminated string");
    }

    Value parse_number() {
        const std::size_t begin = at_;
        if (!take('-')) { /* optional */ }
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') {
                ++at_;
                continue;
            }
            break;
        }
        Value value;
        value.kind = Value::Kind::Number;
        value.number = std::strtod(text_.substr(begin, at_ - begin).c_str(), nullptr);
        return value;
    }
};

}  // namespace

Value parse(const std::string & text) { return Parser(text).parse(); }

}  // namespace maxlabel::json
