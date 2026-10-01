// maxlabel_cli — the headless half of MaxLabel.
//
// Enough to drive the M0 data path end to end without Qt: scan a directory,
// inspect what was found, validate a PFML fragment, and write one back.

#include "maxlabel/core.h"

#include "tifa_ggml/g2p.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage() {
    std::cout <<
        "maxlabel_cli — PFML project tool\n"
        "\n"
        "  scan <dir>                 list the segments found in a directory\n"
        "  show <dir> <id>            print one segment's PFML\n"
        "  validate <file.pfml>       parse-check a PFML fragment\n"
        "  set <dir> <id> [file]      write <id>.pfml (reads stdin when no file)\n"
        "\n"
        "A segment is the set of files sharing a basename: song.wav, song.pfml,\n"
        "song.txt, song.lab, song.json.  Text precedence is .pfml > .txt > .lab,\n"
        "matching the aligner.\n";
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

}  // namespace

int main(int argc, char ** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
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
    } catch (const std::exception & error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    usage();
    return 2;
}
