// maxlabel_cli — the headless half of MaxLabel.
//
// Enough to drive the M0 data path end to end without Qt: scan a directory,
// inspect what was found, validate a PFML fragment, and write one back.

#include "maxlabel/core.h"
#include "maxlabel/g2p_context.h"
#include "maxlabel/language.h"
#include "maxlabel/models.h"
#include "maxlabel/vocabulary.h"

#include "tifa_ggml/g2p.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void usage() {
    std::cout <<
        "maxlabel_cli — PFML project tool\n"
        "\n"
        "  scan <dir>                 list the segments found in a directory\n"
        "  show <dir> <id>            print one segment's PFML\n"
        "  validate <file.pfml>       parse-check a PFML fragment\n"
        "  set <dir> <id> [file]      write <id>.pfml (reads stdin when no file)\n"
        "  langs <file> [-l <lang>]   split a transcript by language, print the PFML\n"
        "  phoneme <symbol> [-l zh,en] [--vocab <file>]\n"
        "                             check a phoneme against the model vocabulary\n"
        "  candidates <text> --g2p <config.json> [--dicts <dir>] [-l zh,en]\n"
        "                             list the pronunciations the pipeline offers\n"
        "\n"
        "A segment is the set of files sharing a basename: song.wav, song.pfml,\n"
        "song.txt, song.lab, song.json.  Text precedence is .pfml > .txt > .lab,\n"
        "matching the aligner.\n"
        "\n"
        "`langs` settles kana/hangul/latin by script.  Han is shared by Chinese,\n"
        "Japanese and Cantonese, so a Han run takes -l when given and is otherwise\n"
        "reported as undetermined rather than guessed at.\n";
}

std::string read_stream(std::istream & in) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string trim(std::string value) {
    std::size_t b = 0;
    std::size_t e = value.size();
    while (b < e && std::isspace(static_cast<unsigned char>(value[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1]))) --e;
    return value.substr(b, e - b);
}

std::vector<std::string> split_commas(const std::string & value) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : value) {
        if (c == ',') {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) out.push_back(trimmed);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    const std::string trimmed = trim(current);
    if (!trimmed.empty()) out.push_back(trimmed);
    return out;
}

const maxlabel::Segment * find(const maxlabel::Project & project, const std::string & id) {
    for (const maxlabel::Segment & segment : project.segments) {
        if (segment.id == id) return &segment;
    }
    return nullptr;
}

int command_scan(const std::string & directory) {
    const maxlabel::Project project = maxlabel::scan(directory);
    std::size_t invalid = 0;
    std::cout << project.segments.size() << " segment(s) in " << project.directory << "\n";
    for (const maxlabel::Segment & segment : project.segments) {
        if (!segment.pfml_valid) ++invalid;
        std::cout << "  " << segment.id
                  << "  audio=" << (segment.audio_path.empty() ? "-" : "yes")
                  << "  source=" << maxlabel::to_string(segment.source)
                  << "  reviewed=" << (segment.reviewed ? "yes" : "no")
                  << "  pfml=" << (segment.pfml_valid ? "ok" : "INVALID");
        if (!segment.pfml_valid) std::cout << "  (" << segment.error << ")";
        std::cout << "\n";
    }
    if (invalid != 0) {
        std::cerr << invalid << " segment(s) have PFML the aligner would skip\n";
        return 1;
    }
    return 0;
}

int command_show(const std::string & directory, const std::string & id) {
    const maxlabel::Project project = maxlabel::scan(directory);
    const maxlabel::Segment * segment = find(project, id);
    if (segment == nullptr) {
        std::cerr << "no segment '" << id << "' in " << directory << "\n";
        return 1;
    }
    std::cout << segment->pfml << "\n";
    return segment->pfml_valid ? 0 : 1;
}

int command_validate(const std::string & path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot read " << path << "\n";
        return 1;
    }
    const std::string text = read_stream(in);
    try {
        maxlabel::validate(text);
    } catch (const std::exception & error) {
        std::cerr << path << ": " << error.what() << "\n";
        return 1;
    }
    std::cout << path << ": ok\n";
    return 0;
}

int command_set(const std::string & directory, const std::string & id,
                const std::vector<std::string> & rest) {
    const maxlabel::Project project = maxlabel::scan(directory);
    const maxlabel::Segment * found = find(project, id);
    if (found == nullptr) {
        std::cerr << "no segment '" << id << "' in " << directory << "\n";
        return 1;
    }
    maxlabel::Segment segment = *found;

    std::string pfml;
    if (!rest.empty()) {
        std::ifstream in(rest[0], std::ios::binary);
        if (!in) {
            std::cerr << "cannot read " << rest[0] << "\n";
            return 1;
        }
        pfml = read_stream(in);
    } else {
        pfml = read_stream(std::cin);
    }
    segment.pfml = trim(pfml);
    maxlabel::save(segment);
    std::cout << "wrote " << segment.id << ".pfml\n";
    return 0;
}

int command_langs(const std::string & path, const std::string & default_language) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot read " << path << "\n";
        return 1;
    }
    const std::string text = trim(read_stream(in));
    const std::vector<maxlabel::LangSpan> spans =
        maxlabel::detect_languages(text, default_language);

    std::cout << spans.size() << " span(s)\n";
    for (const maxlabel::LangSpan & span : spans) {
        std::cout << "  [" << span.begin << "," << span.end << ")  "
                  << (span.language.empty() ? "(undetermined)" : span.language)
                  << (span.manual ? "  manual" : "")
                  << (span.ambiguous ? "  ambiguous" : "")
                  << "  " << text.substr(span.begin, span.end - span.begin) << "\n";
    }

    const std::string pfml = maxlabel::spans_to_pfml(text, spans);
    std::cout << "\npfml:\n" << pfml << "\n";
    try {
        maxlabel::validate(pfml);
    } catch (const std::exception & error) {
        std::cerr << "\nproduced PFML does not parse: " << error.what() << "\n";
        return 1;
    }
    if (maxlabel::has_undetermined(spans)) {
        std::cerr << "\nwarning: undetermined span(s) — the aligner needs a language there\n";
        return 1;
    }
    return 0;
}

int command_phoneme(const std::string & symbol, const std::string & vocabulary_path,
                    const std::vector<std::string> & languages) {
    maxlabel::Vocabulary vocabulary;
    if (!vocabulary_path.empty()) {
        std::string error;
        if (!vocabulary.load(vocabulary_path, &error)) {
            std::cerr << error << "\n";
            return 1;
        }
    }

    const maxlabel::PhonemeCheck result = maxlabel::check_phoneme(vocabulary, symbol, languages);
    if (!result.checkable) {
        std::cerr << "no vocabulary loaded (--vocab <symbols.txt>) — cannot check '"
                  << symbol << "'\n";
        return 2;
    }
    if (result.known) {
        std::cout << symbol << ": resolves\n";
        return 0;
    }
    std::cerr << symbol << ": not in the model vocabulary — the aligner would not resolve it\n";
    return 1;
}

int command_candidates(const std::string & text, const std::string & g2p_config,
                       const std::string & dictionaries,
                       const std::vector<std::string> & languages) {
    if (g2p_config.empty()) {
        std::cerr << "candidates needs --g2p <config.json> [--dicts <dir>]\n";
        return 2;
    }
    maxlabel::G2PContext g2p;
    std::string error;
    if (!g2p.load(g2p_config, dictionaries, &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    const std::vector<tifa_ggml::G2PWordCandidates> words = g2p.candidates(text, languages);
    if (words.empty()) {
        std::cout << "no candidates (nothing claims this text)\n";
        return 1;
    }
    for (const tifa_ggml::G2PWordCandidates & word : words) {
        std::cout << word.text;
        if (!word.language.empty()) std::cout << "  [" << word.language << "]";
        std::cout << "\n";
        for (const tifa_ggml::G2PCandidate & candidate : word.candidates) {
            std::cout << "  reading " << candidate.reading << " / path " << candidate.path
                      << "   script=" << candidate.script << "   phonemes=";
            for (std::size_t i = 0; i < candidate.phonemes.size(); ++i) {
                if (i != 0) std::cout << ' ';
                std::cout << candidate.phonemes[i];
            }
            std::cout << "\n";
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char ** argv) {
#ifdef _WIN32
    // Without this the console mangles every non-ASCII byte, which makes a
    // tool whose whole job is CJK transcripts unreadable.
    SetConsoleOutputCP(CP_UTF8);
#endif
    // A release ships its data files beside the binary; a development build
    // keeps the compiled-in default, which is the source tree.
    maxlabel::use_bundled_models(argc > 0 ? argv[0] : "");

    const std::vector<std::string> args(argv + 1, argv + argc);

    // A global option: where the data files (the BudouX models, the detector
    // weights) are looked up.
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--models") maxlabel::set_model_directory(args[i + 1]);
    }

    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage();
        return args.empty() ? 2 : 0;
    }
    const std::string & command = args[0];
    try {
        if (command == "scan" && args.size() == 2) return command_scan(args[1]);
        if (command == "show" && args.size() == 3) return command_show(args[1], args[2]);
        if (command == "validate" && args.size() == 2) return command_validate(args[1]);
        if (command == "set" && args.size() >= 3) {
            return command_set(args[1], args[2],
                               std::vector<std::string>(args.begin() + 3, args.end()));
        }
        if (command == "langs" && args.size() >= 2) {
            std::string default_language;
            for (std::size_t i = 2; i + 1 < args.size(); ++i) {
                if (args[i] == "-l") default_language = args[i + 1];
            }
            return command_langs(args[1], default_language);
        }
        if (command == "phoneme" && args.size() >= 2) {
            std::string vocabulary_path;
            std::vector<std::string> languages;
            for (std::size_t i = 2; i + 1 < args.size(); ++i) {
                if (args[i] == "--vocab") vocabulary_path = args[i + 1];
                if (args[i] == "-l") languages = split_commas(args[i + 1]);
            }
            return command_phoneme(args[1], vocabulary_path, languages);
        }
        if (command == "candidates" && args.size() >= 2) {
            std::string g2p_config;
            std::string dictionaries;
            std::vector<std::string> languages;
            for (std::size_t i = 2; i + 1 < args.size(); ++i) {
                if (args[i] == "--g2p") g2p_config = args[i + 1];
                if (args[i] == "--dicts") dictionaries = args[i + 1];
                if (args[i] == "-l") languages = split_commas(args[i + 1]);
            }
            return command_candidates(args[1], g2p_config, dictionaries, languages);
        }
    } catch (const std::exception & error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    usage();
    return 2;
}
