#include "core/Faults.h"

#include "library/LibraryRegistry.h"

#include <QCryptographicHash>

namespace cm::faults {

namespace {

const QString kOn = QStringLiteral("fault.on");
const QString kHigh = QStringLiteral("fault.high");
const QString kLow = QStringLiteral("fault.low");
const QString kOpen = QStringLiteral("fault.open");
const QString kShort = QStringLiteral("fault.short");
const QString kValue = QStringLiteral("fault.value");
const QString kHint = QStringLiteral("fault.hint");
const QString kReplaced = QStringLiteral("fault.replaced");
const QString kHinted = QStringLiteral("fault.hinted");

QStringList pins(const QVariantMap& p, const QString& key)
{
    return p.value(key).toString().split(u',', Qt::SkipEmptyParts);
}

// Rewiring of the simulation netlist.
class Rewire {
public:
    Rewire(Circuit& c, Netlist& n, const LibraryRegistry& lib) : m_c(c), m_n(n), m_lib(lib) {}

    int newNet(const QString& name)
    {
        m_n.nets.append(Net{name, {}, {}});
        return int(m_n.nets.size()) - 1;
    }

    void movePin(PinRef r, int net)
    {
        const int old = m_n.pinNet.value(r, -1);
        if (old == net) return;
        if (old >= 0) m_n.nets[old].pins.removeAll(r);
        m_n.nets[net].pins << r;
        m_n.pinNet.insert(r, net);
    }

    // Moves everything on `from` onto `to`.
    void mergeNets(int to, int from)
    {
        if (to == from || to < 0 || from < 0) return;
        for (const PinRef& r : std::as_const(m_n.nets[from].pins)) {
            m_n.nets[to].pins << r;
            m_n.pinNet.insert(r, to);
        }
        for (ItemId w : std::as_const(m_n.nets[from].wires)) {
            m_n.nets[to].wires << w;
            m_n.wireNet.insert(w, to);
        }
        m_n.nets[from].pins.clear();
        m_n.nets[from].wires.clear();
    }

    // A hidden part whose pins (in order) join `nets`.
    void addPart(const QString& symbolId, const QString& label, const QVector<int>& nets)
    {
        const SymbolDef* s = m_lib.find(symbolId);
        if (!s || s->pins.size() < nets.size()) return;
        Component c;
        c.symbolId = symbolId;
        c.designator = QStringLiteral("FAULT%1").arg(++m_parts);
        c.label = label;
        c.pos = QPointF(-1e7, -1e7);
        const ItemId id = m_c.addComponent(c);
        for (int i = 0; i < nets.size(); ++i)
            if (nets[i] >= 0) movePin({id, i}, nets[i]);
    }

private:
    Circuit& m_c;
    Netlist& m_n;
    const LibraryRegistry& m_lib;
    int m_parts = 0;
};

} // namespace

bool DeviceFaults::hasData() const
{
    return !high.isEmpty() || !low.isEmpty() || !open.isEmpty() || shorted.size() >= 2 || !value.isEmpty();
}

DeviceFaults read(const Component& c)
{
    DeviceFaults f;
    f.enabled = c.props.value(kOn).toBool();
    f.high = pins(c.props, kHigh);
    f.low = pins(c.props, kLow);
    f.open = pins(c.props, kOpen);
    f.shorted = pins(c.props, kShort);
    f.value = c.props.value(kValue).toString();
    f.hint = c.props.value(kHint).toString();
    f.replaced = c.props.value(kReplaced).toBool();
    f.hinted = c.props.value(kHinted).toBool();
    return f;
}

void write(Component& c, const DeviceFaults& f)
{
    auto set = [&](const QString& key, const QVariant& v, bool keep) {
        if (keep) c.props.insert(key, v);
        else c.props.remove(key);
    };
    set(kOn, true, f.enabled);
    set(kHigh, f.high.join(u','), !f.high.isEmpty());
    set(kLow, f.low.join(u','), !f.low.isEmpty());
    set(kOpen, f.open.join(u','), !f.open.isEmpty());
    set(kShort, f.shorted.join(u','), !f.shorted.isEmpty());
    set(kValue, f.value, !f.value.isEmpty());
    set(kHint, f.hint, !f.hint.isEmpty());
    set(kReplaced, true, f.replaced);
    set(kHinted, true, f.hinted);
}

bool isFaultProperty(const QString& key) { return key.startsWith(u"fault."); }

bool anyActive(const Circuit& circuit)
{
    for (const Component& c : circuit.components())
        if (read(c).active()) return true;
    return false;
}

void apply(Circuit& circuit, Netlist& netlist, const LibraryRegistry& library, bool analog)
{
    const FaultSettings& settings = circuit.faultSettings();
    Rewire rw(circuit, netlist, library);
    const QList<Component> parts = circuit.components().values();
    for (const Component& c : parts) {
        const DeviceFaults f = read(c);
        if (!f.active()) continue;
        const SymbolDef* s = library.find(c.symbolId);
        if (!s) continue;
        if (!f.value.isEmpty()) {
            Component wrong = c;
            wrong.label = f.value;
            circuit.updateComponent(wrong);
        }
        auto pinRef = [&](const QString& name) { return PinRef{c.id, s->pinIndex(name)}; };

        for (const QString& name : f.open) {
            const PinRef r = pinRef(name);
            if (r.pin < 0) continue;
            const int node = netlist.pinNet.value(r, -1);
            const int alone = rw.newNet(QStringLiteral("%1.%2.open").arg(c.designator, name));
            rw.movePin(r, alone);
            if (analog && node >= 0)
                rw.addPart(QStringLiteral("resistor"), settings.open, {alone, node});
        }
        for (const bool high : {true, false}) {
            for (const QString& name : high ? f.high : f.low) {
                const PinRef r = pinRef(name);
                if (r.pin < 0) continue;
                const int node = netlist.pinNet.value(r, -1);
                if (analog) {
                    const int net = node >= 0 ? node : rw.newNet(QStringLiteral("%1.%2").arg(c.designator, name));
                    if (node < 0) rw.movePin(r, net);
                    rw.addPart(QStringLiteral("vcc"), high ? settings.high : settings.low, {net});
                    continue;
                }
                const QString source = high ? QStringLiteral("vcc") : QStringLiteral("ground");
                const int stuck = rw.newNet(QStringLiteral("%1.%2.stuck").arg(c.designator, name));
                rw.movePin(r, stuck);
                rw.addPart(source, {}, {stuck});
                // an output stuck at a level forces its node too
                if (s->pins[r.pin].dir != SymbolPin::Dir::In && node >= 0)
                    rw.addPart(source, {}, {node});
            }
        }
        if (f.shorted.size() >= 2) {
            QVector<int> nets;
            for (const QString& name : f.shorted) {
                const PinRef r = pinRef(name);
                if (r.pin < 0) continue;
                int net = netlist.pinNet.value(r, -1);
                if (net < 0) {
                    net = rw.newNet(QStringLiteral("%1.%2").arg(c.designator, name));
                    rw.movePin(r, net);
                }
                nets << net;
            }
            for (int i = 1; i < nets.size(); ++i) {
                if (analog) rw.addPart(QStringLiteral("resistor"), settings.shorted, {nets[i - 1], nets[i]});
                else rw.mergeNets(nets[0], nets[i]);
            }
        }
    }
}

std::unique_ptr<Circuit> copy(const Circuit& circuit)
{
    auto out = std::make_unique<Circuit>();
    for (const Component& c : circuit.components()) out->addComponent(c);
    for (const Wire& w : circuit.wires()) out->addWire(w);
    out->setAnalysis(circuit.analysis());
    out->setFaultSettings(circuit.faultSettings());
    return out;
}

QString hashPassword(const QString& password)
{
    if (password.isEmpty()) return {};
    return QString::fromLatin1(
        QCryptographicHash::hash(("cirmaker-faults:" + password).toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace cm::faults
