#pragma once

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

namespace cm {

// One device of CircuitMaker 2000's Device.lib, as far as a .ckt record needs it.
struct CmLibDevice {
    QString name;               // "2-In NAND~", "74LS161"
    int type = 0;
    int pinCount = 0;           // the schematic pins, CircuitMaker pin 1..pinCount
    QVector<QPoint> pins;       // pin positions relative to the origin, index 0 = pin 1
    QVector<int> packageMap;    // 0, the pins of part A, then every part's pins (gates), ...
    int parts = 0;              // gates in one package (0 or 1: not a multi-part package)
    int code = 0;               // the letter stored in each instance ('A', 'R', 'V', ...)
    int flags = 0;
    QRect valueRect;
    QString prefix;             // designator prefix
    QString power, spice, value, kind, package;
};

// Reads Device.lib from the user's own CircuitMaker 2000 installation; the
// library is not distributed with this program. Records are found by
// validating every candidate header: a 0xfe-byte block (type at +0, name at
// +0xc, pin count at +0x20, package map count at +0x94, part count at +0xb6,
// prefix at +0x9c) followed by the pins (0x14 bytes, then a counted name),
// the package map and a block of NUL-separated strings whose offsets for
// value, kind and package are at +0x5c, +0xd2 and +0xd6.
class CmDeviceLib {
public:
    bool load(const QString& path, QString* error = nullptr);
    bool isLoaded() const { return !m_devices.isEmpty(); }
    const CmLibDevice* find(const QString& name) const;
    // The device whose default value is this part number ("7400" -> "2-In NAND~").
    const CmLibDevice* findByValue(const QString& value) const;
    int size() const { return int(m_devices.size()); }

    // Device.lib of a CircuitMaker 2000 installation: the configured folder,
    // else the usual Wine and Windows install locations; empty if none.
    static QString locate(const QString& configuredFolder = {});

private:
    QVector<CmLibDevice> m_devices;
    QHash<QString, int> m_byName;           // lower case
    QHash<QString, int> m_byValue;          // lower case
};

} // namespace cm
