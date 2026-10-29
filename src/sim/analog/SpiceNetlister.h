#pragma once

#include "core/Circuit.h"
#include "core/NetlistBuilder.h"

#include <QHash>
#include <QStringList>

#include <optional>

namespace cm {
class LibraryRegistry;
}

namespace cm::spice {

// What a multimeter reads once the analysis has run.
struct Meter {
    bool amps = false;
    QString plus, minus;        // voltmeter nodes
    QString current;            // ammeter: branch-current vector
};

struct Deck {
    QString text;                           // complete netlist, title to .end
    QHash<int, QString> nodeOfNet;          // our net index -> SPICE node ("0" = ground)
    QHash<ItemId, Meter> meters;
    QHash<QString, QString> sourceOfDesignator; // "V1" -> element name, for DC sweeps
    QStringList warnings;
    bool ok = true;                         // false when the deck cannot run (see warnings)
};

// Seconds per digital tick when digital parts (pulser periods, gate delays)
// run inside an analog simulation.
inline constexpr double kTickSeconds = 1e-6;

Deck build(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist);

// SPICE number: 4.7k, 10u, 1Meg, 2.2nF ... (units after the scale are ignored).
std::optional<double> parseNumber(const QString& text);
QString formatNumber(double v);

} // namespace cm::spice
