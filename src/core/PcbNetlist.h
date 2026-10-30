#pragma once

#include "core/Circuit.h"
#include "core/NetlistBuilder.h"

namespace cm {

class LibraryRegistry;

// PCB netlists in the formats CircuitMaker exported. Net labels, terminals
// and power symbols name nets but are not parts.
namespace PcbNetlist {
enum class Format {
    Protel,     // "[ part ]" and "( net )" blocks, for TraxMaker and most PCB tools
    Tango,      // the same blocks with three blank lines in each part
    Pads,       // *PADS-PCB* with *PART* and *NET* sections
    OrcadPcb2,  // OrCAD PCB II: one block per part listing each pin's net
    CadNetix,   // PARTS LIST and NET LIST sections, NODENAME per net
    Calay,      // "/name ref(pin) ...;" per net; parts in a separate list
    Calay90,    // the same without the slash, pins as ref('pin)
};
QString write(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist,
              Format format = Format::Protel);
// Calay's component list (the .cmp file next to the netlist): value,
// designator and footprint in 20-character columns.
QString calayComponents(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist);
}

} // namespace cm
