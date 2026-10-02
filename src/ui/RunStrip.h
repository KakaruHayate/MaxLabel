#pragma once

// The character strip: the line presented as the units that get annotated.
//
// This is the interaction model the tool had been missing.  "Drag a selection
// in the text, then find the button" needs a precise selection — hard in CJK,
// where there is no word boundary to snap to — and then a trip to the side
// panel.  Here the line is laid out as one block per character (per word for
// Latin), and clicking a block selects it.  What the block shows underneath is
// its current reading, so the result of an annotation is visible where the
// annotation was made rather than as an underline on the text above.

#include <QColor>
#include <QFont>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class RunStrip : public QWidget {
    Q_OBJECT

public:
    explicit RunStrip(QWidget * parent = nullptr);

    struct Chip {
        std::size_t begin = 0;   // byte offsets into the segment text
        std::size_t end   = 0;
        QString     label;       // the character or word
        QString     reading;     // what is pinned to it, or empty
        QColor      colour;      // the language tint
        bool        current = false;
        bool        word = false;      // a fixed word boundary covers it
        bool        pinned = false;    // a pronunciation is pinned to it
        bool        unknown = false;   // that pronunciation is not in the vocabulary
    };

    void setChips(const std::vector<Chip> & chips);
    void setCurrent(std::size_t begin, std::size_t end);

    // The height the blocks need, so a caller can cap it and scroll instead of
    // letting a long line push the text out of the window.
    int neededHeight() const { return needed_height_; }
    int rowHeight() const;

signals:
    // The block that was clicked, as byte offsets into the segment text.
    void chipClicked(std::size_t begin, std::size_t end);

    // How tall the blocks now want to be.  Wrapping depends on the width, so
    // this changes on resize too, not only when the chips do — a caller that
    // only listened to setChips would size itself from a layout made before
    // the widget had a width, and get the wrapping wrong.
    void heightNeeded(int height);

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;

private:
    void relayout();
    const QRect * chip_rect_at(int x, int y) const;

    // The block is the thing being read and clicked, so it is set larger than
    // the app default; the heights follow the fonts rather than being fixed,
    // or a bigger font would be clipped.
    QFont labelFont() const;
    QFont readingFont() const;
    int   labelHeight() const;
    int   readingHeight() const;

    std::vector<Chip>  chips_;
    std::vector<QRect> rects_;       // one per chip, in the same order
    int current_ = -1;               // index into chips_
    int needed_height_ = 0;
    int announced_height_ = -1;
};
