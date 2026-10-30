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
};
QString write(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist,
              Format format = Format::Protel);
}

} // namespace cm
