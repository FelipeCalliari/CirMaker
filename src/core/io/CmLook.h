#pragma once

#include <QJsonArray>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

namespace cm {

struct CmShape;
struct CmLibDevice;
class CmDeviceLib;
struct SymbolDef;

// CircuitMaker's look: parts drawn as CircuitMaker 2000 draws them, from the
// drawing in the user's own Device.lib (read at run time, never shipped).
namespace CmLook {

// Device.lib drawing elements as our symbol graphics (JSON), each point
// through `pt`; the other drawing style (flag 0x4000) is left out.
// variant: the value of its bound numbers to draw (kind 12), -1 the first.
QJsonArray shapeGraphics(const QVector<CmShape>& shapes, const std::function<QPointF(QPoint)>& pt, int variant = -1);

// The user's Device.lib, read once (empty when CircuitMaker is not installed).
const CmDeviceLib& deviceLib();

// Whether parts are drawn with CircuitMaker's drawings (a setting, on by default).
bool enabled();
void setEnabled(bool on);

// The symbol drawn as `cmDevice` (else the device the symbol stands for) is
// drawn in CircuitMaker: same pins, model and everything else, CircuitMaker's
// drawing with leads out to our pins. The symbol itself when there is no such
// drawing, when it is drawn live (lamps, switches, displays) or when
// CircuitMaker's pins do not fall on ours. Cached for the program's life.
// label: a part's label that picks its drawing (the VCO's SQRVCO).
const SymbolDef* symbolFor(const SymbolDef& s, const QString& cmDevice = {}, const QString& label = {});

} // namespace CmLook
} // namespace cm
