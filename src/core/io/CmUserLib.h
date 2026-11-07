#pragma once

#include "core/io/CmDeviceLib.h"

#include "core/io/CktFile.h"

#include <QPoint>
#include <QStringList>
#include <QVector>

namespace cm {

// A macro of CircuitMaker 2000's User.lib: the circuit inside and how the
// macro's pins reach it.
struct CmMacro {
    QString name;               // "Delay", "Timer~"
    QStringList pins;           // port name of CircuitMaker pin 1, 2, ... (unique)
    QVector<bool> outputs;      // pins drawn on the right
    QVector<QPoint> at;         // pin positions on CircuitMaker's symbol
    QVector<QPoint> from;       // where each pin's lead starts
    QStringList shownNames;     // pin names as drawn (empty: none shown)
    QVector<bool> bubbles;      // inverting pins: a bubble against the body
    int flags = 0;              // the header's (0x80: the name shown inside)
    QVector<CmShape> drawing;   // CircuitMaker's drawing of the symbol
    QVector<int> pinNets;       // the inner net each pin joins
    ckt::CktFile body;          // inner devices and wires, as a .ckt file holds them
};

// Reads the macros of the User.lib of the user's CircuitMaker installation
// (not distributed with this program). Each macro is the library header of
// its symbol followed by its circuit, written as CircuitMaker holds it in
// memory: a device count, then per device a 15-byte library name and a
// 0xf2-byte record (type, position, pin count and nets, string offsets,
// package map, angle at +0x40) with its arrays, the first device being the
// macro itself, whose nets name the inner net of each pin; then the wires,
// 72 bytes each (pins, devices and wires the ends lie on), and their points.
class CmUserLib {
public:
    bool load(const QString& path, QString* error = nullptr);
    const CmMacro* find(const QString& name) const;     // case and a trailing '~' ignored
    int size() const { return int(m_macros.size()); }

    // User.lib beside the Device.lib found by CmDeviceLib::locate
    static QString locate(const QString& configuredFolder = {});

private:
    QVector<CmMacro> m_macros;
};

} // namespace cm
