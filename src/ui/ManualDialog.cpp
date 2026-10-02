// The manual — see ManualDialog.h.

#include "ManualDialog.h"

#include "DialogButtons.h"

#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

// One row of the shortcut table.
QString row(const QString & keys, const QString & what) {
    return QStringLiteral("<tr><td class=\"key\">%1</td><td>%2</td></tr>")
        .arg(keys.toHtmlEscaped(), what);
}

QString heading(const QString & text) {
    return QStringLiteral("<h3>%1</h3>").arg(text.toHtmlEscaped());
}

QString para(const QString & text) {
    return QStringLiteral("<p>%1</p>").arg(text);
}

}  // namespace

ManualDialog::ManualDialog(QWidget * parent) : QDialog(parent) {
    setWindowTitle(tr("Manual"));
    resize(660, 640);

    auto * view = new QTextBrowser(this);
    view->setOpenExternalLinks(true);

    QString html;
    html += QStringLiteral(
        "<style>"
        "body { color: #cccccc; background: #1e1e1e; font-family: 'Segoe UI', "
        "'Microsoft YaHei UI', sans-serif; font-size: 10pt; }"
        "h3 { color: #E91E63; margin-top: 18px; margin-bottom: 4px; }"
        "p  { margin: 6px 0; }"
        "table { border-collapse: collapse; margin: 6px 0; }"
        "td { padding: 2px 10px 2px 0; vertical-align: top; }"
        "td.key { color: #00BCD4; font-family: Consolas, monospace; "
        "white-space: nowrap; }"
        "code { color: #ffb300; font-family: Consolas, monospace; }"
        "li { margin: 2px 0; }"
        "</style>");

    html += heading(tr("What this is"));
    html += para(tr("MaxLabel sits before the aligner: it takes the transcript of each "
                    "segment, says which language each run is in, fixes the word "
                    "boundaries and pins the pronunciations the dictionary gets wrong, "
                    "and writes the PFML the aligner reads. Text first — a segment with "
                    "no recording is a normal case, not a broken one, and the audio "
                    "panes get out of the way when there is none."));

    html += heading(tr("The strip is the main way in"));
    html += para(tr("The line is laid out as one block per character (per word for "
                    "Latin). Everything else is done from those blocks."));
    html += QStringLiteral("<table>");
    html += row(tr("Click"), tr("Select that block."));
    html += row(tr("Drag"), tr("Select the run between two blocks. A word is usually "
                               "longer than one character, so this is how a word gets "
                               "marked."));
    html += row(tr("Double-click"), tr("Open the pronunciation dialog for that block. "
                                       "On a <code>+tag</code> it opens that inserted "
                                       "sound for editing, and <em>Remove</em> in the "
                                       "dialog takes it away."));
    html += QStringLiteral("</table>");
    html += para(tr("A block carries its state: the reading underneath is what is pinned "
                    "to it, a cyan bar along the bottom means it is a fixed word, and a "
                    "dotted outline means the pronunciation was written by hand — red "
                    "when the phoneme is not in the vocabulary."));

    html += heading(tr("Annotating"));
    html += QStringLiteral("<table>");
    html += row(tr("1 2 3 4 5"), tr("Set the language of the selection: Chinese, "
                                    "Japanese, English, Korean, Cantonese. The letter "
                                    "keys work anywhere, and each button carries the "
                                    "colour that language takes in the text."));
    html += row(tr("W"), tr("Fix the selection as one word — or take that mark back if "
                            "it already is exactly that word."));
    html += row(tr("P"), tr("Pin a pronunciation. The dictionary's candidates are "
                            "listed — the dictionaries ship with the tool; pick one or "
                            "type your own. <em>Remove</em> in that dialog takes a "
                            "written pronunciation off again."));
    html += row(tr("I"), tr("Insert a discrete sound at the cursor — a nasal pad the "
                            "singer added, a breath. It is not part of the text, so it "
                            "gets a <code>+tag</code> above the line instead of a "
                            "character. Inserting again at the same place replaces it "
                            "rather than stacking a second copy, and double-clicking "
                            "the tag opens it to change or remove."));
    html += row(tr("R"), tr("Re-split the languages from scratch. Only the automatic "
                            "spans are thrown away; what you decided by hand stays."));
    html += QStringLiteral("</table>");
    html += para(tr("A later mark wins over the range it names. Marking <code>天气</code> "
                    "inside an existing <code>今天天气</code> leaves <code>今天</code> as "
                    "its own word rather than losing it — and repeating a mark on the "
                    "same range takes it back, so nothing needs a separate erase "
                    "command."));

    html += heading(tr("Audio"));
    html += QStringLiteral("<table>");
    html += row(tr("Ctrl+Space"), tr("Play the selection, or the whole file."));
    html += row(tr("Space  B"), tr("Play, once the audio pane has the focus."));
    html += row(tr("H"), tr("Stop."));
    html += row(tr("Q  W"), tr("Back / forward half a second."));
    html += row(tr("Left  Right"), tr("The same, from the arrow keys."));
    html += row(tr("Click  Drag"), tr("Move the playhead / select a range."));
    html += QStringLiteral("</table>");
    html += para(tr("The space bar keeps working as a space in the text panes; the "
                    "audio keys only take over once that pane has the focus."));

    html += heading(tr("The PFML pane is editable"));
    html += para(tr("The pane at the bottom is what gets written, and it can be typed "
                    "in directly. What you type there is read back into the text above "
                    "— so the two can never disagree. A fragment that does not parse "
                    "changes nothing: the pane turns red, the panes derived from it go "
                    "dim rather than showing a stale reading, and the reason is in the "
                    "status bar."));

    html += heading(tr("Files"));
    html += para(tr("A segment is the set of files sharing a basename. The transcript "
                    "comes from the highest of <code>.pfml</code>, <code>.txt</code>, "
                    "<code>.lab</code> — the same precedence the aligner uses. Text is "
                    "read from <code>.pfml</code> rather than shown as markup, so "
                    "editing a file this tool wrote does not corrupt it."));
    html += para(tr("Every change is written immediately, so nothing is pending and "
                    "switching segments cannot lose work. The list marks a segment "
                    "whose PFML is not on disk yet with <code>*</code>."));

    html += heading(tr("Undo"));
    html += QStringLiteral("<table>");
    html += row(tr("Ctrl+Z"), tr("Undo."));
    html += row(tr("Ctrl+Shift+Z  Ctrl+Y"), tr("Redo."));
    html += QStringLiteral("</table>");
    html += para(tr("One history for the whole window, covering text, languages, words, "
                    "pronunciations and PFML edits alike — a text pane with its own undo "
                    "stack would silently ignore every annotation."));

    view->setHtml(html);

    auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    maxlabel::ui::localise_buttons(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto * layout = new QVBoxLayout(this);
    layout->addWidget(view);
    layout->addWidget(buttons);
}
