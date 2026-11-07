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
    double ohmsCurrent = 0;     // an ohmmeter: the current it forces from + round to - (0: not one)
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
    double generatorFrequency = 0;          // first signal generator's (Fourier default)
    // ngspice vector of the current through a component (into its first pin),
    // for current, power and impedance probes: "@r1[i]", "v1#branch"
    QHash<ItemId, QString> currentOf;
    QStringList elements, models, subckts;  // the parts of the text, for subcircuits
};

// One simulation of a sweep (temperature, parameter, Monte Carlo).
struct Run {
    QString label;                          // "50 °C", "R1 = 2000", "run 3"; empty without sweep
    Deck deck;
};

// Harmonics 0..n of a periodic signal: peak magnitudes, phases in degrees
// (relative to a sine), total harmonic distortion in percent.
struct Fourier {
    QVector<double> frequency, magnitude, phase;
    double thd = 0;
};

// Seconds per digital tick when digital parts (pulser periods, gate delays)
// run inside an analog simulation.
inline constexpr double kTickSeconds = 1e-6;

// `extra` goes before the analysis line (e.g. ".temp 50").
Deck build(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist, const QString& extra = {});
// The decks of the circuit's analysis, one per sweep point.
QVector<Run> buildRuns(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist, QString* error);
// The circuit as ".subckt name <ports>": its Input/Output port symbols are
// the external nodes, inputs first, then outputs, in reading order.
QString subcircuit(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist, const QString& name,
                   QString* error);
// Fourier analysis of the last period of a transient waveform.
Fourier fourier(const QVector<double>& time, const QVector<double>& y, double f0, int harmonics);

// The d_cosim library that runs our chip models inside ngspice, at a path
// ngspice can read (it lower-cases the deck); empty when not available.
QString cosimLibrary();

// SPICE number: 4.7k, 10u, 1Meg, 2.2nF ... (units after the scale are ignored).
std::optional<double> parseNumber(const QString& text);
QString formatNumber(double v);

} // namespace cm::spice
