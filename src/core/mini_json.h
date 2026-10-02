#pragma once

// A small JSON reader, enough for the BudouX model files.
//
// Those files are a single object of objects of integers and nothing else, so
// a full JSON library would be a large dependency for one shape.  What is here
// covers the whole grammar anyway — it is the amount of code it takes, not a
// subset — so it is safe to point at anything.

#include <string>
#include <utility>
#include <vector>

namespace maxlabel::json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;   // insertion-ordered

    bool is_object() const { return kind == Kind::Object; }
    bool is_array() const { return kind == Kind::Array; }
    bool is_number() const { return kind == Kind::Number; }
    bool is_string() const { return kind == Kind::String; }

    // The member named `key`, or nullptr.  Linear, which is fine for the
    // handful of keys these files have.
    const Value * find(const std::string & key) const;
};

// Throws std::runtime_error with the byte offset when the text is not JSON.
Value parse(const std::string & text);

}  // namespace maxlabel::json
