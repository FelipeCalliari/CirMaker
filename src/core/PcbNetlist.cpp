#include "core/PcbNetlist.h"

#include "library/LibraryRegistry.h"

#include <QCollator>

namespace cm::PcbNetlist {

QString write(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist)
{
    auto isPart = [&](const Component& c) {
        const SymbolDef* s = library.find(c.symbolId);
        return s && !s->netLabel && !s->hidden && s->model.value(u"kind").toString() != u"const"
            && s->model.value(u"kind").toString() != u"scope";
    };
    QCollator natural;
    natural.setNumericMode(true);

    QVector<const Component*> parts;
    for (const Component& c : circuit.components())
        if (isPart(c)) parts << &c;
    std::sort(parts.begin(), parts.end(), [&](const Component* a, const Component* b) {
        return natural.compare(a->designator, b->designator) < 0;
    });

    QString out;
    for (const Component* c : std::as_const(parts)) {
        const SymbolDef* s = library.find(c->symbolId);
        out += QStringLiteral("[\n%1\n%2\n%3\n]\n").arg(c->designator, s->id, c->label.isEmpty() ? s->name : c->label);
    }
    for (int n = 0; n < netlist.nets.size(); ++n) {
        QStringList pins;
        for (const PinRef& r : netlist.nets[n].pins) {
            const Component* c = circuit.component(r.component);
            if (!c || !isPart(*c)) continue;
            const SymbolDef* s = library.find(c->symbolId);
            const QString number = s->pins[r.pin].number.isEmpty() ? QString::number(r.pin + 1) : s->pins[r.pin].number;
            pins << c->designator + u'-' + number;
        }
        if (pins.size() < 2) continue;
        std::sort(pins.begin(), pins.end(), [&](const QString& a, const QString& b) { return natural.compare(a, b) < 0; });
        out += QStringLiteral("(\n%1\n%2\n)\n").arg(netlist.nets[n].name, pins.join(u'\n'));
    }
    return out;
}

} // namespace cm::PcbNetlist
