// The character strip — see RunStrip.h.

#include "RunStrip.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>

namespace {

constexpr int kGap = 4;          // between blocks
constexpr int kPaddingX = 6;     // inside a block, either side

// The same three state colours the text pane uses, so a word boundary or a
// wrong phoneme reads the same in both views.
const QColor kAccent(0xE9, 0x1E, 0x63);
const QColor kWord(0x00, 0xBC, 0xD4);
const QColor kPinned(0xFF, 0xB3, 0x00);
const QColor kWarn(0xF4, 0x43, 0x36);
// An inserted sound: a shape of its own rather than a language tint, because
// it is not part of the text and must not be mistaken for a character.
const QColor kInsert(0xE8, 0xA9, 0x4A);

}  // namespace

RunStrip::RunStrip(QWidget * parent) : QWidget(parent) {
    needed_height_ = rowHeight() + 8;
    setMinimumHeight(needed_height_);
    setMouseTracking(false);
}

QFont RunStrip::labelFont() const {
    QFont font_ = font();
    font_.setPointSizeF(font_.pointSizeF() + 3.5);
    return font_;
}

QFont RunStrip::readingFont() const {
    QFont font_ = font();
    font_.setPointSizeF(std::max(6.0, font_.pointSizeF() - 0.5));
    return font_;
}

int RunStrip::labelHeight() const { return QFontMetrics(labelFont()).height() + 4; }
int RunStrip::readingHeight() const { return QFontMetrics(readingFont()).height() + 2; }
int RunStrip::rowHeight() const { return labelHeight() + readingHeight() + 8; }

void RunStrip::setChips(const std::vector<Chip> & chips) {
    chips_ = chips;
    current_first_ = current_last_ = -1;
    relayout();
    update();
}

void RunStrip::setCurrent(std::size_t begin, std::size_t end) {
    current_first_ = current_last_ = -1;
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        if (chips_[i].begin >= begin && chips_[i].end <= end) {
            if (current_first_ < 0) current_first_ = static_cast<int>(i);
            current_last_ = static_cast<int>(i);
        }
    }
    update();
}

void RunStrip::clearCurrent() {
    if (current_first_ < 0 && current_last_ < 0) return;
    current_first_ = current_last_ = -1;
    update();
}

void RunStrip::relayout() {
    rects_.clear();
    if (chips_.empty()) {
        needed_height_ = rowHeight() + 8;
        setMinimumHeight(needed_height_);
        if (needed_height_ != announced_height_) {
            announced_height_ = needed_height_;
            emit heightNeeded(needed_height_);
        }
        return;
    }

    const QFontMetrics labelMetrics(labelFont());
    const QFontMetrics readingMetrics(readingFont());
    const int row = rowHeight();
    const int available = std::max(1, width() - 8);

    int x = 4;
    int y = 4;
    for (const Chip & chip : chips_) {
        const int width = std::max(labelMetrics.horizontalAdvance(chip.label),
                                   readingMetrics.horizontalAdvance(chip.reading)) +
                          kPaddingX * 2;
        if (x + width > available && x > 4) {   // wrap, but never a lone block
            x = 4;
            y += row;
        }
        rects_.push_back(QRect(x, y, width, row));
        x += width + kGap;
    }
    needed_height_ = y + row + 4;
    setMinimumHeight(needed_height_);
    if (needed_height_ != announced_height_) {
        announced_height_ = needed_height_;
        emit heightNeeded(needed_height_);
    }
}

void RunStrip::resizeEvent(QResizeEvent * event) {
    QWidget::resizeEvent(event);
    relayout();
}

int RunStrip::chip_index_at(int x, int y) const {
    for (std::size_t i = 0; i < rects_.size(); ++i) {
        if (rects_[i].contains(x, y)) return static_cast<int>(i);
    }
    return -1;
}

void RunStrip::mousePressEvent(QMouseEvent * event) {
    if (event->button() != Qt::LeftButton) return;
    const int index = chip_index_at(static_cast<int>(event->position().x()),
                                    static_cast<int>(event->position().y()));
    if (index < 0) return;
    const Chip & chip = chips_[static_cast<std::size_t>(index)];
    // An inserted sound covers no text, so there is no range to select; the
    // double click is what acts on it.
    if (chip.insert) return;
    anchor_ = index;
    current_first_ = current_last_ = index;
    update();
    emit chipClicked(chip.begin, chip.end);
}

void RunStrip::mouseMoveEvent(QMouseEvent * event) {
    if (anchor_ < 0) return;   // no button down: not a drag
    const int index = chip_index_at(static_cast<int>(event->position().x()),
                                    static_cast<int>(event->position().y()));
    if (index < 0) return;
    current_first_ = std::min(anchor_, index);
    current_last_  = std::max(anchor_, index);
    update();
    // A word is usually longer than one character, so a selection has to be
    // able to span blocks: press on the first, drag to the last.  This is a
    // range the click alone cannot express, and `<word>` needs the range.
    emit chipClicked(chips_[static_cast<std::size_t>(current_first_)].begin,
                     chips_[static_cast<std::size_t>(current_last_)].end);
}

void RunStrip::mouseReleaseEvent(QMouseEvent * event) {
    if (event->button() == Qt::LeftButton) anchor_ = -1;
}

void RunStrip::mouseDoubleClickEvent(QMouseEvent * event) {
    if (event->button() != Qt::LeftButton) return;
    const int index = chip_index_at(static_cast<int>(event->position().x()),
                                    static_cast<int>(event->position().y()));
    if (index < 0) return;
    emit chipActivated(chips_[static_cast<std::size_t>(index)].begin,
                       chips_[static_cast<std::size_t>(index)].end);
}

void RunStrip::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x1E, 0x1E, 0x1E));

    if (chips_.empty()) {
        painter.setPen(QColor(0x8A, 0x8F, 0x98));
        painter.drawText(rect(), Qt::AlignCenter, tr("Nothing to annotate."));
        return;
    }

    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const Chip & chip = chips_[i];
        const QRect & rect = rects_[i];
        const bool current = static_cast<int>(i) >= current_first_ &&
                             static_cast<int>(i) <= current_last_;

        // An inserted sound is not a character: it gets a small tag in the
        // warm colour instead of a language block, so it cannot be mistaken
        // for text the author typed.
        if (chip.insert) {
            const QColor edge = chip.unknown ? kWarn : kInsert;
            painter.setPen(QPen(edge, 1));
            painter.setBrush(QColor(0x35, 0x28, 0x14));
            painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 3, 3);
            painter.setPen(edge);
            painter.setFont(readingFont());
            painter.drawText(rect, Qt::AlignCenter, chip.label);
            continue;
        }

        // The block: its language tint, stronger when it is the one selected.
        QColor fill = chip.colour;
        if (current) fill = kAccent;
        painter.setPen(QColor(0x3E, 0x3E, 0x42));
        painter.setBrush(fill);
        painter.drawRoundedRect(rect, 3, 3);

        // The states that are about *this* block rather than the language: a
        // bar along the bottom for a fixed word, an outline for a written
        // pronunciation — the same two signals the text pane carries, in the
        // same colours.
        if (chip.word) {
            QPen pen(kWord, 2);
            painter.setPen(pen);
            painter.drawLine(rect.left() + 3, rect.bottom() - 1,
                             rect.right() - 3, rect.bottom() - 1);
        }
        if (chip.pinned || chip.unknown) {
            QPen pen(chip.unknown ? kWarn : kPinned, 1, Qt::DotLine);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 3, 3);
        }

        // The character, and the reading above it when there is one.  A block
        // with nothing pinned keeps its character centred in the whole block,
        // so the row reads as one line of text with readings annotated on top
        // of it — rather than as characters hanging from the tops of boxes
        // with an unexplained gap underneath.
        if (chip.reading.isEmpty()) {
            painter.setPen(current ? QColor(0xFF, 0xFF, 0xFF) : QColor(0xE8, 0xE8, 0xE8));
            painter.setFont(labelFont());
            painter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, chip.label);
        } else {
            painter.setPen(chip.unknown ? QColor(0xFF, 0xB0, 0xAB) : QColor(0xFF, 0xD9, 0xEC));
            painter.setFont(readingFont());
            const QRect readingRect(rect.x(), rect.y() + 2, rect.width(), readingHeight());
            painter.drawText(readingRect, Qt::AlignHCenter | Qt::AlignVCenter, chip.reading);

            painter.setPen(current ? QColor(0xFF, 0xFF, 0xFF) : QColor(0xE8, 0xE8, 0xE8));
            painter.setFont(labelFont());
            const QRect labelRect(rect.x(), rect.y() + readingHeight() + 2, rect.width(),
                                  rect.height() - readingHeight() - 2);
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter, chip.label);
        }
    }
}
