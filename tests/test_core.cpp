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
    // The text is the markup-stripped lyric: the editor edits text, not markup.
    check(other != nullptr && other->text == "重" && other->text != other->pfml,
          "other: the text is the lyric, not the markup");

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
    // A project with no audio at all.  Text-first means this is a complete
    // project rather than a degraded one: everything works, and the audio is
    // simply absent.
    {
        const fs::path dir = fs::temp_directory_path() / "maxlabel_text_only";
        fs::remove_all(dir);
        fs::create_directories(dir);
        write_file(dir / "a.txt", "今天天气不错 I love you");
        write_file(dir / "b.txt", "東京");

        const maxlabel::Project project = maxlabel::scan(dir.string());
        check(project.segments.size() == 2, "text-only: both segments found");
        for (const maxlabel::Segment & segment : project.segments) {
            check(segment.audio_path.empty(), "text-only: no audio, as written");
            check(!segment.spans.empty(), "text-only: languages detected");
            check(segment.pfml_valid, "text-only: the PFML is valid");
        }

        // Saving does not need the audio either — it goes to the segment's own
        // directory, which it knows without being told.
        maxlabel::Segment edited = project.segments.front();
        maxlabel::save(edited);
        check(fs::exists(dir / "a.pfml"), "text-only: saving works without audio");

        const maxlabel::Project reloaded = maxlabel::scan(dir.string());
        check(reloaded.segments.size() == 2, "text-only: the rescan is unchanged");
        if (!reloaded.segments.empty()) {
            check(reloaded.segments.front().source == maxlabel::TextSource::Pfml,
                  "text-only: the written PFML takes precedence on the rescan");
        }
        fs::remove_all(dir);
    }

    // Marks are overlap-resolved, and the same selection takes its own mark
    // back.  Both halves matter: a mark that cannot be taken back is one the
    // author has to work around, and a later mark that silently deletes an
    // earlier one loses work they did deliberately.
    {
        maxlabel::Segment segment;
        segment.id = "marks";
        segment.directory = dir.string();
        segment.text = "今天天气不错";
        maxlabel::detect_spans(segment);

        // Byte offsets: one CJK character is three bytes.
        const std::size_t tianqi = 6;    // 天气 starts here
        const std::size_t bu     = 9;    // 不   starts here

        check(maxlabel::toggle_word(segment, 0, tianqi),
              "toggle_word marks when there was no mark");
        check(maxlabel::has_word(segment, 0, tianqi), "and the mark is there");
        check(!maxlabel::toggle_word(segment, 0, tianqi), "the same range unmarks");
        check(!maxlabel::has_word(segment, 0, tianqi), "and the mark is gone");

        // 今天天气 as one word, then 天气 as a word: the later mark wins over
        // the range it names, and 今天 survives as a boundary of its own.
        maxlabel::toggle_word(segment, 0, tianqi);
        maxlabel::toggle_word(segment, 6, 12);
        check(maxlabel::has_word(segment, 0, 6),
              "the part an earlier mark kept outside the new range survives");
        check(maxlabel::has_word(segment, 6, 12), "and the new mark is there");
        check(!maxlabel::has_word(segment, 0, 12),
              "while the range it covered is no longer one word");

        // A point on the seam between two words removes one of them, not both:
        // the one that starts there, since the offset names its beginning.
        maxlabel::remove_word_at(segment, 6);
        check(maxlabel::has_word(segment, 0, 6),
              "remove_word_at leaves the word ending there");
        check(!maxlabel::has_word(segment, 6, 12),
              "and takes the one starting there");

        // A pronunciation is a value, so an overlapping one is replaced whole
        // rather than trimmed into a value nobody wrote for the shorter range.
        maxlabel::set_override(segment, 0, 12, "", "jintian", { "j", "in" });
        maxlabel::set_override(segment, 6, 12, "", "tianqi", { "t", "ian" });
        check(!maxlabel::has_override(segment, 0, 12),
              "an overlapping pronunciation is replaced");
        check(maxlabel::has_override(segment, 6, 12),
              "by the one that was written later");

        // One sound per position: re-inserting does not stack copies.
        maxlabel::insert_phoneme(segment, bu, { "n" });
        check(maxlabel::has_insertion_at(segment, bu), "an inserted sound is there");
        maxlabel::insert_phoneme(segment, bu, { "m" });
        std::size_t insertions = 0;
        for (const maxlabel::Override & override_ : segment.overrides) {
            if (override_.inserts()) ++insertions;
        }
        check(insertions == 1, "inserting again at the same position replaces it");
        maxlabel::remove_override_at(segment, bu);
        check(!maxlabel::has_insertion_at(segment, bu), "and it can be taken back");

        // Whatever the sequence, the fragment still has to be valid PFML.
        bool valid = true;
        try {
            maxlabel::validate(segment.pfml);
        } catch (const std::exception &) {
            valid = false;
        }
        check(valid, "the fragment is still valid PFML after all of that");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
