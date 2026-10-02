#pragma once

// Pick or type the final phonemes for a run of text.
//
// Two ways in, one way out.  When a G2P pipeline is configured the dialog lists
// the pronunciations the dictionary already knows and the author picks one;
// without it they type the phonemes.  Both produce the same Override, so
// nothing downstream can tell which route was taken.
//
// The verdict line is the point of the vocabulary: a phoneme the model cannot
// resolve would make the aligner fail on that sample, so it is flagged here
// rather than at alignment time.

#include "maxlabel/g2p_context.h"
#include "maxlabel/vocabulary.h"

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;

class PronunciationDialog : public QDialog {
    Q_OBJECT

public:
    PronunciationDialog(const QString & selected_text,
                        const QString & language,
                        bool insertion,
                        const maxlabel::G2PContext * g2p,
                        const maxlabel::Vocabulary * vocabulary,
                        QWidget * parent = nullptr);

    // Pre-fill with what is already pinned to this run, so the dialog shows
    // the decision it is about to replace rather than an empty form.
    void setExisting(const QString & script, const QString & phonemes);

    // True when the author asked for the pronunciation to be taken off rather
    // than set.  The dialog cannot do it itself — the model is not its to
    // touch — so it reports and the caller applies.
    bool removalRequested() const { return removed_; }

    QString script() const;
    std::vector<std::string> phonemes() const;

private slots:
    void useSelectedCandidate();
    void revalidate();

private:
    void fillFrom(int row);

    QListWidget * candidates_ = nullptr;
    QLineEdit *   script_     = nullptr;
    QLineEdit *   phonemes_   = nullptr;
    QLabel *      verdict_    = nullptr;

    const maxlabel::Vocabulary * vocabulary_ = nullptr;
    std::string language_;
    bool removed_ = false;
};
