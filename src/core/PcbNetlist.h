#pragma once

#include "core/Circuit.h"
#include "core/NetlistBuilder.h"

namespace cm {

class LibraryRegistry;

// Protel/Tango netlist ("[ part ]" and "( net )" blocks), the format
// CircuitMaker exported for TraxMaker and most PCB tools. Net labels,
// terminals and power symbols name nets but are not parts.
namespace PcbNetlist {
QString write(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist);
}

} // namespace cm
