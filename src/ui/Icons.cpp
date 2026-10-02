// Icons — see Icons.h.

#include "Icons.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace maxlabel::ui {

namespace {

// Rendered at twice the requested size with the ratio set, so one file is
// sharp on a HiDPI screen without shipping a second one.
constexpr int kScale = 2;

QPixmap render(const QString & name, const QColor & colour, int size) {
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return QPixmap();

    QString svg = QString::fromUtf8(file.readAll());
    svg.replace(QStringLiteral("#ffffff"), colour.name(QColor::HexRgb), Qt::CaseInsensitive);

    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) return QPixmap();

    QImage image(size * kScale, size * kScale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter);
    }
    image.setDevicePixelRatio(kScale);
    return QPixmap::fromImage(image);
}

}  // namespace

QIcon icon(const QString & name, const QColor & colour, int size) {
    const QPixmap pixmap = render(name, colour, size);
    return pixmap.isNull() ? QIcon() : QIcon(pixmap);
}

QIcon icon(const QString & name, const QColor & colour, const QColor & checked, int size) {
    QIcon result;
    const QPixmap off = render(name, colour, size);
    const QPixmap on = render(name, checked, size);
    if (!off.isNull()) result.addPixmap(off, QIcon::Normal, QIcon::Off);
    if (!on.isNull()) result.addPixmap(on, QIcon::Normal, QIcon::On);
    return result;
}

QIcon swatch(const QColor & colour, int size) {
    if (!colour.isValid()) return QIcon();

    const int side = size * kScale;
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // A lighter edge, because a dark tint on a dark button would otherwise
        // lose its shape and read as a smudge.
        painter.setPen(QColor(colour).lighter(170));
        painter.setBrush(colour);
        const qreal inset = kScale * 0.5;
        painter.drawRoundedRect(QRectF(inset, inset, side - 2 * inset, side - 2 * inset),
                                side * 0.22, side * 0.22);
    }
    image.setDevicePixelRatio(kScale);
    return QIcon(QPixmap::fromImage(image));
}

}  // namespace maxlabel::ui
