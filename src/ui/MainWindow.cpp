#include "MainWindow.h"

#include "AudioPanel.h"
#include "PronunciationDialog.h"
#include "DialogButtons.h"
#include "LyricEditor.h"
#include "ManualDialog.h"
#include "RunStrip.h"

#include "Icons.h"

#include "maxlabel/g2p_config.h"
#include "maxlabel/import_pfml.h"
#include "maxlabel/languages.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <functional>

namespace {

// A burst of typing is one undo step, not one per character: the first
// keystroke of a burst anchors it and the rest do not add another.  A pause,
// or any structural edit, starts a new one.
constexpr qint64 kTypingGroupMs = 700;
constexpr std::size_t kMaxHistory = 100;
// How long a pause in typing counts as "stopped".
constexpr int kTypingSaveMs = 1000;
// How long a confirmation stays on the status line before the summary returns.
constexpr int kStatusHoldMs = 4000;

QString format_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = static_cast<int>(seconds * 1000.0);
    return QStringLiteral("%1:%2.%3")
        .arg(total / 60000)
        .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg((total / 100) % 10);
}

// A tint per language, chosen for a dark background: deep enough that the
// default light text stays readable on top, and far enough apart to tell at a
// glance.  (The first version of these was light — they had been picked
// against a light theme, and on the dark one they glared and hid their own
// text.)
//
// Hue alone is not enough for five: amber and orange sat 30 degrees apart and
// read as the same brown.  So they are spread around the wheel *and* given
// different lightness, which survives being shrunk to a 10-pixel swatch.
QColor colour_for(const std::string & language) {
    if (language == "zh")  return QColor(0x24, 0x47, 0x6F);   // blue
    if (language == "ja")  return QColor(0x5C, 0x26, 0x50);   // magenta
    if (language == "en")  return QColor(0x24, 0x53, 0x34);   // green
    if (language == "ko")  return QColor(0x6B, 0x5A, 0x15);   // gold: light, yellow
    if (language == "yue") return QColor(0x4F, 0x24, 0x18);   // red-brown: dark, ruddy
    return QColor();
}

// The annotation underlines.  They sit on top of a language tint, so they have
// to be light enough to read against one: the palette's secondary and a warm
// accent, and the error red for the one case that is a real problem.
// The name a language is shown under.  It lives here rather than in the
// language table because the core library has no Qt and therefore no tr(); a
// language the table gains that this does not know about falls back to its id,
// which is at least unambiguous.
QString language_name(const std::string & id) {
    if (id == "zh")  return QCoreApplication::translate("languages", "Chinese");
    if (id == "ja")  return QCoreApplication::translate("languages", "Japanese");
    if (id == "en")  return QCoreApplication::translate("languages", "English");
    if (id == "ko")  return QCoreApplication::translate("languages", "Korean");
    if (id == "yue") return QCoreApplication::translate("languages", "Cantonese");
    return QString::fromStdString(id);
}

// Icons rest at the text colour and go white where the accent is behind them,
// which is the same rule the buttons follow.
const QColor kIconColour(0xCC, 0xCC, 0xCC);
const QColor kIconOnAccent(0xFF, 0xFF, 0xFF);

const QColor kWordUnderline(0x00, 0xBC, 0xD4);      // secondary
const QColor kOverrideUnderline(0xFF, 0xB3, 0x00);  // warm: written by hand
const QColor kErrorUnderline(0xF4, 0x43, 0x36);     // the error colour

// Qt counts in UTF-16 code units; the span model counts UTF-8 bytes.  Cutting
// the UTF-8 encoding at a span boundary is safe because the detector only ever
// produces boundaries between code points.
std::size_t byte_offset_of(const QString & text, int utf16_position) {
    return static_cast<std::size_t>(text.left(utf16_position).toUtf8().size());
}

int utf16_offset_of(const QString & text, std::size_t byte_offset) {
    const QByteArray utf8 = text.toUtf8();
    if (byte_offset >= static_cast<std::size_t>(utf8.size())) return text.size();
    return QString::fromUtf8(utf8.left(static_cast<int>(byte_offset))).size();
}

bool has_manual_spans(const std::vector<maxlabel::LangSpan> & spans) {
    return std::any_of(spans.begin(), spans.end(),
                       [](const maxlabel::LangSpan & span) { return span.manual; });
}

// Whether any of these phonemes would fail to resolve, under the languages
// given.  An empty vocabulary cannot answer, so nothing is flagged — a tool
// that flags everything is as useless as one that flags nothing.
//
// The languages a phoneme is allowed to resolve under.
//
// The run's own language first.  When the run has not been decided yet, every
// language the segment uses: the aligner qualifies a bare phoneme with the
// language it is aligning as, so a phoneme that exists somewhere in this line
// is not the mistake this check is looking for.  Without the fallback, turning
// the check on by shipping a vocabulary would flag every phoneme in every
// undecided line, which reads as a broken tool rather than as a decision
// waiting to be made.
std::vector<std::string> phoneme_languages(const maxlabel::Segment & segment,
                                           std::size_t position) {
    const std::string language = maxlabel::language_at(segment, position);
    if (!language.empty()) return { language };

    std::vector<std::string> out;
    for (const maxlabel::LangSpan & span : segment.spans) {
        if (span.language.empty()) continue;
        if (std::find(out.begin(), out.end(), span.language) == out.end()) {
            out.push_back(span.language);
        }
    }
    return out;
}

bool has_unknown_phoneme(const maxlabel::Vocabulary & vocabulary,
                         const std::vector<std::string> & phonemes,
                         const std::vector<std::string> & languages) {
    for (const std::string & phoneme : phonemes) {
        if (!maxlabel::check_phoneme(vocabulary, phoneme, languages).known) return true;
    }
    return false;
}

}  // namespace

MainWindow::MainWindow(QWidget * parent) : QMainWindow(parent) {
    setWindowTitle(tr("MaxLabel"));
    resize(1280, 820);

    // --- actions ------------------------------------------------------------
    // The actions own the behaviour and the keyboard; the rail below is only
    // how they are drawn.  Keeping the two apart is what lets the buttons be
    // restyled without touching a shortcut, and what keeps a key working while
    // the editor has focus.
    const auto makeAction = [this](const QString & text, const QKeySequence & shortcut,
                                   const QString & tip) {
        auto * action = new QAction(text, this);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        if (!tip.isEmpty()) action->setToolTip(tip);
        addAction(action);   // window-wide
        return action;
    };

    QAction * openAction = makeAction(tr("Open Folder…"), QKeySequence::Open, QString());
    connect(openAction, &QAction::triggered, this, &MainWindow::openDirectory);

    prevAction_ = makeAction(tr("Previous"), QKeySequence(Qt::Key_Z), QString());
    connect(prevAction_, &QAction::triggered, this, &MainWindow::goPrevious);

    nextAction_ = makeAction(tr("Next"), QKeySequence(Qt::Key_X), QString());
    connect(nextAction_, &QAction::triggered, this, &MainWindow::goNext);

    saveAction_ = makeAction(tr("Save"), QKeySequence::Save, QString());
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrent);

    reSplitAction_ = makeAction(tr("Re-split Languages"), QKeySequence(Qt::Key_R),
                                tr("Discard the manual language marks and re-run detection"));
    connect(reSplitAction_, &QAction::triggered, this, &MainWindow::reSplit);

    QAction * markWordAction = makeAction(tr("Mark Word"), QKeySequence(Qt::Key_W),
                                          tr("Fix the selected run as one word"));
    connect(markWordAction, &QAction::triggered, this, &MainWindow::markWord);

    QAction * clearWordsAction = makeAction(tr("Clear Words"), QKeySequence(), QString());
    connect(clearWordsAction, &QAction::triggered, this, &MainWindow::clearWords);

    QAction * pinAction = makeAction(tr("Set Pronunciation…"), QKeySequence(Qt::Key_P),
                                     tr("Pin the final phonemes for the selection"));
    connect(pinAction, &QAction::triggered, this, &MainWindow::pinPronunciation);

    QAction * insertAction = makeAction(tr("Insert Phonemes…"), QKeySequence(Qt::Key_I),
                                        tr("Insert a sound at the cursor that is not a word"));
    connect(insertAction, &QAction::triggered, this, &MainWindow::insertPhonemes);

    QAction * clearOverridesAction = makeAction(tr("Clear Overrides"), QKeySequence(), QString());
    connect(clearOverridesAction, &QAction::triggered, this, &MainWindow::clearOverrides);

    undoAction_ = makeAction(tr("Undo"), QKeySequence::Undo, QString());
    connect(undoAction_, &QAction::triggered, this, &MainWindow::undo);
    redoAction_ = makeAction(tr("Redo"), QKeySequence::Redo, QString());
    // Ctrl+Shift+Z as well: it is what half the world reaches for.
    redoAction_->setShortcuts({ QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z")) });
    connect(redoAction_, &QAction::triggered, this, &MainWindow::redo);
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);

    // The non-lexical symbols the aligner always knows.  A nasal pad the singer
    // added is not one of these, but a breath is, and both are one click.
    QMenu * symbolMenu = new QMenu(this);
    for (const std::string & symbol : maxlabel::default_global_symbols()) {
        const QString name = QString::fromStdString(symbol);
        QAction * action = symbolMenu->addAction(name);
        connect(action, &QAction::triggered, this, [this, name]() {
            maxlabel::Segment * segment = currentSegment();
            if (segment == nullptr) return;
            const QTextCursor cursor = editor_->textCursor();
            const std::size_t at = byte_offset_of(editor_->toPlainText(), cursor.position());
            pushHistory(false);
            maxlabel::insert_phoneme(*segment, at, { name.toStdString() });
            commitEdit();
        });
    }

    // --- menus --------------------------------------------------------------
    // The rail is for the things you do constantly; the menu bar is where the
    // rest is findable, and where a keyboard shortcut is discoverable rather
    // than something you have to be told about.
    QMenu * fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction_);
    fileMenu->addSeparator();
    QAction * quitAction = fileMenu->addAction(tr("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu * editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    editMenu->addSeparator();
    editMenu->addAction(markWordAction);
    editMenu->addAction(clearWordsAction);
    editMenu->addAction(reSplitAction_);
    editMenu->addSeparator();
    editMenu->addAction(pinAction);
    editMenu->addAction(insertAction);
    editMenu->addAction(clearOverridesAction);

    QMenu * viewMenu = menuBar()->addMenu(tr("&View"));
    QAction * waveformAction = viewMenu->addAction(tr("Waveform"));
    waveformAction->setCheckable(true);
    waveformAction->setChecked(true);
    QAction * spectrumAction = viewMenu->addAction(tr("Spectrum"));
    spectrumAction->setCheckable(true);
    connect(waveformAction, &QAction::triggered, this, [this, waveformAction, spectrumAction]() {
        audio_->setSpectrumMode(false);
        waveformAction->setChecked(true);
        spectrumAction->setChecked(false);
    });
    connect(spectrumAction, &QAction::triggered, this, [this, waveformAction, spectrumAction]() {
        audio_->setSpectrumMode(true);
        waveformAction->setChecked(false);
        spectrumAction->setChecked(true);
    });
    viewMenu->addSeparator();
    viewMenu->addAction(prevAction_);
    viewMenu->addAction(nextAction_);

    QMenu * helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction * manualAction = helpMenu->addAction(tr("Manual"));
    manualAction->setShortcut(QKeySequence::HelpContents);   // F1
    addAction(manualAction);   // window-wide, so F1 works from any pane
    connect(manualAction, &QAction::triggered, this, [this]() {
        ManualDialog dialog(this);
        dialog.exec();
    });
    helpMenu->addSeparator();
    QAction * aboutAction = helpMenu->addAction(tr("About MaxLabel"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        maxlabel::ui::about(this, tr("About MaxLabel"),
                           tr("<b>MaxLabel</b> %1 — a PFML editor for the aligner.<br><br>"
                              "It turns a folder of audio and transcripts into the PFML "
                              "that TIFA reads.")
                               .arg(QStringLiteral(MAXLABEL_VERSION)));
    });

    // --- the rail -----------------------------------------------------------
    // Controls on the left, the document on the right, sections in upper case
    // and in the accent colour.  The reference tool's arrangement, and the
    // reason a dense editor does not read as a wall of grey buttons.
    // The rail scrolls: there are five sections in it and a short window would
    // otherwise cut the last one off with no way to reach it.
    auto * railScroll = new QScrollArea(this);
    railScroll->setWidgetResizable(true);
    railScroll->setFrameShape(QFrame::NoFrame);
    railScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget * rail = new QWidget;
    railScroll->setWidget(rail);
    QVBoxLayout * railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 8, 8, 8);
    railLayout->setSpacing(8);

    list_ = new QListWidget(rail);
    list_->setObjectName(QStringLiteral("segmentList"));
    list_->setMinimumHeight(120);
    connect(list_, &QListWidget::currentRowChanged, this, &MainWindow::onRowChanged);

    // Titled like every other pane: the list is where you are, and an untitled
    // box at the top of a rail reads as leftover space.
    auto * listBox = new QGroupBox(tr("SEGMENTS"), rail);
    auto * listLayout = new QVBoxLayout(listBox);
    listLayout->setContentsMargins(8, 8, 8, 8);
    listLayout->addWidget(list_);
    railLayout->addWidget(listBox, 1);

    const auto addRailButton = [](QGroupBox * box, QBoxLayout * layout, QAction * action,
                                  const QString & iconName = QString(), bool accent = false) {
        auto * button = new QPushButton(action->text(), box);
        if (!iconName.isEmpty()) {
            button->setIcon(maxlabel::ui::icon(iconName, kIconColour, 16));
            button->setIconSize(QSize(16, 16));
        }
        if (accent) button->setProperty("accent", true);
        if (!action->toolTip().isEmpty()) button->setToolTip(action->toolTip());
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        layout->addWidget(button);
        return button;
    };

    {
        auto * box = new QGroupBox(tr("PROJECT"), rail);
        auto * layout = new QVBoxLayout(box);
        layout->setSpacing(6);
        addRailButton(box, layout, openAction, QStringLiteral("folder"));
        addRailButton(box, layout, saveAction_, QStringLiteral("save"), /*accent=*/true);

        auto * steps = new QHBoxLayout();
        steps->setSpacing(6);
        auto * previous = new QPushButton(tr("‹ Previous"), box);
        previous->setIcon(maxlabel::ui::icon(QStringLiteral("previous"), kIconColour, 16));
        previous->setToolTip(tr("Z"));
        connect(previous, &QPushButton::clicked, prevAction_, &QAction::trigger);
        auto * next = new QPushButton(tr("Next ›"), box);
        next->setIcon(maxlabel::ui::icon(QStringLiteral("next"), kIconColour, 16));
        next->setToolTip(tr("X"));
        connect(next, &QPushButton::clicked, nextAction_, &QAction::trigger);
        steps->addWidget(previous);
        steps->addWidget(next);
        layout->addLayout(steps);

        auto * history = new QHBoxLayout();
        history->setSpacing(6);
        addRailButton(box, history, undoAction_, QStringLiteral("undo"));
        addRailButton(box, history, redoAction_, QStringLiteral("redo"));
        layout->addLayout(history);

        railLayout->addWidget(box);
    }

    {
        auto * box = new QGroupBox(tr("LANGUAGE OF SELECTION"), rail);
        auto * layout = new QGridLayout(box);
        layout->setSpacing(6);
        // Built from the language table, so supporting another language is one
        // entry there rather than an edit here.
        int column = 0;
        int row = 0;
        for (const maxlabel::LanguageInfo & language : maxlabel::LanguageTable::builtin().all()) {
            const QString id = QString::fromStdString(language.id);
            const QString name = language_name(language.id);
            // The button says the id — it is what goes into the PFML, and five
            // of them have to fit in a toolbar — while the tip carries the name
            // and the key, so the same label the rail shows is still findable.
            auto * action = makeAction(
                id, QKeySequence(),
                language.shortcut == '\0'
                    ? name
                    : QStringLiteral("%1 — %2").arg(name).arg(QChar(language.shortcut)));
            connect(action, &QAction::triggered, this,
                    [this, id]() { setSelectionLanguage(id); });
            if (language.shortcut != '\0') {
                action->setShortcut(QKeySequence(QString(QChar(language.shortcut))));
            }
            // The quick bar under the audio uses the same actions: one
            // definition, two places to reach it from.
            languageActions_.push_back({id, action});

            // The name is translated, the id is not: the id is what goes into
            // the PFML, so it stays in the tooltip rather than the label.
            auto * button = new QPushButton(
                QStringLiteral("%1  %2").arg(QChar(language.shortcut)).arg(name), box);
            // The swatch is the legend: this is the colour the language takes
            // in the text and in the strip, worn by the button that picks it.
            button->setIcon(maxlabel::ui::swatch(colour_for(language.id)));
            button->setToolTip(QStringLiteral("%1 — %2").arg(id).arg(QString(QChar(language.shortcut))));
            connect(button, &QPushButton::clicked, action, &QAction::trigger);
            layout->addWidget(button, row, column);
            if (++column == 2) {
                column = 0;
                ++row;
            }
        }
        railLayout->addWidget(box);
    }

    {
        auto * box = new QGroupBox(tr("WORD BOUNDARIES"), rail);
        auto * layout = new QVBoxLayout(box);
        layout->setSpacing(6);
        addRailButton(box, layout, markWordAction);
        addRailButton(box, layout, clearWordsAction);
        addRailButton(box, layout, reSplitAction_);
        railLayout->addWidget(box);
    }

    {
        auto * box = new QGroupBox(tr("PRONUNCIATION"), rail);
        auto * layout = new QVBoxLayout(box);
        layout->setSpacing(6);
        addRailButton(box, layout, pinAction, QString(), /*accent=*/true);
        addRailButton(box, layout, insertAction);

        auto * symbolButton = new QPushButton(tr("Non-lexical ▾"), box);
        symbolButton->setMenu(symbolMenu);
        symbolButton->setToolTip(tr("Insert AP / SP / sil / … at the cursor"));
        layout->addWidget(symbolButton);

        addRailButton(box, layout, clearOverridesAction);
        railLayout->addWidget(box);
    }

    // Typing commits shortly after it stops, so nothing is left only in memory.
    typingSaveTimer_ = new QTimer(this);
    typingSaveTimer_->setSingleShot(true);
    typingSaveTimer_->setInterval(kTypingSaveMs);
    connect(typingSaveTimer_, &QTimer::timeout, this, [this]() {
        if (currentSegment() == nullptr) return;
        maxlabel::Segment * segment = currentSegment();
        if (segment == nullptr) return;
        try {
            maxlabel::save(*segment);
        } catch (const std::exception & error) {
            status_->setText(tr("PFML did not parse: %1").arg(QString::fromUtf8(error.what())));
        }
    });

    // PFML edits are applied after a pause too, and for the same reason plus
    // one more: applying on every keystroke would rebuild the text pane under
    // the cursor while the author is still typing the tag.
    pfmlApplyTimer_ = new QTimer(this);
    pfmlApplyTimer_->setSingleShot(true);
    pfmlApplyTimer_->setInterval(kTypingSaveMs);
    connect(pfmlApplyTimer_, &QTimer::timeout, this, &MainWindow::applyPfmlEdit);

    // A message shown for a moment, then the summary comes back: the status
    // line is the only place that says what just happened, and it is also the
    // only place that says where you are.
    statusTimer_ = new QTimer(this);
    statusTimer_->setSingleShot(true);
    statusTimer_->setInterval(kStatusHoldMs);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);

    // --- central widget -----------------------------------------------------
    QSplitter * splitter = new QSplitter(this);

    // Aegisub's arrangement: the audio on top, the controls in a bar under it,
    // then the text.  The audio is full width because a spectrogram reads time
    // along X — in a tall thin pane it is unreadable, and that is the pane's
    // whole job.
    QSplitter * column = new QSplitter(Qt::Vertical, splitter);

    // Each pane gets a titled frame, as the reference tool does: it says what
    // the pane is without a legend, and it gives the eye somewhere to rest
    // between panes that would otherwise run together.
    auto * audioBox = new QGroupBox(tr("AUDIO"), column);
    auto * audioLayout = new QVBoxLayout(audioBox);
    audioLayout->setContentsMargins(8, 8, 8, 8);

    audio_ = new AudioPanel(audioBox);
    audio_->setObjectName(QStringLiteral("audioPanel"));
    audio_->setMinimumHeight(150);
    audioLayout->addWidget(audio_);
    connect(audio_, &AudioPanel::statusMessage, this, [this](const QString & message) {
        status_->setText(message);
    });

    QWidget * controls = new QWidget(column);
    audioControls_ = controls;
    QHBoxLayout * controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(4, 2, 4, 2);
    controlsLayout->setSpacing(2);

    // The transport lives in its own strip inside the bar, because it is the
    // only part of the bar that needs audio: an annotation is a text
    // operation, and it has to stay reachable on a segment with no recording.
    QWidget * transport = new QWidget(controls);
    transport_ = transport;
    QHBoxLayout * transportLayout = new QHBoxLayout(transport);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    transportLayout->setSpacing(2);

    const auto addButton = [&](const QString & iconName, const QString & tip,
                               const std::function<void()> & action, bool checkable = false) {
        QToolButton * button = new QToolButton(transport);
        button->setIcon(checkable
                            ? maxlabel::ui::icon(iconName, kIconColour, kIconOnAccent, 18)
                            : maxlabel::ui::icon(iconName, kIconColour, 18));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(tip);
        button->setProperty("role", "transport");   // square, per the theme
        button->setCheckable(checkable);
        // No focus, so the transport never steals the keyboard from the editor.
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, action);
        transportLayout->addWidget(button);
        return button;
    };

    addButton(QStringLiteral("play"), tr("Play the selection, or the whole file (Ctrl+Space)"),
              [this]() { audio_->playSelectionOrAll(); });
    addButton(QStringLiteral("stop"), tr("Stop"), [this]() { audio_->stop(); });
    transportLayout->addSpacing(10);
    addButton(QStringLiteral("back"), tr("Back half a second (Q)"),
              [this]() { audio_->nudge(-0.5); });
    addButton(QStringLiteral("forward"), tr("Forward half a second (W)"),
              [this]() { audio_->nudge(0.5); });
    transportLayout->addSpacing(10);
    addButton(QStringLiteral("spectrum"), tr("Waveform / spectrum"), [this]() { audio_->toggleMode(); },
              true);
    transportLayout->addSpacing(10);
    addButton(QStringLiteral("clear"), tr("Clear the selection"),
              [this]() { audio_->view()->clearSelection(); });

    QLabel * readout = new QLabel(format_time(0.0), transport);
    readout->setProperty("role", "readout");
    transportLayout->addSpacing(10);
    transportLayout->addWidget(readout);
    connect(audio_, &AudioPanel::positionChanged, this, [readout](double seconds) {
        readout->setText(format_time(seconds));
    });

    controlsLayout->addWidget(transport);
    controlsLayout->addSpacing(10);

    // The annotations people reach for constantly, in the bar between the audio
    // and the text — where the eye already is, and where a selection made in
    // the text is one click away from being used.
    const auto addQuick = [&](QAction * action) {
        QToolButton * button = new QToolButton(controls);
        button->setText(action->text());
        button->setToolTip(action->toolTip().isEmpty() ? action->text() : action->toolTip());
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        controlsLayout->addWidget(button);
        return button;
    };
    addQuick(markWordAction);
    addQuick(pinAction);
    addQuick(insertAction);
    controlsLayout->addSpacing(10);
    for (const LanguageEntry & entry : languageActions_) {
        QToolButton * button = new QToolButton(controls);
        button->setText(entry.id);
        // Same legend as the rail, in the compact form: the id and its colour.
        button->setIcon(maxlabel::ui::swatch(colour_for(entry.id.toStdString()), 11));
        button->setIconSize(QSize(11, 11));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setToolTip(entry.action->toolTip());
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, entry.action, &QAction::trigger);
        controlsLayout->addWidget(button);
    }

    controlsLayout->addStretch(1);

    // No audio, no audio area: an empty waveform and a dead transport take up
    // room and say nothing.  The annotation bar stays — it is not about the
    // recording.
    audioBox_ = audioBox;
    connect(audio_, &AudioPanel::audioAvailabilityChanged, this, [this](bool available) {
        audio_->setVisible(available);
        if (transport_ != nullptr) transport_->setVisible(available);
        if (audioBox_ != nullptr) audioBox_->setVisible(available);
    });
    audio_->setVisible(false);
    transport->setVisible(false);
    audioBox->setVisible(false);

    // The strip: the line as the units that get annotated.  Clicking one
    // selects it, which is the whole of the interaction — no dragging across
    // characters that have no boundary to snap to.
    strip_ = new RunStrip;
    // No setFont here either: RunStrip's size is in the theme, for the reason
    // spelled out at the editor below.
    connect(strip_, &RunStrip::chipClicked, this, &MainWindow::selectChip);
    // Double-click goes straight to the dialog: the common annotation should
    // not need the block clicked and then a button somewhere else.  An
    // inserted sound opens the same dialog filled with what is there — it has
    // no text range to set a pronunciation for, but it does have something to
    // change, and the dialog is also where it gets removed.
    // Double-click goes straight to the dialog: the common annotation should
    // not need the block clicked and then a button somewhere else.  An
    // inserted sound opens the same dialog filled with what is there — it has
    // no text range to set a pronunciation for, but it does have something to
    // change, and the dialog is also where it gets removed.
    connect(strip_, &RunStrip::chipActivated, this, [this](std::size_t begin, std::size_t end) {
        if (begin == end) {
            editInsertion(begin);
            return;
        }
        selectChip(begin, end);
        pinPronunciation();
    });
    strip_->setToolTip(tr("Click a block to select it, drag across blocks to select a "
                          "run, double-click to set its pronunciation.  Double-click "
                          "a +tag to change or remove that inserted sound."));
    // Scroll rather than grow: a long line must not push the text box, which is
    // the thing being edited, off the window.
    stripScroll_ = new QScrollArea(column);
    stripScroll_->setObjectName(QStringLiteral("stripScroll"));
    stripScroll_->setWidget(strip_);
    stripScroll_->setWidgetResizable(true);
    stripScroll_->setFrameShape(QFrame::NoFrame);
    stripScroll_->setMaximumHeight(strip_->rowHeight() * 3 + 20);
    stripScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The strip knows how tall its blocks are only once it has a width to wrap
    // against, so it tells us rather than being asked at the wrong moment.  A
    // scroll area will otherwise be squeezed to nothing by the layout, which
    // clipped the blocks; fixing the height keeps one row one row tall, and
    // turns a long line into an inner scroll instead of a shorter text box.
    connect(strip_, &RunStrip::heightNeeded, this, [this](int needed) {
        stripScroll_->setFixedHeight(
            std::min(needed, strip_->rowHeight() * 3 + 12));
    });

    editor_ = new LyricEditor(column);
    editor_->setObjectName(QStringLiteral("editor"));
    // One undo, not two.  Qt's own stack would cover typing and silently ignore
    // every annotation, which is worse than no undo at all.
    editor_->setUndoRedoEnabled(false);
    // And the editor has to hand the key over: with its own undo disabled it
    // would otherwise swallow Ctrl+Z and the history would only be reachable
    // from the rail.
    editor_->installEventFilter(this);
    // The size and the family of both text panes live in the theme, not here:
    // a QWidget rule already names a font size, and a stylesheet wins over
    // setFont for every property it names — so a setPointSize(18) at this
    // point was being silently discarded, and read as "the code says 18pt but
    // it still looks 9pt".  See QPlainTextEdit#editor and RunStrip in
    // theme.qss.
    editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor_->setPlaceholderText(tr("The lyric line.  Language is detected per script; "
                                   "select a run and press 1-4 to decide it yourself."));
    connect(editor_, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(editor_, &QPlainTextEdit::selectionChanged, this,
            &MainWindow::onEditorSelectionChanged);

    preview_ = new QPlainTextEdit(column);
    preview_->setObjectName(QStringLiteral("preview"));
    // Editable on purpose: the PFML is the artefact that ships, and an author
    // who can read it should be able to correct it in place.  What is typed
    // here is parsed back into the model the other panes draw from, so the two
    // never disagree — and a fragment that will not parse changes nothing and
    // says so.
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    preview_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    // One undo for the window, not one per pane: see eventFilter.
    preview_->setUndoRedoEnabled(false);
    preview_->installEventFilter(this);
    preview_->setPlaceholderText(tr("The PFML that will be written for the aligner.  "
                                    "Editable: what you type here is read back into the "
                                    "text above."));
    connect(preview_, &QPlainTextEdit::textChanged, this, &MainWindow::onPfmlChanged);

    auto * textBox = new QGroupBox(tr("TEXT"), column);
    auto * textLayout = new QVBoxLayout(textBox);
    textLayout->setContentsMargins(8, 8, 8, 8);
    textLayout->addWidget(editor_);

    auto * previewBox = new QGroupBox(tr("PFML"), column);
    auto * previewLayout = new QVBoxLayout(previewBox);
    previewLayout->setContentsMargins(8, 8, 8, 8);
    previewLayout->addWidget(preview_);

    column->addWidget(audioBox);
    column->addWidget(controls);
    column->addWidget(stripScroll_);
    column->addWidget(textBox);
    column->addWidget(previewBox);
    column->setStretchFactor(0, 3);
    column->setStretchFactor(1, 0);
    column->setStretchFactor(2, 0);
    column->setStretchFactor(3, 4);
    column->setStretchFactor(4, 1);
    column->setCollapsible(2, false);

    railScroll->setMinimumWidth(250);
    railScroll->setMaximumWidth(330);
    splitter->addWidget(railScroll);
    splitter->addWidget(column);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    // The audio keys are scoped to the panel so Space can still type a space;
    // this one works from anywhere, for when the editor has focus.
    QAction * playAction = new QAction(tr("Play"), this);
    playAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    connect(playAction, &QAction::triggered, this, [this]() {
        audio_->playSelectionOrAll();
    });
    addAction(playAction);

    status_ = new QLabel(this);
    status_->setProperty("state", "idle");
    statusBar()->addWidget(status_);

    // The editor takes the focus, so the window opens ready to type and no
    // button shows a focus ring it did not ask for.
    editor_->setFocus();

    refreshStatus();
    updateActions();
    updateHistoryActions();
    lastPushWasTyping_ = false;
}

bool MainWindow::eventFilter(QObject * watched, QEvent * event) {
    // Both text panes hand Ctrl+Z to the window's one history.  A text widget
    // with its own undo stack would quietly cover typing and ignore every
    // annotation, and the PFML pane would undo its own keystrokes instead of
    // the edit they became.
    if (event->type() == QEvent::KeyPress &&
        (watched == editor_ || watched == preview_)) {
        auto * key = static_cast<QKeyEvent *>(event);
        const int modifiers = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        if (modifiers == Qt::ControlModifier && key->key() == Qt::Key_Z) {
            undo();
            return true;
        }
        if (modifiers == Qt::ControlModifier && key->key() == Qt::Key_Y) {
            redo();
            return true;
        }
        if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) &&
            key->key() == Qt::Key_Z) {
            redo();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::flushPendingPfmlEdit() {
    // An undo pressed while a PFML edit is still in its debounce window would
    // otherwise step over it: the model has not changed yet, so the undo would
    // take back the action before the typing instead of the typing.
    if (pfmlApplyTimer_ != nullptr && pfmlApplyTimer_->isActive()) {
        pfmlApplyTimer_->stop();
        applyPfmlEdit();
    }
}

const maxlabel::Segment * MainWindow::currentSegment() const {
    if (current_ < 0 || current_ >= static_cast<int>(project_.segments.size())) return nullptr;
    return &project_.segments[static_cast<std::size_t>(current_)];
}

maxlabel::Segment * MainWindow::currentSegment() {
    if (current_ < 0 || current_ >= static_cast<int>(project_.segments.size())) return nullptr;
    return &project_.segments[static_cast<std::size_t>(current_)];
}

void MainWindow::openDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Open a project folder"));
    if (!directory.isEmpty()) loadDirectory(directory);
}

void MainWindow::loadDirectory(const QString & directory) {
    try {
        project_ = maxlabel::scan(directory.toStdString());
    } catch (const std::exception & error) {
        maxlabel::ui::warn(this, tr("MaxLabel"), QString::fromUtf8(error.what()));
        return;
    }
    current_ = -1;
    histories_.clear();
    refreshList();
    if (!project_.segments.empty()) selectRow(0);
    refreshStatus();
    updateActions();

    showStatus(tr("Loaded %n segment(s)", "", static_cast<int>(project_.segments.size())),
               "ready");
}

void MainWindow::refreshList() {
    loading_ = true;
    list_->clear();
    for (const maxlabel::Segment & segment : project_.segments) {
        QString label = QString::fromStdString(segment.id);
        if (!maxlabel::is_saved(segment)) label += QStringLiteral(" *");
        if (!segment.pfml_valid) label += tr("   [invalid]");
        else if (segment.source == maxlabel::TextSource::None) label += tr("   [empty]");
        if (maxlabel::has_undetermined(segment.spans)) label += tr("   [language?]");
        if (segment.audio_path.empty()) label += tr("   (no audio)");
        list_->addItem(label);
    }
    loading_ = false;
}

void MainWindow::selectRow(int row) {
    if (row < 0 || row >= static_cast<int>(project_.segments.size())) return;
    // Commit before moving: nothing may be left pending, or navigating would
    // discard the edit.
    if (current_ != row && !commitCurrent(false)) return;

    loading_ = true;
    current_ = row;
    const maxlabel::Segment & segment = *currentSegment();
    editor_->setPlainText(QString::fromStdString(segment.text));
    // Inside the guard: setCurrentRow emits currentRowChanged, and letting it
    // re-enter would load the same segment's audio twice.
    list_->setCurrentRow(row);
    loading_ = false;

    // Text-first pairing: the segment decides which audio is loaded.
    audio_->setAudioFile(QString::fromStdString(segment.audio_path));

    applyHighlights();
    refreshPreview();
    refreshStatus();
    updateActions();
}

void MainWindow::onRowChanged(int row) {
    if (loading_) return;
    selectRow(row);
}

void MainWindow::onTextChanged() {
    if (loading_) return;
    // Keep the colours live while there is nothing manual to lose.  Once the
    // author has decided a span by hand, typing must not silently re-derive it
    // — the status line says so and R re-splits on request.
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    pushHistory(true);
    // Typing is not written on every keystroke, but it is not left pending
    // either: a short pause is enough to commit it.
    typingSaveTimer_->start();
    if (!has_manual_spans(segment->spans)) {
        segment->text = editor_->toPlainText().toStdString();
        maxlabel::detect_spans(*segment);
        maxlabel::rebuild_pfml(*segment);
    }
    // Refresh only: writing on every keystroke would be a write per character,
    // which is bad enough on a synced folder to be worth the second of delay.
    // The debounce timer commits shortly after typing stops.
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::refreshSpansFromText() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    segment->text = editor_->toPlainText().toStdString();
    maxlabel::detect_spans(*segment);
    maxlabel::rebuild_pfml(*segment);
}

void MainWindow::reSplit() {
    refreshSpansFromText();
    pushHistory(false);
    commitEdit();
}

void MainWindow::markWord() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    std::size_t begin = 0;
    std::size_t end = 0;
    if (!currentRange(begin, end)) {
        showStatus(tr("Select the run to fix as one word."), "busy");
        return;
    }
    pushHistory(false);
    // The same selection toggles: pressing it again on a word that is already
    // exactly this one takes the mark back, which is how a mistaken boundary
    // gets undone without a separate command to find.
    const bool marked = maxlabel::toggle_word(*segment, begin, end);
    commitEdit();
    showStatus(marked ? tr("Marked as one word.") : tr("Word mark removed."), "ready");
}

void MainWindow::clearWords() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    pushHistory(false);
    maxlabel::clear_words(*segment);
    commitEdit();
}

void MainWindow::setSpectrumMode(bool spectrum) { audio_->setSpectrumMode(spectrum); }

void MainWindow::selectRowAt(int row) { selectRow(row); }

void MainWindow::selectRange(std::size_t begin, std::size_t end) {
    editor_->setFocus();
    selectChip(begin, end);
}

MainWindow::Snapshot MainWindow::snapshot() const {
    Snapshot state;
    const maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return state;
    state.text      = segment->text;
    state.spans     = segment->spans;
    state.words     = segment->words;
    state.overrides = segment->overrides;
    state.pfml      = segment->pfml;
    return state;
}

void MainWindow::pushHistory(bool typing) {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    History & history = histories_[segment->id];

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (typing && lastPushWasTyping_ && !history.undo.empty() &&
        now - lastPushMs_ < kTypingGroupMs) {
        lastPushMs_ = now;   // the burst is already anchored
        return;
    }

    history.undo.push_back(snapshot());
    if (history.undo.size() > kMaxHistory) history.undo.erase(history.undo.begin());
    history.redo.clear();
    lastPushWasTyping_ = typing;
    lastPushMs_ = now;
    updateHistoryActions();
}

void MainWindow::restore(const Snapshot & state) {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    segment->text      = state.text;
    segment->spans     = state.spans;
    segment->words     = state.words;
    segment->overrides = state.overrides;
    segment->pfml      = state.pfml;

    // loading_ keeps onTextChanged from reading this as a keystroke and pushing
    // another step on top of the one being undone.
    loading_ = true;
    editor_->setPlainText(QString::fromStdString(state.text));
    loading_ = false;

    // Everything else lands on disk immediately; an undo has to as well, or the
    // file would disagree with the window.
    try {
        maxlabel::save(*segment);
    } catch (const std::exception & error) {
        status_->setText(tr("PFML did not parse: %1").arg(QString::fromUtf8(error.what())));
    }

    commitEdit();
}

void MainWindow::undo() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    flushPendingPfmlEdit();
    History & history = histories_[segment->id];
    if (history.undo.empty()) return;

    history.redo.push_back(snapshot());
    const Snapshot state = history.undo.back();
    history.undo.pop_back();
    restore(state);
    lastPushWasTyping_ = false;
    updateHistoryActions();
    showStatus(tr("Undone"), "ready");
}

void MainWindow::redo() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    flushPendingPfmlEdit();
    History & history = histories_[segment->id];
    if (history.redo.empty()) return;

    history.undo.push_back(snapshot());
    const Snapshot state = history.redo.back();
    history.redo.pop_back();
    restore(state);
    lastPushWasTyping_ = false;
    updateHistoryActions();
    showStatus(tr("Redone"), "ready");
}

void MainWindow::updateHistoryActions() {
    const maxlabel::Segment * segment = currentSegment();
    const History * history = nullptr;
    if (segment != nullptr) {
        const auto it = histories_.find(segment->id);
        if (it != histories_.end()) history = &it->second;
    }
    if (undoAction_ != nullptr) undoAction_->setEnabled(history != nullptr && !history->undo.empty());
    if (redoAction_ != nullptr) redoAction_->setEnabled(history != nullptr && !history->redo.empty());
}

void MainWindow::showStatus(const QString & message, const char * state, int milliseconds) {
    status_->setText(message);
    // The theme colours the status line by this property, so it has to be
    // re-polished for the change to show: Qt does not restyle on a property
    // change by itself.
    status_->setProperty("state", state);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    statusTimer_->start(milliseconds);
}

void MainWindow::commitEdit() {
    maxlabel::Segment * segment = currentSegment();
    if (segment != nullptr) {
        // The design is that nothing is ever pending: a change that is only in
        // memory is one crash away from being lost, and the file would
        // disagree with the window until the next navigation.
        try {
            maxlabel::save(*segment);
        } catch (const std::exception & error) {
            showStatus(tr("Not saved: %1").arg(QString::fromUtf8(error.what())), "error");
        }
    }
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::refreshStrip() {
    const maxlabel::Segment * segment = currentSegment();
    std::vector<RunStrip::Chip> chips;
    if (segment != nullptr) {
        const std::string & text = segment->text;

        // Inserted sounds first: they get their own block, placed before the
        // character they precede.  A tint on the neighbour says only that
        // something is there; the block says what.
        std::vector<RunStrip::Chip> inserts;
        for (const maxlabel::Override & override_ : segment->overrides) {
            if (!override_.inserts() || override_.phonemes.empty()) continue;
            RunStrip::Chip insert;
            insert.begin = insert.end = override_.begin;
            insert.insert = true;
            insert.colour = QColor();   // the block paints itself, not a tint
            QString joined;
            for (const std::string & phoneme : override_.phonemes) {
                if (!joined.isEmpty()) joined += QLatin1Char(' ');
                joined += QString::fromStdString(phoneme);
            }
            insert.label   = QStringLiteral("+") + joined;
            insert.unknown = has_unknown_phoneme(
                vocabulary_, override_.phonemes,
                phoneme_languages(*segment, override_.begin));
            inserts.push_back(std::move(insert));
        }
        std::sort(inserts.begin(), inserts.end(),
                  [](const RunStrip::Chip & a, const RunStrip::Chip & b) {
                      return a.begin < b.begin;
                  });
        std::size_t next_insert = 0;
        const auto flush_inserts = [&](std::size_t up_to) {
            while (next_insert < inserts.size() && inserts[next_insert].begin <= up_to) {
                chips.push_back(inserts[next_insert]);
                ++next_insert;
            }
        };

        for (const maxlabel::LangSpan & span : segment->spans) {
            if (span.end <= span.begin || span.end > text.size()) continue;

            std::size_t i = span.begin;
            while (i < span.end) {
                const std::size_t at = i;
                const unsigned char b = static_cast<unsigned char>(text[i]);
                std::size_t width = 1;
                if ((b & 0xE0) == 0xC0) width = 2;
                else if ((b & 0xF0) == 0xE0) width = 3;
                else if ((b & 0xF8) == 0xF0) width = 4;
                i += width;
                if (i > span.end) i = span.end;

                // Latin runs stay one block: a word is one thing to annotate,
                // and per-letter blocks would be noise.
                if (width == 1 &&
                    std::isalnum(static_cast<unsigned char>(text[at]))) {
                    while (i < span.end &&
                           std::isalnum(static_cast<unsigned char>(text[i]))) {
                        ++i;
                    }
                }

                flush_inserts(at);

                RunStrip::Chip chip;
                chip.begin  = at;
                chip.end    = i;
                chip.label  = QString::fromStdString(text.substr(at, i - at));
                chip.colour = colour_for(span.language);

                for (const maxlabel::WordBoundary & word : segment->words) {
                    if (word.begin <= at && word.end >= i) {
                        chip.word = true;
                        break;
                    }
                }

                // What is pinned to it, if anything: the reading under the
                // character is the visible result of an annotation.
                for (const maxlabel::Override & override_ : segment->overrides) {
                    if (override_.begin > at || override_.end < i) continue;
                    if (override_.inserts()) continue;
                    chip.pinned = true;
                    // The script is what a person reads — "de" — and the
                    // phoneme list is what the aligner gets.  Show the
                    // readable one; fall back when there is no script.
                    if (!override_.script.empty()) {
                        chip.reading = QString::fromStdString(override_.script);
                    } else if (!override_.phonemes.empty()) {
                        QString joined;
                        for (const std::string & phoneme : override_.phonemes) {
                            if (!joined.isEmpty()) joined += QLatin1Char(' ');
                            joined += QString::fromStdString(phoneme);
                        }
                        chip.reading = joined;
                    }
                    chip.unknown = has_unknown_phoneme(
                        vocabulary_, override_.phonemes,
                        phoneme_languages(*segment, override_.begin));
                    break;
                }
                chips.push_back(std::move(chip));
            }
        }
        flush_inserts(text.size());
    }
    strip_->setChips(chips);
    // setChips drops the highlight, so put it back: the block the author
    // clicked has to stay marked while the model underneath it changes.
    if (stripHasCurrent_) strip_->setCurrent(stripBegin_, stripEnd_);
}

bool MainWindow::currentRange(std::size_t & begin, std::size_t & end) const {
    if (stripHasCurrent_) {
        begin = stripBegin_;
        end = stripEnd_;
        return true;
    }
    const QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) return false;
    const QString text = editor_->toPlainText();
    begin = byte_offset_of(text, cursor.selectionStart());
    end = byte_offset_of(text, cursor.selectionEnd());
    return true;
}

void MainWindow::selectChip(std::size_t begin, std::size_t end) {
    stripHasCurrent_ = true;
    stripBegin_ = begin;
    stripEnd_ = end;
    strip_->setCurrent(begin, end);

    // Mirror it in the editor, so the two views agree on what is selected.
    // The mirror provokes a selectionChanged of its own, which must not be
    // read as the author selecting something in the text.
    const QString text = editor_->toPlainText();
    QTextCursor cursor = editor_->textCursor();
    cursor.setPosition(utf16_offset_of(text, begin));
    cursor.setPosition(utf16_offset_of(text, end), QTextCursor::KeepAnchor);
    mirroringSelection_ = true;
    editor_->setTextCursor(cursor);
    mirroringSelection_ = false;
}

void MainWindow::onEditorSelectionChanged() {
    if (mirroringSelection_ || loading_) return;
    if (!stripHasCurrent_) return;
    // A selection made in the text pane is the newer decision, so the strip
    // stops claiming one.  Without this the block clicked once keeps winning
    // every later selection, and the text pane becomes unusable.
    stripHasCurrent_ = false;
    strip_->clearCurrent();
}

void MainWindow::loadVocabulary(const QString & path) {
    std::string error;
    if (!vocabulary_.load(path.toStdString(), &error)) {
        maxlabel::ui::warn(this, tr("MaxLabel"), QString::fromUtf8(error.c_str()));
        return;
    }
    applyHighlights();
    refreshStatus();
}

void MainWindow::loadG2PDirectory(const QString & model_dir) {
    // The dictionaries live beside the model, so pointing at it is all the
    // configuration a user should have to do.
    const std::string config = maxlabel::build_g2p_config(model_dir.toStdString());
    if (config.empty()) {
        showStatus(tr("No dictionaries in %1").arg(model_dir), "error");
        return;
    }
    // The config is text here, not a path: loadG2P is the --g2p <file> variant.
    std::string error;
    if (!g2p_.loadConfig(config, model_dir.toStdString(), &error)) {
        maxlabel::ui::warn(this, tr("MaxLabel"), QString::fromUtf8(error.c_str()));
        return;
    }
    refreshStatus();
    showStatus(tr("Pronunciation candidates loaded"), "ready");
}

void MainWindow::loadG2P(const QString & config_json, const QString & dictionary_dir) {
    std::string error;
    if (!g2p_.load(config_json.toStdString(), dictionary_dir.toStdString(), &error)) {
        maxlabel::ui::warn(this, tr("MaxLabel"), QString::fromUtf8(error.c_str()));
        return;
    }
    refreshStatus();
}

void MainWindow::pinPronunciation() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    std::size_t begin = 0;
    std::size_t end = 0;
    if (!currentRange(begin, end)) {
        showStatus(tr("Select the run to pin a pronunciation for."), "busy");
        return;
    }

    PronunciationDialog dialog(
        QString::fromStdString(segment->text.substr(begin, end - begin)),
        QString::fromStdString(maxlabel::language_at(*segment, begin)),
        false, &g2p_, &vocabulary_, this);
    // Show what is already there: the dialog is opened to change a decision as
    // often as to make one, and an empty form looks like nothing is pinned.
    for (const maxlabel::Override & override_ : segment->overrides) {
        if (override_.inserts()) continue;
        if (override_.begin != begin || override_.end != end) continue;
        QString joined;
        for (const std::string & phoneme : override_.phonemes) {
            if (!joined.isEmpty()) joined += QLatin1Char(' ');
            joined += QString::fromStdString(phoneme);
        }
        dialog.setExisting(QString::fromStdString(override_.script), joined);
        break;
    }

    if (dialog.exec() != QDialog::Accepted) return;

    pushHistory(false);
    if (dialog.removalRequested()) {
        maxlabel::remove_overrides_in(*segment, begin, end);
        commitEdit();
        showStatus(tr("Written pronunciation removed."), "ready");
        return;
    }
    const std::vector<std::string> phonemes = dialog.phonemes();
    if (phonemes.empty()) return;

    maxlabel::set_override(*segment, begin, end, std::string(),
                           dialog.script().toStdString(), phonemes);
    commitEdit();
}

void MainWindow::insertPhonemes() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    const std::size_t at =
        byte_offset_of(editor_->toPlainText(), editor_->textCursor().position());
    PronunciationDialog dialog(QString(),
                               QString::fromStdString(maxlabel::language_at(*segment, at)),
                               true, &g2p_, &vocabulary_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const std::vector<std::string> phonemes = dialog.phonemes();
    if (phonemes.empty()) return;

    pushHistory(false);
    maxlabel::insert_phoneme(*segment, at, phonemes);
    commitEdit();
}

void MainWindow::editInsertion(std::size_t position) {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    const maxlabel::Override * found = nullptr;
    for (const maxlabel::Override & override_ : segment->overrides) {
        if (override_.inserts() && override_.begin == position) {
            found = &override_;
            break;
        }
    }
    if (found == nullptr) return;

    QString joined;
    for (const std::string & phoneme : found->phonemes) {
        if (!joined.isEmpty()) joined += QLatin1Char(' ');
        joined += QString::fromStdString(phoneme);
    }

    // Insertion mode, so the placeholder is about a sound rather than a
    // reading; setExisting then fills the field and adds Remove.
    PronunciationDialog dialog(QString(),
                               QString::fromStdString(maxlabel::language_at(*segment, position)),
                               true, &g2p_, &vocabulary_, this);
    dialog.setExisting(QString(), joined);
    if (dialog.exec() != QDialog::Accepted) return;

    if (dialog.removalRequested()) {
        pushHistory(false);
        maxlabel::remove_override_at(*segment, position);
        commitEdit();
        showStatus(tr("Inserted sound removed."), "ready");
        return;
    }
    const std::vector<std::string> phonemes = dialog.phonemes();
    if (phonemes.empty()) {
        // Nothing to record and nothing to undo: say so rather than pushing a
        // history step for a change that was not made.
        showStatus(tr("Enter at least one phoneme, or use Remove."), "busy");
        return;
    }
    pushHistory(false);
    maxlabel::insert_phoneme(*segment, position, phonemes);
    commitEdit();
    showStatus(tr("Inserted sound changed."), "ready");
}

void MainWindow::clearOverrides() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    pushHistory(false);
    maxlabel::clear_overrides(*segment);
    commitEdit();
}

void MainWindow::applyHighlights() {
    const maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) {
        strip_->setChips({});
        return;
    }

    const QString text = editor_->toPlainText();
    QList<QTextEdit::ExtraSelection> selections;

    for (const maxlabel::LangSpan & span : segment->spans) {
        const int begin = utf16_offset_of(text, span.begin);
        const int end   = utf16_offset_of(text, span.end);
        if (end <= begin) continue;

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor_->document());
        selection.cursor.setPosition(begin);
        selection.cursor.setPosition(end, QTextCursor::KeepAnchor);

        QTextCharFormat format;
        if (span.ambiguous || span.language.empty()) {
            // Undetermined: no colour, a wavy underline.  Colour would read as
            // an answer, and there is not one yet.
            format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            format.setUnderlineColor(kErrorUnderline);
        } else {
            format.setBackground(colour_for(span.language));
            if (span.manual) format.setFontWeight(QFont::DemiBold);
        }
        selection.format = format;
        selections.push_back(selection);
    }

    // Fixed word boundaries get a solid underline on top of the language
    // background: two independent layers over the same text, so they have to
    // be told apart at a glance.
    for (const maxlabel::WordBoundary & word : segment->words) {
        const int begin = utf16_offset_of(text, word.begin);
        const int end   = utf16_offset_of(text, word.end);
        if (end <= begin) continue;

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor_->document());
        selection.cursor.setPosition(begin);
        selection.cursor.setPosition(end, QTextCursor::KeepAnchor);

        QTextCharFormat format;
        format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        format.setUnderlineColor(kWordUnderline);
        selection.format = format;
        selections.push_back(selection);
    }

    // Overrides: a dotted underline says "the phonemes here are written by
    // hand".  A phoneme the vocabulary does not know turns it into a red wavy
    // one, because that is the case the aligner would fail on — and finding
    // out here is the whole reason to check.
    std::vector<LyricEditor::Insertion> insertions;
    for (const maxlabel::Override & override_ : segment->overrides) {
        if (override_.inserts()) {
            // An insertion has no width, so the underline cannot say anything
            // about it.  The editor draws it instead, by position, and the
            // text under it keeps only a faint tint so the eye knows the
            // tagged sound lands between these two characters.
            const int at = utf16_offset_of(text, override_.begin);
            QString joined;
            for (const std::string & phoneme : override_.phonemes) {
                if (!joined.isEmpty()) joined += QLatin1Char(' ');
                joined += QString::fromStdString(phoneme);
            }
            insertions.push_back(LyricEditor::Insertion{ at, joined });

            if (at >= text.size()) continue;
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(editor_->document());
            selection.cursor.setPosition(at);
            selection.cursor.setPosition(std::min(at + 1, static_cast<int>(text.size())),
                                         QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setBackground(QColor(0x4A, 0x38, 0x1C));
            selection.format = format;
            selections.push_back(selection);
            continue;
        }
        const int begin = utf16_offset_of(text, override_.begin);
        const int end   = utf16_offset_of(text, override_.end);
        if (end <= begin) continue;

        const bool unknown = has_unknown_phoneme(
            vocabulary_, override_.phonemes, phoneme_languages(*segment, override_.begin));

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor_->document());
        selection.cursor.setPosition(begin);
        selection.cursor.setPosition(end, QTextCursor::KeepAnchor);

        QTextCharFormat format;
        if (unknown) {
            format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            format.setUnderlineColor(kErrorUnderline);
        } else {
            format.setUnderlineStyle(QTextCharFormat::DotLine);
            format.setUnderlineColor(kOverrideUnderline);
        }
        selection.format = format;
        selections.push_back(selection);
    }

    editor_->setExtraSelections(selections);
    editor_->setInsertions(insertions);
    refreshStrip();
}

void MainWindow::refreshPreview() {
    const maxlabel::Segment * segment = currentSegment();
    // Setting the text fires textChanged, which is the same signal the author's
    // typing arrives on; the guard is what tells the two apart.
    updatingPreview_ = true;
    preview_->setPlainText(segment == nullptr ? QString()
                                              : QString::fromStdString(segment->pfml));
    updatingPreview_ = false;
    setPfmlError(QString());
}

void MainWindow::setPfmlError(const QString & message) {
    // An error is a state of the whole window, not just of the lower pane:
    // while the fragment does not parse there is no text to draw, no spans and
    // no strip, so the panes that would be showing those say so by going dim
    // rather than by going on showing the last thing that did parse.
    const bool broken = !message.isEmpty();
    const auto repolish = [](QWidget * widget) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    };
    preview_->setProperty("state", broken ? QStringLiteral("error") : QString());
    editor_->setProperty("stale", broken);
    stripScroll_->setProperty("stale", broken);
    repolish(preview_);
    repolish(editor_);
    repolish(stripScroll_);
    if (broken) {
        preview_->setToolTip(message);
        showStatus(message, "error");
    } else {
        preview_->setToolTip(tr("The PFML that will be written for the aligner."));
    }
}

void MainWindow::onPfmlChanged() {
    if (updatingPreview_ || loading_) return;
    // Not applied yet: the fragment is usually invalid halfway through a tag,
    // and reporting an error for every intermediate keystroke would be noise.
    pfmlApplyTimer_->start();
}

void MainWindow::applyPfmlEdit() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    const std::string typed = preview_->toPlainText().toStdString();
    if (typed == segment->pfml) return;   // nothing was actually edited

    // Parsed before anything is touched, in two steps, because the two failure
    // modes are different and both have to leave the model alone.  A first
    // version imported first and let save() catch the malformed case — which
    // meant the text pane had already been refilled from a fragment that was
    // never going to be written, and the window disagreed with the file.
    try {
        maxlabel::validate(typed);
    } catch (const std::exception & error) {
        setPfmlError(tr("PFML does not parse: %1 — the text and the strip still "
                        "show the last fragment that parsed.")
                         .arg(QString::fromUtf8(error.what())));
        return;
    }

    const maxlabel::ImportedFragment imported = maxlabel::import_pfml(typed);
    if (!imported.error.empty()) {
        setPfmlError(tr("PFML not understood: %1 — the text and the strip still "
                        "show the last fragment that parsed.")
                         .arg(QString::fromUtf8(imported.error.c_str())));
        return;
    }

    pushHistory(false);
    segment->text      = imported.text;
    segment->spans     = imported.spans;
    segment->words     = imported.words;
    segment->overrides = imported.overrides;
    segment->pfml      = typed;

    // The text came back from the fragment, so the editor has to be refilled
    // before anything reads the selection out of it.
    loading_ = true;
    editor_->setPlainText(QString::fromStdString(segment->text));
    loading_ = false;

    setPfmlError(QString());
    try {
        maxlabel::save(*segment);
    } catch (const std::exception & error) {
        setPfmlError(tr("Saved PFML is invalid: %1").arg(QString::fromUtf8(error.what())));
        return;
    }    applyHighlights();
    refreshStatus();
    refreshList();
    showStatus(tr("PFML applied."), "ready");
}

void MainWindow::setSelectionLanguage(const QString & language) {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    const QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
        status_->setText(tr("Select the run to set a language for."));
        return;
    }
    const QString text = editor_->toPlainText();
    const std::size_t begin = byte_offset_of(text, cursor.selectionStart());
    const std::size_t end   = byte_offset_of(text, cursor.selectionEnd());

    pushHistory(false);
    maxlabel::set_span_language(*segment, begin, end, language.toStdString());
    commitEdit();
}

bool MainWindow::commitCurrent(bool quiet) {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return true;

    const std::string text = editor_->toPlainText().toStdString();

    maxlabel::Segment candidate = *segment;
    if (text != candidate.text) {
        // Offsets from a previous text no longer describe this one, so the
        // spans are re-derived rather than sliced against the wrong string.
        candidate.text = text;
        maxlabel::detect_spans(candidate);
        maxlabel::rebuild_pfml(candidate);
    }
    // Always written, even when the model already matches: typing keeps the
    // model in step, so an early return here meant the save button did nothing
    // in the ordinary case.  `save` skips the write when the bytes are already
    // on disk, so this costs a read rather than a rewrite.
    try {
        maxlabel::save(candidate);
    } catch (const std::exception & error) {
        if (!quiet) {
            maxlabel::ui::warn(this, tr("PFML did not parse"),
                                 tr("The segment was not written, because the aligner "
                                    "would skip this sample:\n\n%1")
                                     .arg(QString::fromUtf8(error.what())));
        }
        return false;
    }
    *segment = candidate;
    return true;
}

void MainWindow::saveCurrent() {
    if (!commitCurrent(false)) return;
    refreshList();
    applyHighlights();
    refreshPreview();
    refreshStatus();

    const maxlabel::Segment * segment = currentSegment();
    if (segment != nullptr) {
        refreshList();
        showStatus(tr("Saved %1.pfml").arg(QString::fromStdString(segment->id)), "ready");
    }
}

void MainWindow::goPrevious() {
    if (current_ > 0) selectRow(current_ - 1);
}

void MainWindow::goNext() {
    if (current_ + 1 < static_cast<int>(project_.segments.size())) selectRow(current_ + 1);
}

void MainWindow::refreshStatus() {
    if (project_.directory.empty()) {
        status_->setText(tr("Open a folder to begin."));
        return;
    }
    const maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) {
        status_->setText(tr("%1 segment(s)").arg(project_.segments.size()));
        return;
    }

    QStringList parts;
    parts << QString::fromStdString(segment->id)
          << tr("from %1").arg(QString::fromUtf8(maxlabel::to_string(segment->source)));

    int undetermined = 0;
    for (const maxlabel::LangSpan & span : segment->spans) {
        if (span.ambiguous || span.language.empty()) ++undetermined;
    }
    if (undetermined != 0) {
        parts << tr("%n undetermined run(s) — select and press 1-4", "", undetermined);
    } else if (!segment->spans.empty()) {
        parts << tr("languages decided");
    }
    if (has_manual_spans(segment->spans)) {
        parts << tr("manual marks: editing the text re-derives them");
    }
    if (!segment->words.empty()) {
        parts << tr("%n fixed word(s)", "", static_cast<int>(segment->words.size()));
    }
    if (!segment->overrides.empty()) {
        parts << tr("%n override(s)", "", static_cast<int>(segment->overrides.size()));
    }
    parts << (vocabulary_.empty()
                  ? tr("no vocabulary: phonemes unchecked")
                  : tr("vocabulary: %1 symbols").arg(vocabulary_.size()));
    if (g2p_.ready()) parts << tr("G2P ready");
    if (!segment->pfml_valid) {
        parts << tr("PFML INVALID: %1").arg(QString::fromStdString(segment->error));
    } else if (!segment->error.empty()) {
        parts << tr("not editable here: %1").arg(QString::fromStdString(segment->error));
    }
    status_->setText(parts.join(QStringLiteral("   |   ")));
}

void MainWindow::updateActions() {
    const int count = static_cast<int>(project_.segments.size());
    prevAction_->setEnabled(current_ > 0);
    nextAction_->setEnabled(current_ >= 0 && current_ + 1 < count);
    saveAction_->setEnabled(current_ >= 0);
    reSplitAction_->setEnabled(current_ >= 0);
}
