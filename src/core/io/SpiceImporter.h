#pragma once

#include "core/Circuit.h"

#include <QStringList>

namespace cm {

class LibraryRegistry;

struct SpiceImportReport {
    int components = 0;
    QStringList skipped;            // lines that have no part here (subcircuits, controlled sources...)
};

// Reads a SPICE netlist (R, C, L, D, Q, M, J, V and I elements and the .op,
// .tran, .ac and .dc cards) into a circuit: parts laid out on a grid, each
// pin tied to its node by a terminal named after it (node 0 by a ground).
// Device models become the parts' labels; this library's models simulate them.
namespace SpiceImporter {
bool importText(const QString& text, Circuit& circuit, const LibraryRegistry& library, SpiceImportReport* report = nullptr,
                QString* error = nullptr);
}

} // namespace cm
