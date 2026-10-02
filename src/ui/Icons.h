#pragma once

// Icons, from the SVGs embedded in the binary.
//
// The files are single-colour and carry a placeholder, because Qt's SVG
// renderer has no notion of the current text colour.  Substituting it before
// rendering is what lets one file serve every state and every size — a grey
// glyph on a flat button, a white one on the accent, all from the same source.

#include <QColor>
#include <QIcon>
#include <QString>

namespace maxlabel::ui {

// `colour` is the resting colour; the accent colour for the icons the theme
// draws on a light background is the caller's business.
QIcon icon(const QString & name, const QColor & colour = QColor(0xCC, 0xCC, 0xCC),
           int size = 18);

// The same, with a colour for the checked state: a toggled button has the
// accent behind it, where the resting grey reads as disabled.
QIcon icon(const QString & name, const QColor & colour, const QColor & checked, int size);

// A legend swatch: a language tint as a small rounded square.  A colour the
// reader has to infer from the text is one they cannot look up, and five tints
// cannot be learned from having seen them once.
QIcon swatch(const QColor & colour, int size = 13);

}  // namespace maxlabel::ui
