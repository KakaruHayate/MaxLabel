#include "MainWindow.h"

#include "AudioPanel.h"
#include "PronunciationDialog.h"

#include <QAction>
#include <QColor>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <functional>

namespace {

QString format_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = static_cast<int>(seconds * 1000.0);
    return QStringLiteral("%1:%2.%3")
        .arg(total / 60000)
        .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg((total / 100) % 10);
}

// A colour per language.  Chosen to stay apart under the common forms of
// colour blindness (no red/green pair carrying meaning) and light enough that
// black text stays readable on top.
QColor colour_for(const std::string & language) {
    if (language == "zh")  return QColor(0xC9, 0xE0, 0xF5);   // blue
    if (language == "ja")  return QColor(0xF3, 0xD3, 0xE8);   // pink
    if (language == "en")  return QColor(0xD6, 0xEC, 0xC9);   // green
    if (language == "ko")  return QColor(0xF6, 0xE7, 0xB8);   // amber
    if (language == "yue") return QColor(0xF7, 0xD9, 0xB5);   // orange
    return QColor();
}

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

// Whether any of these phonemes would fail to resolve.  An empty vocabulary
// cannot answer, so nothing is flagged — a tool that flags everything is as
// useless as one that flags nothing.
bool has_unknown_phoneme(const maxlabel::Vocabulary & vocabulary,
                         const std::vector<std::string> & phonemes,
                         const std::string & language) {
    if (vocabulary.empty()) return false;
    const std::vector<std::string> languages =
        language.empty() ? std::vector<std::string>{} : std::vector<std::string>{ language };
    for (const std::string & phoneme : phonemes) {
        if (!maxlabel::check_phoneme(vocabulary, phoneme, languages).known) return true;
    }
    return false;
}

}  // namespace

MainWindow::MainWindow(QWidget * parent) : QMainWindow(parent) {
    setWindowTitle(tr("MaxLabel"));
    resize(1200, 760);

    QToolBar * toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);

    QAction * openAction = toolbar->addAction(tr("Open Folder…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openDirectory);

    toolbar->addSeparator();

    prevAction_ = toolbar->addAction(tr("Previous"));
    prevAction_->setShortcut(QKeySequence(Qt::Key_Z));
    connect(prevAction_, &QAction::triggered, this, &MainWindow::goPrevious);

    nextAction_ = toolbar->addAction(tr("Next"));
    nextAction_->setShortcut(QKeySequence(Qt::Key_X));
    connect(nextAction_, &QAction::triggered, this, &MainWindow::goNext);

    toolbar->addSeparator();

    saveAction_ = toolbar->addAction(tr("Save"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrent);

    reSplitAction_ = toolbar->addAction(tr("Re-split Languages"));
    reSplitAction_->setShortcut(QKeySequence(Qt::Key_R));
    reSplitAction_->setToolTip(tr("Discard the manual language marks and re-run detection"));
    connect(reSplitAction_, &QAction::triggered, this, &MainWindow::reSplit);

    QAction * markWord = toolbar->addAction(tr("Mark Word"));
    markWord->setShortcut(QKeySequence(Qt::Key_W));
    markWord->setToolTip(tr("Fix the selected run as one word"));
    connect(markWord, &QAction::triggered, this, &MainWindow::markWord);

    QAction * clearWordsAction = toolbar->addAction(tr("Clear Words"));
    connect(clearWordsAction, &QAction::triggered, this, &MainWindow::clearWords);

    toolbar->addSeparator();

    QAction * pinAction = toolbar->addAction(tr("Set Pronunciation…"));
    pinAction->setShortcut(QKeySequence(Qt::Key_P));
    pinAction->setToolTip(tr("Pin the final phonemes for the selection"));
    connect(pinAction, &QAction::triggered, this, &MainWindow::pinPronunciation);

    QAction * insertAction = toolbar->addAction(tr("Insert Phonemes…"));
    insertAction->setShortcut(QKeySequence(Qt::Key_I));
    insertAction->setToolTip(tr("Insert a sound at the cursor that is not a word"));
    connect(insertAction, &QAction::triggered, this, &MainWindow::insertPhonemes);

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
            maxlabel::insert_phoneme(*segment, at, { name.toStdString() });
            applyHighlights();
            refreshPreview();
            refreshStatus();
        });
    }
    QAction * symbolAction = toolbar->addAction(tr("Non-lexical ▾"));
    symbolAction->setMenu(symbolMenu);
    symbolAction->setToolTip(tr("Insert AP / SP / sil / … at the cursor"));

    QAction * clearOverridesAction = toolbar->addAction(tr("Clear Overrides"));
    connect(clearOverridesAction, &QAction::triggered, this, &MainWindow::clearOverrides);

    toolbar->addSeparator();
    toolbar->addAction(tr("·  set language of selection:"));

    // Digit keys set the language of the selection — the manual half of the
    // segmentation, and the reason the ambiguity highlight exists.
    QAction * zh = toolbar->addAction(tr("1 zh"));
    zh->setShortcut(QKeySequence(Qt::Key_1));
    connect(zh, &QAction::triggered, this, &MainWindow::setSelectionLanguageZh);

    QAction * ja = toolbar->addAction(tr("2 ja"));
    ja->setShortcut(QKeySequence(Qt::Key_2));
    connect(ja, &QAction::triggered, this, &MainWindow::setSelectionLanguageJa);

    QAction * en = toolbar->addAction(tr("3 en"));
    en->setShortcut(QKeySequence(Qt::Key_3));
    connect(en, &QAction::triggered, this, &MainWindow::setSelectionLanguageEn);

    QAction * ko = toolbar->addAction(tr("4 ko"));
    ko->setShortcut(QKeySequence(Qt::Key_4));
    connect(ko, &QAction::triggered, this, &MainWindow::setSelectionLanguageKo);

    // --- central widget -----------------------------------------------------
    QSplitter * splitter = new QSplitter(this);

    list_ = new QListWidget(splitter);
    list_->setMinimumWidth(220);
    connect(list_, &QListWidget::currentRowChanged, this, &MainWindow::onRowChanged);

    // Aegisub's arrangement: the audio on top, the controls in a bar under it,
    // then the text.  The audio is full width because a spectrogram reads time
    // along X — in a tall thin pane it is unreadable, and that is the pane's
    // whole job.
    QSplitter * column = new QSplitter(Qt::Vertical, splitter);

    audio_ = new AudioPanel(column);
    audio_->setMinimumHeight(150);
    connect(audio_, &AudioPanel::statusMessage, this, [this](const QString & message) {
        status_->setText(message);
    });

    QWidget * controls = new QWidget(column);
    audioControls_ = controls;
    QHBoxLayout * controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(4, 2, 4, 2);
    controlsLayout->setSpacing(2);

    const auto addButton = [&](const QString & glyph, const QString & tip,
                               const std::function<void()> & action, bool checkable = false) {
        QToolButton * button = new QToolButton(controls);
        button->setText(glyph);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setCheckable(checkable);
        // No focus, so the transport never steals the keyboard from the editor.
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, action);
        controlsLayout->addWidget(button);
        return button;
    };

    addButton(QStringLiteral("▶"), tr("Play the selection, or the whole file (Ctrl+Space)"),
              [this]() { audio_->playSelectionOrAll(); });
    addButton(QStringLiteral("■"), tr("Stop"), [this]() { audio_->stop(); });
    controlsLayout->addSpacing(10);
    addButton(QStringLiteral("«"), tr("Back half a second (Q)"),
              [this]() { audio_->nudge(-0.5); });
    addButton(QStringLiteral("»"), tr("Forward half a second (W)"),
              [this]() { audio_->nudge(0.5); });
    controlsLayout->addSpacing(10);
    addButton(QStringLiteral("≋"), tr("Waveform / spectrum"), [this]() { audio_->toggleMode(); },
              true);
    controlsLayout->addSpacing(10);
    addButton(QStringLiteral("⌫"), tr("Clear the selection"),
              [this]() { audio_->view()->clearSelection(); });

    controlsLayout->addStretch(1);
    QLabel * readout = new QLabel(format_time(0.0), controls);
    controlsLayout->addWidget(readout);
    connect(audio_, &AudioPanel::positionChanged, this, [readout](double seconds) {
        readout->setText(format_time(seconds));
    });

    // No audio, no audio area: an empty waveform and a dead transport take up
    // room and say nothing.
    connect(audio_, &AudioPanel::audioAvailabilityChanged, this, [this](bool available) {
        audio_->setVisible(available);
        if (audioControls_ != nullptr) audioControls_->setVisible(available);
    });
    audio_->setVisible(false);
    controls->setVisible(false);

    editor_ = new QPlainTextEdit(column);
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor_->setPlaceholderText(tr("The lyric line.  Language is detected per script; "
                                   "select a run and press 1-4 to decide it yourself."));
    connect(editor_, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);

    preview_ = new QPlainTextEdit(column);
    preview_->setReadOnly(true);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    preview_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    preview_->setPlaceholderText(tr("The PFML that will be written for the aligner."));

    column->addWidget(audio_);
    column->addWidget(controls);
    column->addWidget(editor_);
    column->addWidget(preview_);
    column->setStretchFactor(0, 3);
    column->setStretchFactor(1, 0);
    column->setStretchFactor(2, 4);
    column->setStretchFactor(3, 1);

    splitter->addWidget(list_);
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
    statusBar()->addWidget(status_);

    refreshStatus();
    updateActions();
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
        QMessageBox::warning(this, tr("MaxLabel"), QString::fromUtf8(error.what()));
        return;
    }
    current_ = -1;
    refreshList();
    if (!project_.segments.empty()) selectRow(0);
    refreshStatus();
    updateActions();
}

void MainWindow::refreshList() {
    loading_ = true;
    list_->clear();
    for (const maxlabel::Segment & segment : project_.segments) {
        QString label = QString::fromStdString(segment.id);
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
    if (!has_manual_spans(segment->spans)) {
        segment->text = editor_->toPlainText().toStdString();
        maxlabel::detect_spans(*segment);
        maxlabel::rebuild_pfml(*segment);
    }
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
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::markWord() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    const QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
        status_->setText(tr("Select the run to fix as one word."));
        return;
    }
    const QString text = editor_->toPlainText();
    const std::size_t begin = byte_offset_of(text, cursor.selectionStart());
    const std::size_t end   = byte_offset_of(text, cursor.selectionEnd());

    maxlabel::add_word(*segment, begin, end);
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::clearWords() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    maxlabel::clear_words(*segment);
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::loadVocabulary(const QString & path) {
    std::string error;
    if (!vocabulary_.load(path.toStdString(), &error)) {
        QMessageBox::warning(this, tr("MaxLabel"), QString::fromUtf8(error.c_str()));
        return;
    }
    applyHighlights();
    refreshStatus();
}

void MainWindow::loadG2P(const QString & config_json, const QString & dictionary_dir) {
    std::string error;
    if (!g2p_.load(config_json.toStdString(), dictionary_dir.toStdString(), &error)) {
        QMessageBox::warning(this, tr("MaxLabel"), QString::fromUtf8(error.c_str()));
        return;
    }
    refreshStatus();
}

void MainWindow::pinPronunciation() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

    const QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
        status_->setText(tr("Select the run to pin a pronunciation for."));
        return;
    }
    const QString text = editor_->toPlainText();
    const std::size_t begin = byte_offset_of(text, cursor.selectionStart());
    const std::size_t end   = byte_offset_of(text, cursor.selectionEnd());

    PronunciationDialog dialog(cursor.selectedText(),
                               QString::fromStdString(maxlabel::language_at(*segment, begin)),
                               false, &g2p_, &vocabulary_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const std::vector<std::string> phonemes = dialog.phonemes();
    if (phonemes.empty()) return;

    maxlabel::set_override(*segment, begin, end, std::string(),
                           dialog.script().toStdString(), phonemes);
    applyHighlights();
    refreshPreview();
    refreshStatus();
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

    maxlabel::insert_phoneme(*segment, at, phonemes);
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::clearOverrides() {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;
    maxlabel::clear_overrides(*segment);
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::applyHighlights() {
    const maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return;

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
            format.setUnderlineColor(QColor(0xC0, 0x39, 0x2B));
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
        format.setUnderlineColor(QColor(0x2C, 0x3E, 0x50));
        selection.format = format;
        selections.push_back(selection);
    }

    // Overrides: a dotted underline says "the phonemes here are written by
    // hand".  A phoneme the vocabulary does not know turns it into a red wavy
    // one, because that is the case the aligner would fail on — and finding
    // out here is the whole reason to check.
    for (const maxlabel::Override & override_ : segment->overrides) {
        if (override_.inserts()) continue;   // no text to underline
        const int begin = utf16_offset_of(text, override_.begin);
        const int end   = utf16_offset_of(text, override_.end);
        if (end <= begin) continue;

        const bool unknown = has_unknown_phoneme(
            vocabulary_, override_.phonemes, maxlabel::language_at(*segment, override_.begin));

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor_->document());
        selection.cursor.setPosition(begin);
        selection.cursor.setPosition(end, QTextCursor::KeepAnchor);

        QTextCharFormat format;
        if (unknown) {
            format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            format.setUnderlineColor(QColor(0xC0, 0x39, 0x2B));
        } else {
            format.setUnderlineStyle(QTextCharFormat::DotLine);
            format.setUnderlineColor(QColor(0x7F, 0x5C, 0x00));
        }
        selection.format = format;
        selections.push_back(selection);
    }

    editor_->setExtraSelections(selections);
}

void MainWindow::refreshPreview() {
    const maxlabel::Segment * segment = currentSegment();
    preview_->setPlainText(segment == nullptr ? QString()
                                              : QString::fromStdString(segment->pfml));
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

    maxlabel::set_span_language(*segment, begin, end, language.toStdString());
    applyHighlights();
    refreshPreview();
    refreshStatus();
}

void MainWindow::setSelectionLanguageZh() { setSelectionLanguage(QStringLiteral("zh")); }
void MainWindow::setSelectionLanguageJa() { setSelectionLanguage(QStringLiteral("ja")); }
void MainWindow::setSelectionLanguageEn() { setSelectionLanguage(QStringLiteral("en")); }
void MainWindow::setSelectionLanguageKo() { setSelectionLanguage(QStringLiteral("ko")); }

bool MainWindow::commitCurrent(bool quiet) {
    maxlabel::Segment * segment = currentSegment();
    if (segment == nullptr) return true;

    const std::string text = editor_->toPlainText().toStdString();
    if (text == segment->text) return true;   // nothing to do

    maxlabel::Segment candidate = *segment;
    candidate.text = text;
    // Offsets from a previous text no longer describe this one, so the spans
    // are re-derived rather than sliced against the wrong string.
    maxlabel::detect_spans(candidate);
    maxlabel::rebuild_pfml(candidate);
    try {
        maxlabel::save(candidate);
    } catch (const std::exception & error) {
        if (!quiet) {
            QMessageBox::warning(this, tr("PFML did not parse"),
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
    if (commitCurrent(false)) {
        refreshList();
        applyHighlights();
        refreshPreview();
        refreshStatus();
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
