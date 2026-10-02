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

#include <vector>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QAction;

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
    void setSelectionLanguageZh();
    void setSelectionLanguageJa();
    void setSelectionLanguageEn();
    void setSelectionLanguageKo();

private:
    void setSelectionLanguage(const QString & language);

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

    QListWidget *   list_       = nullptr;
    QPlainTextEdit * editor_    = nullptr;
    QPlainTextEdit * preview_   = nullptr;
    QLabel *        status_     = nullptr;
    QAction *       saveAction_ = nullptr;
    QAction *       prevAction_ = nullptr;
    QAction *       nextAction_ = nullptr;
    QAction *       reSplitAction_ = nullptr;
};
