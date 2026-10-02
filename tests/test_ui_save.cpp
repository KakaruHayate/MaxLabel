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
#include "RunStrip.h"

#include "maxlabel/core.h"

#include <QApplication>
#include <QFile>
#include <QFile>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextCursor>
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

    // Undo has to cover the annotations, not just the text: setting a run's
    // language is a model change with no keystroke behind it, and it was the
    // case that went wrong first.
    {
        std::cout << "      [step] selecting all" << std::endl;
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(0);
        cursor.setPosition(editor->toPlainText().size(), QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);

        std::cout << "      [step] invoking setSelectionLanguage" << std::endl;
        check(QMetaObject::invokeMethod(&window, "setSelectionLanguage",
                                        Q_ARG(QString, QStringLiteral("ko"))),
              "setSelectionLanguage is invocable");
        const std::string marked = read_file(pfml_path);
        std::cout << "      marked:  " << marked << "\n";
        std::cout << "      written: " << written << "\n";
        check(marked.find("language=\"ko\"") != std::string::npos,
              "setting the language changed the PFML");
        check(marked.find("language=\"zh\"") == std::string::npos,
              "and replaced the language that was there");

        check(QMetaObject::invokeMethod(&window, "undo"), "undo is invocable");
        const std::string undone = read_file(pfml_path);
        check(undone.find("language=\"ko\"") == std::string::npos,
              "undo takes the language change back");
        check(undone.find("language=\"zh\"") != std::string::npos,
              "and restores what was there before");
        check(undone == written, "undo restores the file byte for byte");

        check(QMetaObject::invokeMethod(&window, "redo"), "redo is invocable");
        check(read_file(pfml_path) == marked, "redo puts it back");
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

    // The strip is the interaction the tool is built around: a block per
    // character, click it, and the rest of the window agrees on what is
    // selected.  It is drawn rather than laid out by widgets, so nothing about
    // it is checked by the compiler or by the core tests — a mis-hit block or
    // a mapping that is off by one character would look fine and annotate the
    // wrong thing.
    {
        editor->setPlainText(QStringLiteral("今天天气不错 I love you"));
        QMetaObject::invokeMethod(&window, "saveCurrent");

        auto * strip = window.findChild<RunStrip *>();
        check(strip != nullptr, "the window has the character strip");
        if (strip != nullptr) {
            // The first block is always laid out at the top-left corner.
            const QPointF at(10, 10);
            QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(strip, &press);

            check(editor->textCursor().selectedText() == QStringLiteral("今"),
                  "clicking the first block selects exactly the first character");

            // And an annotation made from that click lands on that character:
            // the whole point of being able to select it without dragging.
            check(QMetaObject::invokeMethod(&window, "setSelectionLanguage",
                                            Q_ARG(QString, QStringLiteral("ja"))),
                  "setSelectionLanguage is invocable from a strip selection");
            const std::string partial = read_file(pfml_path);
            std::cout << "      partial: " << partial << "\n";
            check(partial.find("language=\"ja\">今<") != std::string::npos,
                  "the clicked character alone became Japanese");

            QMetaObject::invokeMethod(&window, "undo");
            const std::string restored = read_file(pfml_path);
            check(restored.find("language=\"ja\"") == std::string::npos,
                  "undo takes the partial change back");

            // A word is longer than one character, so a selection has to be
            // able to span blocks.  `chat` has no name for that, so it is a
            // drag — and a drag that only ever re-selects the first block is
            // exactly the bug that would make `<word>` unusable and look fine.
            const QPointF from(10, 10);
            QMouseEvent dragPress(QEvent::MouseButtonPress, from, from, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(strip, &dragPress);

            QString dragged;
            const int row = strip->rowHeight();
            for (int y = 10; y < row * 2 && dragged.size() < 2; y += std::max(1, row / 2)) {
                for (int x = 12; x < 400 && dragged.size() < 2; x += 2) {
                    const QPointF at(x, y);
                    QMouseEvent move(QEvent::MouseMove, at, at, Qt::NoButton,
                                     Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(strip, &move);
                    dragged = editor->textCursor().selectedText();
                }
            }
            const QPointF to(400, 10);
            QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(strip, &release);

            check(dragged == QStringLiteral("今天"),
                  "dragging across blocks selects the run between them");

            check(QMetaObject::invokeMethod(&window, "markWord"),
                  "markWord is invocable on a dragged selection");
            check(QMetaObject::invokeMethod(&window, "undo"), "undo after the drag");
        }
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
