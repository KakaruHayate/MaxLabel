// MaxLabel core: directory scanning, sidecar import and PFML export.
//
// The whole project model is basename-driven, so this file is the only place
// that knows the layout.  Nothing here needs Qt, a model or a network.

#include "maxlabel/core.h"

#include "maxlabel/import_pfml.h"

#include "tifa_ggml/g2p.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace maxlabel {

namespace fs = std::filesystem;

const char * to_string(TextSource source) {
    switch (source) {
        case TextSource::Pfml: return "pfml";
        case TextSource::Json: return "json";
        case TextSource::Text: return "txt";
        case TextSource::Lab:  return "lab";
        case TextSource::None: break;
    }
    return "none";
}

bool is_audio_extension(const std::string & extension) {
    std::string lower = extension;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "wav" || lower == "flac" || lower == "mp3";
}

namespace {

std::string read_file(const std::string & path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const std::string & path, const std::string & contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << contents;
    if (!out) throw std::runtime_error("cannot write " + path);
}

std::string trim(const std::string & s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// ---------------------------------------------------------------------------
// Minimal flat-JSON reader for the MinLabel / LyricFA sidecar.
//
// The sidecar is a flat object of strings and booleans
// ({"raw_text": "...", "lab": "...", "lab_without_tone": "...", "isCheck": true}),
// so nested values are skipped rather than modelled.  Pulling in a JSON library
// for four keys is not worth the dependency.
// ---------------------------------------------------------------------------

void skip_ws(const std::string & s, std::size_t & i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

bool parse_json_string(const std::string & s, std::size_t & i, std::string & out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        const char c = s[i++];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i >= s.size()) return false;
        const char escape = s[i++];
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
                if (i + 4 > s.size()) return false;
                unsigned code = 0;
                for (int k = 0; k < 4; ++k) {
                    const char h = s[i++];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= unsigned(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= unsigned(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= unsigned(h - 'A' + 10);
                    else return false;
                }
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default: out.push_back(escape); break;
        }
    }
    if (i >= s.size()) return false;
    ++i;   // closing quote
    return true;
}

void skip_json_value(const std::string & s, std::size_t & i);

void skip_json_container(const std::string & s, std::size_t & i) {
    const char open  = s[i];
    const char close = open == '{' ? '}' : ']';
    int depth = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') {
            std::string ignored;
            if (!parse_json_string(s, i, ignored)) return;
            continue;
        }
        if (c == open) {
            ++depth;
            ++i;
            continue;
        }
        if (c == close) {
            --depth;
            ++i;
            if (depth == 0) return;
            continue;
        }
        ++i;
    }
}

void skip_json_value(const std::string & s, std::size_t & i) {
    skip_ws(s, i);
    if (i >= s.size()) return;
    if (s[i] == '{' || s[i] == '[') {
        skip_json_container(s, i);
        return;
    }
    if (s[i] == '"') {
        std::string ignored;
        parse_json_string(s, i, ignored);
        return;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
}

struct Sidecar {
    std::map<std::string, std::string> strings;
    std::map<std::string, bool>        booleans;
};

Sidecar parse_sidecar(const std::string & text) {
    Sidecar out;
    std::size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{') return out;
    ++i;
    while (i < text.size()) {
        skip_ws(text, i);
        if (i < text.size() && text[i] == '}') break;
        if (i < text.size() && text[i] == ',') { ++i; continue; }

        std::string key;
        if (!parse_json_string(text, i, key)) break;
        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':') break;
        ++i;
        skip_ws(text, i);
        if (i >= text.size()) break;

        if (text[i] == '"') {
            std::string value;
            if (!parse_json_string(text, i, value)) break;
            out.strings[key] = value;
        } else if (text.compare(i, 4, "true") == 0) {
            out.booleans[key] = true;
            i += 4;
        } else if (text.compare(i, 5, "false") == 0) {
            out.booleans[key] = false;
            i += 5;
        } else {
            skip_json_value(text, i);
        }
    }
    return out;
}

std::string extension_of(const std::string & filename) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return std::string();
    return filename.substr(dot + 1);
}

std::string stem_of(const std::string & filename) {
    const std::size_t dot = filename.find_last_of('.');
    return dot == std::string::npos ? filename : filename.substr(0, dot);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

// ---------------------------------------------------------------------------
// scan
// ---------------------------------------------------------------------------

Project scan(const std::string & directory) {
    Project project;
    project.directory = directory;

    std::map<std::string, std::size_t> index;   // basename -> segment slot
    const auto slot_for = [&](const std::string & id) {
        const auto it = index.find(id);
        if (it != index.end()) return it->second;
        index.emplace(id, project.segments.size());
        project.segments.push_back(Segment{});
        project.segments.back().id = id;
        project.segments.back().directory = directory;
        return project.segments.size() - 1;
    };

    std::error_code ec;
    fs::directory_iterator it(directory, ec);
    if (ec) throw std::runtime_error("cannot read directory " + directory);

    for (const fs::directory_entry & entry : it) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        const std::string ext  = lower_ascii(extension_of(name));
        const std::string id   = stem_of(name);
        if (id.empty()) continue;

        if (is_audio_extension(ext)) {
            Segment & segment = project.segments[slot_for(id)];
            segment.audio_path = entry.path().string();
        } else if (ext == "pfml") {
            project.segments[slot_for(id)].pfml_path = entry.path().string();
        } else if (ext == "txt") {
            project.segments[slot_for(id)].txt_path = entry.path().string();
        } else if (ext == "lab") {
            project.segments[slot_for(id)].lab_path = entry.path().string();
        } else if (ext == "json") {
            project.segments[slot_for(id)].json_path = entry.path().string();
        }
    }

    std::sort(project.segments.begin(), project.segments.end(),
              [](const Segment & a, const Segment & b) { return a.id < b.id; });
    for (Segment & segment : project.segments) load(segment);
    return project;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

void load(Segment & segment) {
    segment.source     = TextSource::None;
    segment.text.clear();
    segment.lab.clear();
    segment.reviewed   = false;
    segment.spans.clear();
    segment.words.clear();
    segment.overrides.clear();
    segment.pfml.clear();
    segment.pfml_valid = true;
    segment.error.clear();

    // The sidecar, when present, supplies the legacy pronunciation line and
    // MinLabel's review flag regardless of which source wins for the text.
    std::string json_text;
    if (!segment.json_path.empty()) {
        const Sidecar sidecar = parse_sidecar(read_file(segment.json_path));
        const auto lab = sidecar.strings.find("lab");
        if (lab != sidecar.strings.end()) segment.lab = lab->second;
        const auto check = sidecar.booleans.find("isCheck");
        if (check != sidecar.booleans.end()) segment.reviewed = check->second;
        const auto raw = sidecar.strings.find("raw_text");
        if (raw != sidecar.strings.end()) json_text = raw->second;
    }
    if (segment.lab.empty() && !segment.lab_path.empty()) {
        segment.lab = trim(read_file(segment.lab_path));
    }

    // Text precedence mirrors the aligner: .pfml, then .txt, then .lab.
    // LyricFA's json carries the matched lyric text, which beats a bare
    // syllable line, so it sits between the two.
    if (!segment.pfml_path.empty()) {
        // Already markup: read it back into the model, so the editor shows the
        // lyric with its annotations on it rather than the markup.  Showing the
        // markup would be worse than ugly — the spans would be empty, the next
        // keystroke would re-derive language spans from the markup itself, and
        // the saved fragment would be garbage.
        segment.pfml   = trim(read_file(segment.pfml_path));
        segment.source = TextSource::Pfml;

        const ImportedFragment imported = import_pfml(segment.pfml);
        if (imported.error.empty()) {
            segment.text      = imported.text;
            segment.spans     = imported.spans;
            segment.words     = imported.words;
            segment.overrides = imported.overrides;
        } else {
            // Not something the editor models.  Kept as it stands rather than
            // half-read, and flagged so the window can say so.
            segment.text  = segment.pfml;
            segment.error = imported.error;
        }
    } else {
        if (!json_text.empty()) {
            segment.text   = json_text;
            segment.source = TextSource::Json;
        } else if (!segment.txt_path.empty()) {
            segment.text   = trim(read_file(segment.txt_path));
            segment.source = TextSource::Text;
        } else if (!segment.lab_path.empty()) {
            segment.text   = trim(read_file(segment.lab_path));
            segment.source = TextSource::Lab;
        }
        if (segment.source != TextSource::None) {
            // A plain transcript: the language has to be worked out, and the
            // PFML follows from it.
            detect_spans(segment);
            segment.pfml = spans_to_pfml(segment.text, segment.spans);
        }
    }

    if (segment.pfml.empty()) {
        segment.pfml_valid = true;   // nothing to be wrong yet
        return;
    }
    try {
        validate(segment.pfml);
    } catch (const std::exception & error) {
        segment.pfml_valid = false;
        segment.error      = error.what();
    }
}

// ---------------------------------------------------------------------------
// save / validate / escape
// ---------------------------------------------------------------------------

bool is_saved(const Segment & segment) {
    if (segment.id.empty() || segment.directory.empty()) return true;
    const fs::path target = fs::path(segment.directory) / (segment.id + ".pfml");
    std::error_code error;
    if (!fs::exists(target, error)) return false;
    return read_file(target.string()) == segment.pfml;
}

void save(const Segment & segment) {
    validate(segment.pfml);
    if (segment.id.empty()) throw std::runtime_error("segment has no id");
    if (segment.directory.empty()) {
        // Refusing beats guessing: writing to a relative path would put the
        // file wherever the process happens to be running.
        throw std::runtime_error("segment '" + segment.id + "' has no directory");
    }
    const fs::path target = fs::path(segment.directory) / (segment.id + ".pfml");
    // Rewriting identical bytes would only move the timestamp, and a file whose
    // timestamp changes every time you click a different segment looks edited
    // when it is not.
    if (read_file(target.string()) == segment.pfml) return;
    write_file(target.string(), segment.pfml);
}

void validate(const std::string & pfml) {
    tifa_ggml::validate_pfml(pfml);
}

std::string escape_text(const std::string & text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else out.push_back(c);
    }
    return out;
}

void detect_spans(Segment & segment) {
    segment.spans = detect_languages(segment.text, segment.default_language);
}

void rebuild_pfml(Segment & segment) {
    // A segment with no spans is a .pfml source: its language is already in the
    // fragment, and regenerating would throw that away.
    if (segment.spans.empty()) return;
    segment.pfml = annotate_to_pfml(segment.text, segment.spans, segment.words,
                                    segment.overrides, nullptr);
}

std::string language_at(const Segment & segment, std::size_t position) {
    for (const LangSpan & span : segment.spans) {
        if (position >= span.begin && position < span.end) return span.language;
    }
    return std::string();
}

void set_override(Segment & segment, std::size_t begin, std::size_t end,
                  const std::string & language, const std::string & script,
                  const std::vector<std::string> & phonemes) {
    if (begin >= end || end > segment.text.size() || phonemes.empty()) return;

    std::vector<Override> kept;
    kept.reserve(segment.overrides.size() + 1);
    for (const Override & override_ : segment.overrides) {
        if (override_.end <= begin || override_.begin >= end) kept.push_back(override_);
    }
    Override pinned;
    pinned.begin    = begin;
    pinned.end      = end;
    pinned.language = language;
    pinned.script   = script;
    pinned.phonemes = phonemes;
    kept.push_back(std::move(pinned));

    std::sort(kept.begin(), kept.end(), [](const Override & a, const Override & b) {
        return a.begin < b.begin;
    });
    segment.overrides = std::move(kept);
    rebuild_pfml(segment);
}

void insert_phoneme(Segment & segment, std::size_t position,
                    const std::vector<std::string> & phonemes) {
    if (position > segment.text.size() || phonemes.empty()) return;
    Override inserted;
    inserted.begin    = position;
    inserted.end      = position;
    inserted.phonemes = phonemes;
    segment.overrides.push_back(std::move(inserted));
    rebuild_pfml(segment);
}

void remove_override_at(Segment & segment, std::size_t position) {
    std::vector<Override> kept;
    kept.reserve(segment.overrides.size());
    for (const Override & override_ : segment.overrides) {
        if (override_.begin <= position && position <= override_.end) continue;
        kept.push_back(override_);
    }
    if (kept.size() == segment.overrides.size()) return;
    segment.overrides = std::move(kept);
    rebuild_pfml(segment);
}

void remove_overrides_in(Segment & segment, std::size_t begin, std::size_t end) {
    if (begin > end) return;
    std::vector<Override> kept;
    kept.reserve(segment.overrides.size());
    for (const Override & override_ : segment.overrides) {
        const bool overlaps = override_.begin < end && override_.end > begin;
        const bool at_point = override_.inserts() && override_.begin >= begin &&
                              override_.begin <= end;
        if (overlaps || at_point) continue;
        kept.push_back(override_);
    }
    if (kept.size() == segment.overrides.size()) return;
    segment.overrides = std::move(kept);
    rebuild_pfml(segment);
}

void clear_overrides(Segment & segment) {
    if (segment.overrides.empty()) return;
    segment.overrides.clear();
    rebuild_pfml(segment);
}

void add_word(Segment & segment, std::size_t begin, std::size_t end) {
    if (begin >= end || end > segment.text.size()) return;

    std::vector<WordBoundary> kept;
    kept.reserve(segment.words.size() + 1);
    for (const WordBoundary & word : segment.words) {
        if (word.end <= begin || word.begin >= end) kept.push_back(word);
    }
    kept.push_back(WordBoundary{ begin, end });
    std::sort(kept.begin(), kept.end(),
              [](const WordBoundary & a, const WordBoundary & b) { return a.begin < b.begin; });
    segment.words = std::move(kept);
    rebuild_pfml(segment);
}

void remove_word_at(Segment & segment, std::size_t position) {
    std::vector<WordBoundary> kept;
    kept.reserve(segment.words.size());
    for (const WordBoundary & word : segment.words) {
        if (word.begin <= position && position <= word.end) continue;
        kept.push_back(word);
    }
    if (kept.size() == segment.words.size()) return;
    segment.words = std::move(kept);
    rebuild_pfml(segment);
}

void clear_words(Segment & segment) {
    if (segment.words.empty()) return;
    segment.words.clear();
    rebuild_pfml(segment);
}

void set_span_language(Segment & segment, std::size_t begin, std::size_t end,
                       const std::string & language) {
    if (begin >= end || end > segment.text.size()) return;
    if (segment.spans.empty()) detect_spans(segment);

    std::vector<LangSpan> updated;
    updated.reserve(segment.spans.size() + 2);
    for (const LangSpan & span : segment.spans) {
        if (span.end <= begin || span.begin >= end) {
            updated.push_back(span);   // no overlap: untouched
            continue;
        }
        // Overlaps: keep whatever sticks out on either side of the new range,
        // so the spans still tile the text.
        if (span.begin < begin) {
            LangSpan head = span;
            head.end = begin;
            updated.push_back(head);
        }
        if (span.end > end) {
            LangSpan tail = span;
            tail.begin = end;
            updated.push_back(tail);
        }
    }

    LangSpan pinned;
    pinned.begin     = begin;
    pinned.end       = end;
    pinned.language  = language;
    pinned.manual    = true;
    pinned.ambiguous = language.empty();
    updated.push_back(pinned);

    std::sort(updated.begin(), updated.end(),
              [](const LangSpan & a, const LangSpan & b) { return a.begin < b.begin; });
    segment.spans = std::move(updated);
    rebuild_pfml(segment);
}

}  // namespace maxlabel
