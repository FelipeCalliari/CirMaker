#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

// A CircuitMaker 2000 schematic as read: the records of "CircuitMaker Text"
// files, also built from the macros of CircuitMaker's User.lib.
namespace cm::ckt {

struct Device {
    QString name;
    int type = 0, x = 0, y = 0, angle = 0, flags = 0;
    std::optional<QRectF> valueRect, designatorRect;
    QVector<int> nets;
    QString value, designator, prefix;
    QString kind;               // "type:digital", "alias:XPLLX {Fc=100k Fr=50k}"
    QString tag;                // third label: the relay a coil or contact belongs to ("K1")
    QVector<int> data;
    QVector<int> packagePins;   // DIP pin of CM pin 1, 2, ... (first part of the package)
};

struct CktWire {
    QVector<int> h;
    QVector<QPointF> points;
    // a bus line is attached to no device, carries its name in h[0] (some
    // files repeat it in h[1]) and has flag 0x20; names may be plain numbers
    bool isBus() const
    {
        return h[7] == 0 && h[8] == 0 && ((h[5] & 0x20) || (h[0] < 0 && (h[1] == 0 || h[1] == h[0])));
    }
};

struct CktText {
    QRectF rect;
    QString text;
    int fontHeight = 13;
};

struct CktFile {
    double version = 0;
    QVector<Device> devices;
    QVector<CktWire> wires;
    QVector<CktText> texts;
};

} // namespace cm::ckt
