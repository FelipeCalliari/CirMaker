#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace cm {

// One drawing element of a symbol, in symbol-local coordinates (grid = 10).
struct Primitive {
    enum class Type { Line, Rect, Circle, Path, Text };
    Type type = Type::Line;
    QVector<double> v;      // Line: x1 y1 x2 y2 | Rect: x y w h | Circle: cx cy r | Text: x y
    QString path;           // Path: SVG subset (M L Q C Z, absolute)
    QString text;
    bool fill = false;
    double size = 7;        // text point size
    int align = 0;          // text: -1 left, 0 centre, 1 right
    bool overline = false;  // text: draw a bar above (active-low names)
    bool actuator = false;  // moving part of a switch, drawn displaced while pressed
};

struct SymbolPin {
    enum class Dir { In, Out, InOut, Passive };
    QString name;           // "A", "CLK", "~CLR" (~ = active low)
    QString number;         // package pin number, optional
    QPointF pos;            // connection point
    Dir dir = Dir::Passive;
    int floatValue = 0;     // logic value read when the pin is left unconnected
};

struct SymbolDef {
    QString id;             // stable key, e.g. "and2"
    QString name;           // display name, e.g. "2-In AND"
    QString category;       // "Digital/Gates"
    QString prefix = QStringLiteral("U");
    QString description;
    QStringList aliases;    // CircuitMaker 2000 library names, for .ckt import
    QStringList cmPins;     // our pin name for CircuitMaker pin 1, 2, ... (empty entry: unused)
    QString display;        // dynamic rendering: lamp, hex, ascii, switch, button, pulser, hexkey, ...
    QString defaultLabel;
    bool netLabel = false;  // components with equal labels share a net
    bool hidden = false;    // internal symbol, not offered in the device browser
    QVector<Primitive> graphics;
    QVector<SymbolPin> pins;
    QJsonObject model;      // {"kind": "gate", ...}; empty = no simulation behaviour
    QJsonObject spice;      // {"line": "R{name} {1} {2} {value}", "models": [...], "subckt": [...]}
    QVariantMap defaultProps;
    QRectF bounds;

    int pinIndex(const QString& pinName) const
    {
        for (int i = 0; i < pins.size(); ++i)
            if (pins[i].name == pinName)
                return i;
        return -1;
    }
};

} // namespace cm
