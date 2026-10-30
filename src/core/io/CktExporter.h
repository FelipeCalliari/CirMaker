#pragma once

#include "core/Circuit.h"

#include <QByteArray>
#include <QStringList>

namespace cm {

class CmDeviceLib;
struct CmLibDevice;
class LibraryRegistry;
struct SymbolDef;

struct CktExportReport {
    int devices = 0;
    int wires = 0;
    QStringList skipped;        // parts CircuitMaker's library has no device for, bus wires
};

// Writes a circuit as a "CircuitMaker Text" 5.6 schematic. CircuitMaker draws
// each device from its own library by name, so the records are built from
// the user's Device.lib: device name, pins, package map and default strings.
// Positions come from where our pins are, so wires end on CircuitMaker's pins.
namespace CktExporter {
// The Device.lib device a part becomes: an alias naming a device, else a
// device whose default value is one of the aliases ("7400" -> "2-In NAND~").
const CmLibDevice* deviceFor(const SymbolDef& s, const CmDeviceLib& lib);
QByteArray write(const Circuit& circuit, const LibraryRegistry& library, const CmDeviceLib& lib,
                 CktExportReport* report = nullptr);
bool save(const QString& path, const Circuit& circuit, const LibraryRegistry& library, const CmDeviceLib& lib,
          CktExportReport* report = nullptr, QString* error = nullptr);
}

} // namespace cm
