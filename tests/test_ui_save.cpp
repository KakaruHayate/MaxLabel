// The one path the core tests cannot reach: an edit made in the editor, saved
// from the window, landing on disk as valid PFML.
//
// Everything below the window is covered — maxlabel::save is tested, the
// annotation model is tested — but the wiring between them is not, and that
// wiring is the tool's primary output.  So this drives the real window through
// the real slot, offscreen.

#include "Icons.h"
#include "Resources.h"
#include "MainWindow.h"

#include "maxlabel/core.h"

#include <QApplication>
#include <QFile>
#include <QFile>
#include <QListWidget>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

static std::string read_file(const QString & path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

int main(int argc, char ** argv) {
    // No display on a build machine, and none needed: the window is exercised
    // through its own methods, not through the platform.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    maxlabel::ui::init_resources();
    QApplication application(argc, argv);

    // The theme and the icons live in resources.qrc, compiled into the
    // maxlabel_ui static library.  A static library's resource initialiser has
    // no referenced symbol, so the linker drops it unless main() forces it in —
    // and the symptom is not a crash, it is an unstyled window with no icons,
    // which only a human looking at a screenshot notices.  So it is asserted.
    check(QFile::exists(QStringLiteral(":/theme.qss")), "the theme resource is present");
    check(QFile::exists(QStringLiteral(":/icons/play.svg")), "the icon resources are present");
    check(!maxlabel::ui::icon(QStringLiteral("play")).isNull(),
          "an icon actually renders (the SVG renderer is linked and working)");

    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cout << "FAIL: no temporary directory\n";
        return 1;
    }
    {
        std::ofstream out((directory.path() + "/line.txt").toStdString(), std::ios::binary);
        out << "hello world";
    }

    MainWindow window;
    window.loadDirectory(directory.path());

    auto * editor = window.findChild<QPlainTextEdit *>(QStringLiteral("editor"));
    auto * list = window.findChild<QListWidget *>(QStringLiteral("segmentList"));
    check(editor != nullptr, "the window has an editor");
    check(list != nullptr && list->count() == 1, "the window loaded the one segment");
    if (editor == nullptr || list == nullptr) {
        std::cout << "\nFAILURES\n";
        return 1;
    }

    check(editor->toPlainText() == QStringLiteral("hello world"),
          "the editor shows the segment's text");

    // Edit it the way a person would, then save.
    editor->setPlainText(QStringLiteral("今天天气不错 I love you"));
    check(QMetaObject::invokeMethod(&window, "saveCurrent"), "saveCurrent is invocable");

    const QString pfml_path = directory.path() + "/line.pfml";
    check(QFile::exists(pfml_path), "saving wrote line.pfml");

    const std::string written = read_file(pfml_path);
    check(written.find("<scope language=\"zh\">") != std::string::npos,
          "the written PFML carries the detected Chinese run");
    check(written.find("<scope language=\"en\">") != std::string::npos,
          "and the English one");

    bool parses = true;
    try {
        maxlabel::validate(written);
    } catch (const std::exception &) {
        parses = false;
    }
    check(parses, "the written PFML parses");

    // It has to come back on a rescan, read from the file rather than from
    // whatever the window still has in memory.
    const maxlabel::Project reloaded = maxlabel::scan(directory.path().toStdString());
    check(reloaded.segments.size() == 1, "a rescan finds one segment");
    if (!reloaded.segments.empty()) {
        check(reloaded.segments.front().source == maxlabel::TextSource::Pfml,
              "the rescan reads the PFML, not the .txt it started from");
        check(reloaded.segments.front().pfml_valid, "and it is valid");
    }

    // Editing a segment that already has PFML must not overwrite it with a
    // regeneration that dropped what was there.
    {
        editor->setPlainText(QStringLiteral("changed"));
        QMetaObject::invokeMethod(&window, "saveCurrent");
        const std::string second = read_file(pfml_path);
        check(second.find("changed") != std::string::npos, "a second edit is written");
        check(second.find("今天天气不错") == std::string::npos,
              "and replaces the first, rather than accumulating");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
