#pragma once

#include "core/Circuit.h"

#include <QHash>
#include <QPointF>
#include <QVector>

namespace cm {

class LibraryRegistry;

struct PinRef {
    ItemId component = 0;
    int pin = -1;
    friend bool operator==(const PinRef&, const PinRef&) = default;
};

inline size_t qHash(const PinRef& r, size_t seed = 0) { return qHashMulti(seed, r.component, r.pin); }

struct Net {
    QString name;               // net-label text, or "N<index>"
    QVector<PinRef> pins;
    QVector<ItemId> wires;
};

struct Netlist {
    QVector<Net> nets;
    QHash<PinRef, int> pinNet;  // every pin of every known symbol, including unconnected ones
    QHash<ItemId, int> wireNet;
    QVector<QPointF> junctions; // points joining three or more conductors

    int netOf(ItemId component, int pin) const { return pinNet.value({component, pin}, -1); }
};

// Connectivity rules (as in CircuitMaker): a pin or wire end touching a wire
// (vertex or segment interior) connects to it; wires that merely cross do not.
// Pins at the same point connect. Net-label symbols with equal labels share a net.
class NetlistBuilder {
public:
    static Netlist build(const Circuit& circuit, const LibraryRegistry& library);
    // A net's name as CircuitMaker shows it on a probe: its label, else
    // "<designator>_<package pin>" of the earliest placed part on it (U1_6)
    static QString nodeName(const Netlist& netlist, int net, const Circuit& circuit, const LibraryRegistry& library);
};

} // namespace cm
