#include "core/PcbNetlist.h"

#include "library/LibraryRegistry.h"

#include <QCollator>
#include <QHash>
#include <QSet>

namespace cm::PcbNetlist {

namespace {

struct Part { QString ref, footprint, value; };
struct NetPins { QString name; QVector<std::pair<QString, QString>> pins; };     // (ref, pin)

void collect(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist, QVector<Part>& parts,
             QVector<NetPins>& nets)
{
    auto isPart = [&](const Component& c) {
        const SymbolDef* s = library.find(c.symbolId);
        return s && !s->netLabel && !s->hidden && s->model.value(u"kind").toString() != u"const"
            && s->model.value(u"kind").toString() != u"scope" && !c.props.value(QStringLiteral("excludePcb")).toBool();
    };
    QCollator natural;
    natural.setNumericMode(true);

    QVector<const Component*> comps;
    for (const Component& c : circuit.components())
        if (isPart(c)) comps << &c;
    std::sort(comps.begin(), comps.end(), [&](const Component* a, const Component* b) {
        return natural.compare(a->designator, b->designator) < 0;
    });

    QSet<QString> listed;                       // a package once, whatever its gates
    for (const Component* c : std::as_const(comps)) {
        const SymbolDef* s = library.find(c->symbolId);
        if (s->parts.size() > 1 && listed.contains(c->designator)) continue;
        listed << c->designator;
        QString footprint = c->props.value(QStringLiteral("package")).toString();
        if (footprint.isEmpty()) footprint = s->parts.size() > 1 && !s->package.isEmpty() ? s->package : s->id;
        parts << Part{c->designator, footprint, c->label.isEmpty() ? s->name : c->label};
    }
    for (int n = 0; n < netlist.nets.size(); ++n) {
        NetPins net{netlist.nets[n].name, {}};
        for (const PinRef& r : netlist.nets[n].pins) {
            const Component* c = circuit.component(r.component);
            if (!c || !isPart(*c)) continue;
            const SymbolDef* s = library.find(c->symbolId);
            const QString pin = packagePin(*s, *c, r.pin);
            net.pins << std::pair{c->designator, pin.isEmpty() ? QString::number(r.pin + 1) : pin};
        }
        if (net.pins.size() < 2) continue;
        std::sort(net.pins.begin(), net.pins.end(), [&](const auto& a, const auto& b) {
            return natural.compare(a.first + u'-' + a.second, b.first + u'-' + b.second) < 0;
        });
        nets << net;
    }
}

// pins in lines of at most `perLine`
template <typename F>
QString wrapped(const QVector<std::pair<QString, QString>>& pins, int perLine, const QString& indent, const QString& gap,
                F pinText)
{
    QStringList lines, line;
    for (const auto& [ref, pin] : pins) {
        line << pinText(ref, pin);
        if (line.size() == perLine) { lines << line.join(gap); line.clear(); }
    }
    if (!line.isEmpty()) lines << line.join(gap);
    return lines.join(u'\n' + indent);
}

} // namespace

QString write(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist, Format format)
{
    QVector<Part> parts;
    QVector<NetPins> nets;
    collect(circuit, library, netlist, parts, nets);
    QCollator natural;
    natural.setNumericMode(true);
    QHash<std::pair<QString, QString>, QString> netOfPin;
    for (const NetPins& n : std::as_const(nets))
        for (const auto& p : n.pins) netOfPin.insert(p, n.name);

    QString out;
    switch (format) {
    case Format::Protel:
    case Format::Tango:
        for (const Part& p : std::as_const(parts))
            out += format == Format::Tango ? QStringLiteral("[\n%1\n%2\n%3\n\n\n\n]\n").arg(p.ref, p.footprint, p.value)
                                           : QStringLiteral("[\n%1\n%2\n%3\n]\n").arg(p.ref, p.footprint, p.value);
        for (const NetPins& n : std::as_const(nets)) {
            QStringList pins;
            for (const auto& [ref, pin] : n.pins) pins << ref + u'-' + pin;
            out += QStringLiteral("(\n%1\n%2\n)\n").arg(n.name, pins.join(u'\n'));
        }
        break;
    case Format::Pads:
        out = QStringLiteral("*PADS-PCB*\n*PART*\n");
        for (const Part& p : std::as_const(parts)) out += p.ref + u' ' + p.footprint + u'\n';
        out += QStringLiteral("\n*NET*\n");
        for (const NetPins& n : std::as_const(nets)) {
            QStringList pins;
            for (const auto& [ref, pin] : n.pins) pins << ref + u'.' + pin;
            out += QStringLiteral("*SIGNAL* %1\n%2\n").arg(n.name, pins.join(u' '));
        }
        out += QStringLiteral("*END*\n");
        break;
    case Format::OrcadPcb2: {
        out = QStringLiteral("( { OrCAD PCB II netlist, CirMaker Qt }\n");
        int index = 0;
        for (const Part& p : std::as_const(parts)) {
            // the part's pins that sit on a net, in pin order
            QVector<std::pair<QString, QString>> pins;
            for (auto it = netOfPin.cbegin(); it != netOfPin.cend(); ++it)
                if (it.key().first == p.ref) pins << std::pair{it.key().second, it.value()};
            std::sort(pins.begin(), pins.end(), [&](const auto& a, const auto& b) { return natural.compare(a.first, b.first) < 0; });
            out += QStringLiteral(" ( %1 %2 %3 %4\n").arg(++index, 8, 10, QChar(u'0')).arg(p.footprint, p.ref, p.value);
            for (const auto& [pin, net] : std::as_const(pins)) out += QStringLiteral("  ( %1 %2 )\n").arg(pin, net);
            out += QStringLiteral(" )\n");
        }
        out += QStringLiteral(")\n");
        break;
    }
    case Format::CadNetix:
        out = QStringLiteral("CadNetix Netlist\nCirMaker Qt\n\nPARTS LIST\n\n");
        for (const Part& p : std::as_const(parts))
            out += QStringLiteral("%1 %2 %3\n").arg(p.value, -18).arg(p.footprint, -18).arg(p.ref);
        out += QStringLiteral("EOS\nNET LIST\n\n");
        for (const NetPins& n : std::as_const(nets)) {
            out += QStringLiteral("NODENAME %1$\n    ").arg(n.name, -19);
            out += wrapped(n.pins, 4, QStringLiteral("    "), QStringLiteral("  "), [](const QString& ref, const QString& pin) {
                return QStringLiteral("%1 %2").arg(ref, -11).arg(pin);
            });
            out += u'\n';
        }
        out += QStringLiteral("EOS\n");
        break;
    case Format::Calay:
    case Format::Calay90:
        for (const NetPins& n : std::as_const(nets)) {
            const QString name = (format == Format::Calay ? QStringLiteral("/") : QString()) + n.name;
            const int width = std::max(format == Format::Calay ? 11 : 10, int(name.size()) + 1);
            out += name.leftJustified(width) + wrapped(n.pins, 8, QString(width, u' '), QStringLiteral(" "), [format](const QString& ref, const QString& pin) {
                return format == Format::Calay ? QStringLiteral("%1(%2)").arg(ref, pin) : QStringLiteral("%1('%2)").arg(ref, pin);
            }) + QStringLiteral(";\n");
        }
        break;
    }
    return out;
}

QString calayComponents(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist)
{
    QVector<Part> parts;
    QVector<NetPins> nets;
    collect(circuit, library, netlist, parts, nets);
    QString out;
    for (const Part& p : std::as_const(parts))
        out += QStringLiteral("%1 %2 %3 000     000     0\n").arg(p.value, -19).arg(p.ref, -19).arg(p.footprint, -19);
    return out;
}

} // namespace cm::PcbNetlist
