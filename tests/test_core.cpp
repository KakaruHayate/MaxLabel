// Self-checking test for the MaxLabel core: directory pairing, sidecar import
// precedence, PFML validation and export.  No Qt, no model, no network.

#include "maxlabel/core.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

static void write_file(const fs::path & path, const std::string & text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static const maxlabel::Segment * find(const maxlabel::Project & project, const std::string & id) {
    for (const maxlabel::Segment & segment : project.segments) {
        if (segment.id == id) return &segment;
    }
    return nullptr;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "maxlabel_core_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // A MinLabel-era segment: audio, a syllable line and the project sidecar.
    write_file(dir / "song.wav", "");
    write_file(dir / "song.lab", "gan shou ting\n");
    write_file(dir / "song.json",
               R"({"lab":"gan shou ting","lab_without_tone":"gan shou ting","isCheck":true})");

    // A segment that already has PFML, with no audio.
    write_file(dir / "other.pfml",
               "<word text=\"重\" language=\"zh\" script=\"zhong\" phonemes=\"zh ong\"/>");

    // Audio only: nothing to read, the text has to be typed.
    write_file(dir / "bare.wav", "");

    // A malformed fragment must be reported, not silently accepted.
    write_file(dir / "broken.pfml", "<word text=\"x\"><bogus/></word>");

    const maxlabel::Project project = maxlabel::scan(dir.string());
    check(project.segments.size() == 4, "scan: four segments paired by basename");

    const maxlabel::Segment * song = find(project, "song");
    check(song != nullptr, "scan: song found");
    if (song != nullptr) {
        check(!song->audio_path.empty(), "song: audio paired");
        check(song->source == maxlabel::TextSource::Lab, "song: text from .lab");
        check(song->text == "gan shou ting", "song: text read");
        check(song->lab == "gan shou ting", "song: legacy lab kept");
        check(song->reviewed, "song: isCheck mapped to reviewed");
        check(song->pfml_valid, "song: escaped text is valid PFML");
    }

    const maxlabel::Segment * other = find(project, "other");
    check(other != nullptr && other->source == maxlabel::TextSource::Pfml,
          "other: text from .pfml");
    check(other != nullptr && other->audio_path.empty(), "other: no audio");
    check(other != nullptr && other->text == other->pfml, "other: pfml is the content");

    const maxlabel::Segment * bare = find(project, "bare");
    check(bare != nullptr && bare->source == maxlabel::TextSource::None,
          "bare: no text source");
    check(bare != nullptr && bare->pfml.empty(), "bare: empty content");
    check(bare != nullptr && bare->pfml_valid, "bare: nothing to be invalid");

    const maxlabel::Segment * broken = find(project, "broken");
    check(broken != nullptr && !broken->pfml_valid, "broken: invalid PFML flagged");
    check(broken != nullptr && !broken->error.empty(), "broken: reason recorded");

    // --- precedence: .pfml beats .lab ---------------------------------------
    write_file(dir / "song.pfml",
               "<scope language=\"zh\">你好</scope>");
    const maxlabel::Project after = maxlabel::scan(dir.string());
    const maxlabel::Segment * song2 = find(after, "song");
    check(song2 != nullptr && song2->source == maxlabel::TextSource::Pfml,
          "precedence: .pfml wins over .lab");

    // --- export -------------------------------------------------------------
    if (bare != nullptr) {
        maxlabel::Segment edited = *bare;
        edited.pfml = "<word text=\"hi\" language=\"en\" script=\"hi\" phonemes=\"HH AY\"/>";
        maxlabel::save(edited);
        const maxlabel::Project reloaded = maxlabel::scan(dir.string());
        const maxlabel::Segment * written = find(reloaded, "bare");
        check(written != nullptr && written->source == maxlabel::TextSource::Pfml,
              "export: written PFML is picked up on rescan");
        check(written != nullptr && written->pfml == edited.pfml,
              "export: content round-trips through the file");
    }

    // --- the export gate ----------------------------------------------------
    {
        maxlabel::Segment bad;
        bad.id   = "bad";
        bad.pfml = "<word text=\"x\"><bogus/></word>";
        bool threw = false;
        try {
            maxlabel::save(bad);
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw, "export gate: invalid PFML refuses to be written");
        check(!fs::exists(dir / "bad.pfml"), "export gate: nothing written on refusal");
    }

    // --- escaping -----------------------------------------------------------
    check(maxlabel::escape_text("a & b < c") == "a &amp; b &lt; c", "escape: entities");
    {
        bool threw = false;
        try {
            maxlabel::validate("<scope language=\"ja\">x</scope>");
        } catch (const std::exception &) {
            threw = true;
        }
        check(!threw, "validate: accepts a well-formed scope");
    }
    {
        bool threw = false;
        try {
            maxlabel::validate("<scope language=\"ja\">unclosed");
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw, "validate: rejects an unclosed scope");
    }

    fs::remove_all(dir);
    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
