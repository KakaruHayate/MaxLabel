#pragma once

// MaxLabel main window.
//
// Text-first: the editor edits the lyric line, and the PFML is a generated
// artifact shown read-only underneath it.  That is the whole point of the
// tool — PFML is the wire format the aligner wants, not something a human
// should be typing, and every annotation (language span, later word boundary
// and pronunciation) is an annotation *on the text*.
//
// The language spans are drawn as background colours, and a span the script
// could not settle is drawn with a wavy underline instead of a colour: the
// difference between "this is English" and "this might be Chinese or Japanese"
// has to be visible at a glance, because only the second one needs a decision.
//
// Data safety: navigation and saving commit the editor back into the model and
// write the segment, so there is never a pending edit to lose.

#include "maxlabel/core.h"
#include "maxlabel/g2p_context.h"
#include "maxlabel/vocabulary.h"

#include <QMainWindow>
#include <QString>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QAction;
class QTimer;
class AudioPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget * parent = nullptr);

    // Open a folder without the dialog — what the command line uses.
    void loadDirectory(const QString & directory);

    // Both optional: with a vocabulary the phonemes are checked, and with a
    // G2P pipeline the pronunciation dialog can offer the dictionary's own
    // answers.  Without them everything still works, by hand.
    void loadVocabulary(const QString & path);
    void loadG2P(const QString & config_json, const QString & dictionary_dir);
    // Builds the config from a model directory and loads it.
    void loadG2PDirectory(const QString & model_dir);

    // Development aid: start in spectrum mode, so the renderer can be looked at.
    void setSpectrumMode(bool spectrum);

private slots:
    void openDirectory();
    void onRowChanged(int row);
    void onTextChanged();
    void saveCurrent();
    void goPrevious();
    void goNext();
    void reSplit();
    void markWord();
    void clearWords();
    void pinPronunciation();
    void insertPhonemes();
    void clearOverrides();
    void undo();
    void redo();
    void setSelectionLanguage(const QString & language);

protected:
    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y are intercepted at the editor, because a
    // text widget that has its own (disabled) undo still eats the key event
    // before a window-level shortcut can see it.
    bool eventFilter(QObject * watched, QEvent * event) override;

private:
    // Undo is whole-segment snapshots.  Aegisub's model, and the one that fits:
    // the annotations are a handful of small vectors, so a snapshot is cheap,
    // and there is no inverse operation to get wrong.
    struct Snapshot {
        std::string text;
        std::vector<maxlabel::LangSpan> spans;
        std::vector<maxlabel::WordBoundary> words;
        std::vector<maxlabel::Override> overrides;
        std::string pfml;
    };
    struct History {
        std::vector<Snapshot> undo;
        std::vector<Snapshot> redo;
    };

    Snapshot snapshot() const;
    void restore(const Snapshot & state);
    // `typing` marks a keystroke, so a burst of them collapses into one step.
    void pushHistory(bool typing);
    // Save and refresh: the tail every structural edit ends with.
    void commitEdit();
    // A message on the status line for a moment, then the summary returns.
    void showStatus(const QString & message, const char * state, int milliseconds = 4000);
    void updateHistoryActions();

    // Editor -> model, then model -> disk.  Returns false (and leaves the model
    // untouched) when the resulting PFML does not parse.
    bool commitCurrent(bool quiet);
    void selectRow(int row);
    void refreshList();
    void refreshSpansFromText();
    void applyHighlights();
    void refreshPreview();
    void refreshStatus();
    void updateActions();
    const maxlabel::Segment * currentSegment() const;
    maxlabel::Segment * currentSegment();

    maxlabel::Project project_;
    int  current_  = -1;
    bool loading_  = false;   // guards the signals fired while repopulating

    maxlabel::G2PContext g2p_;
    maxlabel::Vocabulary vocabulary_;

    // Keyed by segment id, so navigating away and back does not throw the
    // history away.
    std::map<std::string, History> histories_;
    bool     lastPushWasTyping_ = false;
    qint64   lastPushMs_ = 0;
    QTimer *  typingSaveTimer_ = nullptr;
    QTimer *  statusTimer_ = nullptr;
    QAction * undoAction_ = nullptr;
    QAction * redoAction_ = nullptr;

    // The language actions, built once from the language table: the rail shows
    // them and so does the quick bar, and both drive the same action.
    struct LanguageEntry {
        QString id;
        QAction * action;
    };
    std::vector<LanguageEntry> languageActions_;

    QListWidget *   list_       = nullptr;
    QPlainTextEdit * editor_    = nullptr;
    QPlainTextEdit * preview_   = nullptr;
    AudioPanel *    audio_      = nullptr;
    QWidget *       audioControls_ = nullptr;
    QLabel *        status_     = nullptr;
    QAction *       saveAction_ = nullptr;
    QAction *       prevAction_ = nullptr;
    QAction *       nextAction_ = nullptr;
    QAction *       reSplitAction_ = nullptr;
};
