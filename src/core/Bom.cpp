#include "core/Bom.h"
#include "core/NaturalOrder.h"

#include "library/LibraryRegistry.h"

#include <QMap>

#include <algorithm>

namespace cm::Bom {

QVector<BomLine> build(const Circuit& circuit, const LibraryRegistry& library)
{
    QMap<std::pair<QString, QString>, BomLine> byKey;
    for (const Component& c : circuit.components()) {
        const SymbolDef* s = library.find(c.symbolId);
        if (!s || s->netLabel || s->hidden || s->model.value(u"kind").toString() == u"const"
            || c.props.value(QStringLiteral("excludeBom")).toBool())
            continue;
        // gates of one package are one part
        const QString device = s->parts.size() > 1 && !s->package.isEmpty() ? s->package : s->name;
        BomLine& line = byKey[{device, c.label}];
        line.device = device;
        line.value = c.label;
        line.description = s->description;
        if (!line.designators.contains(c.designator) || c.designator.isEmpty() || s->parts.size() < 2) {
            line.designators << c.designator;
            ++line.quantity;
        }
    }

    const NaturalOrder natural;
    QVector<BomLine> out;
    for (BomLine& line : byKey) {
        std::sort(line.designators.begin(), line.designators.end(), natural);
        out << line;
    }
    std::sort(out.begin(), out.end(), [&](const BomLine& a, const BomLine& b) {
        return natural.compare(a.designators.value(0), b.designators.value(0)) < 0;
    });
    return out;
}

QString toCsv(const QVector<BomLine>& lines)
{
    auto field = [](QString v) {
        if (v.contains(u',') || v.contains(u'"') || v.contains(u'\n')) {
            v.replace(u'"', QStringLiteral("\"\""));
            v = u'"' + v + u'"';
        }
        return v;
    };
    QString csv = QStringLiteral("Quantity,Designators,Device,Value,Description\n");
    for (const BomLine& l : lines) {
        csv += QString::number(l.quantity) + u',' + field(l.designators.join(u' ')) + u',' + field(l.device) + u','
             + field(l.value) + u',' + field(l.description) + u'\n';
    }
    return csv;
}

} // namespace cm::Bom
