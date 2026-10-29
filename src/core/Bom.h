#pragma once

#include "core/Circuit.h"

#include <QStringList>
#include <QVector>

namespace cm {

class LibraryRegistry;

struct BomLine {
    int quantity = 0;
    QStringList designators;    // natural order: U2 before U10
    QString device;             // library symbol name
    QString value;              // component label
    QString description;
};

// Bill of materials: one line per device and value. Net labels, terminals,
// power symbols and imported placeholders are not parts and are left out.
namespace Bom {
QVector<BomLine> build(const Circuit& circuit, const LibraryRegistry& library);
QString toCsv(const QVector<BomLine>& lines);
} // namespace Bom

} // namespace cm
