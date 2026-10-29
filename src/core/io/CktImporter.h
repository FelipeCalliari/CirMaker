#pragma once

#include "core/Circuit.h"

#include <QMap>
#include <QStringList>

namespace cm {

class LibraryRegistry;

struct CktImportReport {
    double version = 0;
    int components = 0;
    int wires = 0;
    int texts = 0;
    int shorts = 0;                 // nets of the file our redrawn symbols join
    QMap<QString, int> unmapped;    // CircuitMaker device name -> occurrences
    QStringList warnings;
};

// Reads CircuitMaker 2000 "CircuitMaker Text" schematics (versions 3.x-5.61).
// Devices are matched to library symbols through their CircuitMaker aliases;
// unknown devices become placeholder components listed in the report.
namespace CktImporter {
bool import(const QString& path, Circuit& circuit, const LibraryRegistry& library,
            CktImportReport* report, QString* error);
bool importData(const QByteArray& data, Circuit& circuit, const LibraryRegistry& library,
                CktImportReport* report, QString* error);
} // namespace CktImporter

} // namespace cm
