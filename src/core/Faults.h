#pragma once

#include "core/Circuit.h"
#include "core/NetlistBuilder.h"

#include <QStringList>

namespace cm {

class LibraryRegistry;

// CircuitMaker fault simulation: an instructor "zaps" device pins and the
// students troubleshoot. A device's faults live in its properties:
//   fault.on                    faults enabled
//   fault.high, fault.low       pins stuck high / low   (pin names, comma separated)
//   fault.open, fault.short     pins disconnected / shorted together
//   fault.value                 Label-Value used while simulating (wrong value)
//   fault.hint                  hint text the student may view
//   fault.replaced, fault.hinted  set when the student replaced the device / viewed the hint
namespace faults {

struct DeviceFaults {
    bool enabled = false;
    QStringList high, low, open, shorted;
    QString value, hint;
    bool replaced = false, hinted = false;

    bool hasData() const;
    // Faults the simulation applies.
    bool active() const { return enabled && !replaced && hasData(); }
    friend bool operator==(const DeviceFaults&, const DeviceFaults&) = default;
};

DeviceFaults read(const Component& c);
void write(Component& c, const DeviceFaults& f);
bool isFaultProperty(const QString& key);
bool anyActive(const Circuit& circuit);

// Applies the active faults to a simulation copy of the circuit: pins move
// to other nets, hidden sources/resistors are added to `circuit` and wired in
// `netlist`. Digital: stuck pins see a constant (an output also forces its
// node), open pins float, shorted pins share one node. Analog: invisible
// sources, 'open' resistors in series and 'short' resistors between pins,
// with the values in the circuit's fault settings.
void apply(Circuit& circuit, Netlist& netlist, const LibraryRegistry& library, bool analog);

// Copy of a circuit for simulation (same ids).
std::unique_ptr<Circuit> copy(const Circuit& circuit);

// Fault lock password, stored as a salted SHA-256 hex digest.
QString hashPassword(const QString& password);

} // namespace faults
} // namespace cm
