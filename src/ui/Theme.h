#pragma once

#include <QColor>
#include <QString>

namespace cm {

// Schematic colours and fonts (CircuitMaker's Options > Colors, Device Font
// and Text Font), kept in the user settings.
struct Theme {
    QColor background = Qt::white;
    QColor grid = QColor(0xd8, 0xd8, 0xd8);
    QColor device = Qt::black;
    QColor wire = QColor(0x00, 0x30, 0xff);
    QColor bus = Qt::black;
    QColor text = QColor(0x10, 0x10, 0x30);
    QColor selection = QColor(0x00, 0x60, 0xff);
    QColor high = QColor(0xe0, 0x10, 0x10);     // logic 1 (trace, displays)
    QColor low = QColor(0x10, 0x50, 0xc0);      // logic 0
    QString deviceFont = QStringLiteral("Courier New");
    QString textFont;                           // empty: the application font

    static const Theme& current();
    static void setCurrent(const Theme& theme);     // and saves it
    static Theme load();
    void save() const;
    friend bool operator==(const Theme&, const Theme&) = default;
};

inline const Theme& theme() { return Theme::current(); }

} // namespace cm
