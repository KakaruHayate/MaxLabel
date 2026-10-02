#pragma once

// The lyric box, with one addition to QPlainTextEdit: inserted phonemes.
//
// An insertion has no width.  It sits *between* two characters and is not part
// of the text, so there is nowhere in the document to put it — the only thing
// a plain editor can do is tint the character next to it, which says that
// something is there without saying what.  The whole point of inserting a pad
// nasal is that a person decided the singer put an `n` in; a mark that does
// not name it is not a record of that decision.
//
// So the editor draws them itself: a warm bar at the position and the phoneme
// spelled out beside it, above the text so it does not fight with the
// characters it sits between.

#include <QPlainTextEdit>
#include <QString>

#include <vector>

class LyricEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit LyricEditor(QWidget * parent = nullptr);

    struct Insertion {
        int     position = 0;   // UTF-16 offset, as the document counts
        QString label;          // the phonemes, already joined
    };

    void setInsertions(const std::vector<Insertion> & insertions);

protected:
    void paintEvent(QPaintEvent * event) override;

private:
    int tagLane() const;

    std::vector<Insertion> insertions_;
};
