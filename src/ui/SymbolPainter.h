#pragma once

#include "library/SymbolDef.h"

#include <QColor>
#include <QPainterPath>
#include <QPixmap>

class QPainter;

namespace cm {

namespace SymbolPainter {

// Draws the symbol's static graphics in the painter's current coordinates.
// inside: the part's label for a text that shows it (empty: the text's own,
// the device name), or nullptr when the label is shown elsewhere
void paint(QPainter* p, const SymbolDef& s, const QColor& color, QPointF actuatorOffset = {}, const QString* inside = nullptr);

// Text that stays upright and unmirrored whatever the item transform is.
// align: -1 left, 0 centre, 1 right of `anchor` (local coordinates).
void uprightText(QPainter* p, QPointF anchor, const QString& text, double pointSize, int align,
                 const QColor& color, bool overline = false, bool vcenter = true, bool turns = false);

QPainterPath parsePath(const QString& d);

// Monospaced face used for schematic text, like CircuitMaker's Courier New.
QFont schematicFont(double pointSize, bool bold = false);

QPixmap preview(const SymbolDef& s, QSize size, const QColor& color, const QColor& background);

} // namespace SymbolPainter

} // namespace cm
