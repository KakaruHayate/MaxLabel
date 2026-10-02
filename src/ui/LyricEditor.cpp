// The lyric box — see LyricEditor.h.

#include "LyricEditor.h"

#include <QFontMetrics>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>

namespace {

// The same warm amber the rest of the tool uses for "written by hand", lifted
// enough to read as text on the dark panel.
const QColor kInsertText(0xE8, 0xA9, 0x4A);
const QColor kInsertFill(0x35, 0x28, 0x14, 235);

}  // namespace

LyricEditor::LyricEditor(QWidget * parent) : QPlainTextEdit(parent) {
    // An empty lane above the first line for the insertion tags to sit in.
    // Drawing them in the leading would work for one line and collide with
    // the line above for two.
    setViewportMargins(0, tagLane(), 0, 0);
}

int LyricEditor::tagLane() const {
    QFont labelFont = font();
    labelFont.setPointSizeF(std::max(7.0, font().pointSizeF() * 0.55));
    labelFont.setBold(true);
    return QFontMetrics(labelFont).height() + 6;
}

void LyricEditor::setInsertions(const std::vector<Insertion> & insertions) {
    insertions_ = insertions;
    viewport()->update();
}

void LyricEditor::paintEvent(QPaintEvent * event) {
    QPlainTextEdit::paintEvent(event);
    if (insertions_.empty()) return;

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont labelFont = font();
    labelFont.setPointSizeF(std::max(7.0, font().pointSizeF() * 0.55));
    labelFont.setBold(true);
    const QFontMetrics metrics(labelFont);

    const int last = std::max(0, document()->characterCount() - 1);
    for (const Insertion & insertion : insertions_) {
        QTextCursor cursor(document());
        cursor.setPosition(std::min(std::max(0, insertion.position), last));
        const QRect caret = cursorRect(cursor);

        // The bar says where the sound goes; the tag says what it is.  The tag
        // goes in the lane reserved above the text, so it never covers a
        // character or the line above.
        painter.setPen(Qt::NoPen);
        painter.setBrush(kInsertText);
        painter.drawRect(QRect(caret.left(), caret.top(), 2, caret.height()));

        const QString text = QStringLiteral("+") + insertion.label;
        const int width = metrics.horizontalAdvance(text) + 8;
        const int height = metrics.height() + 1;
        const int x = std::min(caret.left() + 3, viewport()->width() - width - 1);
        const QRect box(x, std::max(0, caret.top() - height - 1), width, height);

        painter.setBrush(kInsertFill);
        painter.setPen(QPen(kInsertText, 1));
        painter.drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        painter.setPen(kInsertText);
        painter.setFont(labelFont);
        painter.drawText(box, Qt::AlignCenter, text);
    }
}
