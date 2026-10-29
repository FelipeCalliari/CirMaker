#include "sim/analog/SpiceNetlister.h"

#include "library/LibraryRegistry.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

using namespace Qt::StringLiterals;

namespace cm::spice {

std::optional<double> parseNumber(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?)\\s*([a-zA-Z]*)"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    double v = m.captured(1).toDouble();
    const QString suffix = m.captured(2).toLower();
    if (suffix.startsWith(u"meg")) v *= 1e6;
    else if (suffix.startsWith(u"mil")) v *= 25.4e-6;
    else if (!suffix.isEmpty()) {
        switch (suffix[0].unicode()) {
        case 'f': v *= 1e-15; break;
        case 'p': v *= 1e-12; break;
        case 'n': v *= 1e-9; break;
        case 'u': v *= 1e-6; break;
        case 'm': v *= 1e-3; break;
        case 'k': v *= 1e3; break;
        case 'g': v *= 1e9; break;
        case 't': v *= 1e12; break;
        default: break;                     // a unit such as V, A, F, Hz
        }
    }
    return v;
}

QString formatNumber(double v) { return QString::number(v, 'g', 9); }

namespace {

QString sanitize(QString s)
{
    s = s.toLower();
    for (QChar& c : s)
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != u'_')
            c = u'_';
    return s;
}

class Builder {
public:
    Builder(const Circuit& c, const LibraryRegistry& lib, const Netlist& nl) : m_c(c), m_lib(lib), m_nl(nl) {}

    Deck run()
    {
        assignNodes();
        for (const Component& comp : m_c.components()) {
            const SymbolDef* s = m_lib.find(comp.symbolId);
            if (!s || s->netLabel || s->hidden)
                continue;
            emitComponent(comp, *s);
        }
        // every node keeps a DC path to ground, so open inputs never make
        // the matrix singular
        for (const QString& node : std::as_const(m_usedNodes))
            if (node != u"0")
                m_lines << QStringLiteral("Rleak_%1 %1 0 1T").arg(node);

        if (m_lines.isEmpty()) {
            m_deck.ok = false;
            m_deck.warnings << QStringLiteral("the circuit has no analog parts");
        }
        QStringList text{QStringLiteral("* CirMaker Qt")};
        text << m_lines << m_models << m_subckts;
        m_deck.text = text.join(u'\n') + u'\n';
        return m_deck;
    }

private:
    QVariantMap props(const Component& c, const SymbolDef& s) const
    {
        QVariantMap p = s.defaultProps;
        for (auto it = c.props.cbegin(); it != c.props.cend(); ++it)
            p.insert(it.key(), it.value());
        return p;
    }

    bool isGroundNet(int net) const
    {
        for (const PinRef& r : m_nl.nets[net].pins) {
            const Component* c = m_c.component(r.component);
            if (c && c->symbolId == u"ground")
                return true;
        }
        return false;
    }

    bool connected(int net) const
    {
        return net >= 0 && m_nl.nets[net].pins.size() + m_nl.nets[net].wires.size() > 1;
    }

    void assignNodes()
    {
        QSet<QString> taken{QStringLiteral("0")};
        for (int i = 0; i < m_nl.nets.size(); ++i) {
            QString node;
            if (isGroundNet(i)) {
                node = QStringLiteral("0");
            } else {
                const QString& name = m_nl.nets[i].name;
                const bool autoName = name == QStringLiteral("N%1").arg(i + 1);
                node = autoName ? QStringLiteral("n%1").arg(i + 1) : sanitize(name);
                if (node.isEmpty() || node[0].isDigit()) node.prepend(u"n_");
                while (taken.contains(node)) node += u'_';
                taken.insert(node);
            }
            m_deck.nodeOfNet.insert(i, node);
        }
    }

    QString node(const Component& c, int pin)
    {
        const QString n = m_deck.nodeOfNet.value(m_nl.netOf(c.id, pin), QStringLiteral("0"));
        m_usedNodes.insert(n);
        return n;
    }
    QString node(const Component& c, const SymbolDef& s, const QString& pinName)
    {
        return node(c, s.pinIndex(pinName));
    }

    QString uniqueName(const Component& c)
    {
        QString base = sanitize(c.designator);
        if (base.isEmpty()) base = QStringLiteral("c%1").arg(c.id);
        QString name = base;
        while (m_names.contains(name)) name += QStringLiteral("_%1").arg(c.id);
        m_names.insert(name);
        return name;
    }

    QString value(const Component& c, const SymbolDef& s) const
    {
        QString v = (c.label.trimmed().isEmpty() ? s.defaultLabel : c.label).trimmed();
        v = v.section(u' ', 0, 0);
        if (v.startsWith(u'+')) v.remove(0, 1);
        return v;
    }

    void addModel(const QString& text)
    {
        if (!m_models.contains(text)) m_models << text;
    }
    void addSubckt(const QJsonArray& lines)
    {
        const QString first = lines.isEmpty() ? QString() : lines.first().toString();
        if (first.isEmpty() || m_subcktHeads.contains(first)) return;
        m_subcktHeads.insert(first);
        for (const QJsonValue& l : lines) m_subckts << l.toString();
    }

    void emitComponent(const Component& c, const SymbolDef& s)
    {
        const QString kind = s.model.value(u"kind").toString();
        const QString name = uniqueName(c);
        if (!s.spice.isEmpty()) {
            emitTemplate(c, s, name);
            if (s.spice.value(u"line").toString().startsWith(u'V'))
                m_deck.sourceOfDesignator.insert(c.designator, u'V' + name);
            else if (s.spice.value(u"line").toString().startsWith(u'I'))
                m_deck.sourceOfDesignator.insert(c.designator, u'I' + name);
            return;
        }
        if (kind == u"siggen") return emitGenerator(c, s, name);
        if (kind == u"pot") return emitPot(c, s, name);
        if (kind == u"meter") return emitMeter(c, s, name);
        if (kind == u"link") return emitSwitch(c, s, name);
        if (kind == u"const") return;                      // ground
        if (kind.isEmpty()) return;
        emitDigital(c, s, name, kind);
    }

    void emitTemplate(const Component& c, const SymbolDef& s, const QString& name)
    {
        static const QRegularExpression placeholder(QStringLiteral("\\{([^}]+)\\}"));
        const QString tpl = s.spice.value(u"line").toString();
        QString out;
        int last = 0;
        for (auto it = placeholder.globalMatch(tpl); it.hasNext();) {
            const auto m = it.next();
            out += tpl.mid(last, m.capturedStart() - last);
            const QString key = m.captured(1);
            if (key == u"name") out += name;
            else if (key == u"value") out += value(c, s);
            else if (key.startsWith(u"prop:")) out += props(c, s).value(key.mid(5)).toString();
            else if (s.pinIndex(key) >= 0) out += node(c, s, key);
            else out += m.captured(0);
            last = int(m.capturedEnd());
        }
        out += tpl.mid(last);
        m_lines << out;
        for (const QJsonValue& v : s.spice.value(u"models").toArray()) addModel(v.toString());
        addSubckt(s.spice.value(u"subckt").toArray());
    }

    double number(const QVariantMap& p, const char* key, double fallback)
    {
        return parseNumber(p.value(QLatin1String(key)).toString()).value_or(fallback);
    }

    void emitGenerator(const Component& c, const SymbolDef& s, const QString& name)
    {
        const QVariantMap p = props(c, s);
        const double amp = number(p, "amplitude", 1), freq = std::max(number(p, "frequency", 1e3), 1e-6);
        const double off = number(p, "offset", 0), period = 1 / freq;
        const QString wave = p.value(QStringLiteral("wave")).toString().toLower();
        QString shape;
        if (wave.startsWith(u"sq")) {
            const double tr = period / 1000;
            shape = QStringLiteral("PULSE(%1 %2 0 %3 %3 %4 %5)")
                        .arg(formatNumber(off - amp), formatNumber(off + amp), formatNumber(tr),
                             formatNumber(period / 2 - tr), formatNumber(period));
        } else if (wave.startsWith(u"tri")) {
            shape = QStringLiteral("PULSE(%1 %2 0 %3 %3 %4 %5)")
                        .arg(formatNumber(off - amp), formatNumber(off + amp), formatNumber(period / 2),
                             formatNumber(period * 1e-6), formatNumber(period));
        } else {
            shape = QStringLiteral("SIN(%1 %2 %3)").arg(formatNumber(off), formatNumber(amp), formatNumber(freq));
        }
        m_lines << QStringLiteral("V%1 %2 %3 DC %4 %5 AC 1")
                       .arg(name, node(c, s, QStringLiteral("+")), node(c, s, QStringLiteral("-")), formatNumber(off), shape);
        m_deck.sourceOfDesignator.insert(c.designator, u'V' + name);
    }

    void emitPot(const Component& c, const SymbolDef& s, const QString& name)
    {
        const QVariantMap p = props(c, s);
        const double total = parseNumber(value(c, s)).value_or(10e3);
        const double pos = std::clamp(p.value(QStringLiteral("position"), 50).toDouble(), 0.0, 100.0) / 100;
        const QString w = node(c, s, QStringLiteral("W"));
        m_lines << QStringLiteral("R%1a %2 %3 %4").arg(name, node(c, s, QStringLiteral("1")), w, formatNumber(std::max(1e-3, total * pos)));
        m_lines << QStringLiteral("R%1b %2 %3 %4").arg(name, w, node(c, s, QStringLiteral("2")), formatNumber(std::max(1e-3, total * (1 - pos))));
    }

    void emitMeter(const Component& c, const SymbolDef& s, const QString& name)
    {
        Meter m;
        m.amps = props(c, s).value(QStringLiteral("mode")).toString().toUpper() == u"A";
        m.plus = node(c, s, QStringLiteral("+"));
        m.minus = node(c, s, QStringLiteral("-"));
        if (m.amps) {
            m_lines << QStringLiteral("Vam_%1 %2 %3 DC 0").arg(name, m.plus, m.minus);
            m.current = QStringLiteral("vam_%1#branch").arg(name);
        }
        m_deck.meters.insert(c.id, m);
    }

    // Switches keep the position they have when the analysis starts.
    void emitSwitch(const Component& c, const SymbolDef& s, const QString& name)
    {
        const QString type = s.model.value(u"type").toString();
        const QVariantMap p = props(c, s);
        auto r = [](bool closed) { return closed ? QStringLiteral("1m") : QStringLiteral("1T"); };
        if (type == u"spdt") {
            const QString com = node(c, s, QStringLiteral("C"));
            m_lines << QStringLiteral("R%1nc %2 %3 %4").arg(name, com, node(c, s, QStringLiteral("NC")), r(true));
            m_lines << QStringLiteral("R%1no %2 %3 %4").arg(name, com, node(c, s, QStringLiteral("NO")), r(false));
            return;
        }
        bool closed = false;
        if (type == u"nc") closed = true;
        else if (type == u"toggle") closed = p.value(QStringLiteral("closed")).toInt() != 0;
        m_lines << QStringLiteral("R%1 %2 %3 %4").arg(name, node(c, 0), node(c, 1), r(closed));
    }

    // ---- digital parts as XSPICE code models behind A/D bridges

    void digitalModels()
    {
        if (m_digitalModels) return;
        m_digitalModels = true;
        const QString d = QStringLiteral("rise_delay=%1 fall_delay=%1").arg(formatNumber(kTickSeconds / 100));
        addModel(QStringLiteral(".model cm_adc adc_bridge(in_low=0.8 in_high=2.0)"));
        addModel(QStringLiteral(".model cm_dac dac_bridge(out_low=0 out_high=5 out_undef=2.5 t_rise=1n t_fall=1n)"));
        for (const char* g : {"and", "nand", "or", "nor", "xor", "xnor"})
            addModel(QStringLiteral(".model cm_%1 d_%1(%2)").arg(QLatin1String(g), d));
        addModel(QStringLiteral(".model cm_inv d_inverter(%1)").arg(d));
        addModel(QStringLiteral(".model cm_buf d_buffer(%1)").arg(d));
        addModel(QStringLiteral(".model cm_tri d_tristate(delay=%1)").arg(formatNumber(kTickSeconds / 100)));
        addModel(QStringLiteral(".model cm_dff d_dff(clk_delay=%1 set_delay=%1 reset_delay=%1 %2)")
                     .arg(formatNumber(kTickSeconds / 100), d));
        addModel(QStringLiteral(".model cm_jkff d_jkff(clk_delay=%1 set_delay=%1 reset_delay=%1 %2)")
                     .arg(formatNumber(kTickSeconds / 100), d));
        addModel(QStringLiteral(".model cm_pullup d_pullup(load=1p)"));
        addModel(QStringLiteral(".model cm_pulldown d_pulldown(load=1p)"));
    }

    // Digital node for an input pin: bridged from its analog net, or tied to
    // the pin's floating level when nothing drives it.
    QString digitalIn(const Component& c, const SymbolDef& s, int pin, QStringList& adcIn, QStringList& adcOut)
    {
        if (pin < 0) return QStringLiteral("NULL");
        const QString d = QStringLiteral("d%1_%2").arg(c.id).arg(pin);
        if (connected(m_nl.netOf(c.id, pin))) {
            adcIn << node(c, pin);
            adcOut << d;
        } else {
            m_lines << QStringLiteral("Atie%1_%2 %3 %4").arg(c.id).arg(pin).arg(d,
                          s.pins[pin].floatValue ? QStringLiteral("cm_pullup") : QStringLiteral("cm_pulldown"));
        }
        return d;
    }
    QString digitalOut(const Component& c, int pin, QStringList& dacIn, QStringList& dacOut)
    {
        if (pin < 0) return QStringLiteral("NULL");
        const QString d = QStringLiteral("d%1_%2").arg(c.id).arg(pin);
        dacIn << d;
        dacOut << node(c, pin);
        return d;
    }
    void bridges(const Component& c, const QStringList& adcIn, const QStringList& adcOut,
                 const QStringList& dacIn, const QStringList& dacOut)
    {
        if (!adcIn.isEmpty())
            m_lines << QStringLiteral("Aadc%1 [%2] [%3] cm_adc").arg(c.id).arg(adcIn.join(u' '), adcOut.join(u' '));
        if (!dacIn.isEmpty())
            m_lines << QStringLiteral("Adac%1 [%2] [%3] cm_dac").arg(c.id).arg(dacIn.join(u' '), dacOut.join(u' '));
    }

    // Finds a pin by name or active-low name; "~" marks inversion for XSPICE.
    QString port(const Component& c, const SymbolDef& s, std::initializer_list<const char*> names,
                 QStringList& adcIn, QStringList& adcOut)
    {
        for (const char* n : names) {
            const QString name = QString::fromLatin1(n);
            if (const int i = s.pinIndex(name); i >= 0) return digitalIn(c, s, i, adcIn, adcOut);
            if (const int i = s.pinIndex(u'~' + name); i >= 0) return u'~' + digitalIn(c, s, i, adcIn, adcOut);
        }
        return QStringLiteral("NULL");
    }

    void emitDigital(const Component& c, const SymbolDef& s, const QString& name, const QString& kind)
    {
        const QVariantMap p = props(c, s);
        if (kind == u"logicswitch") {
            m_lines << QStringLiteral("V%1 %2 0 DC %3").arg(name, node(c, 0), p.value(QStringLiteral("state")).toInt() ? u"5"_s : u"0"_s);
            return;
        }
        if (kind == u"pulser") {
            const double high = std::max(1, p.value(QStringLiteral("high"), 10).toInt()) * kTickSeconds;
            const double low = std::max(1, p.value(QStringLiteral("low"), 10).toInt()) * kTickSeconds;
            const QString shape = QStringLiteral("PULSE(%1 %2 %3 1n 1n %4 %5)");
            if (const int q = s.pinIndex(QStringLiteral("Q")); q >= 0)
                m_lines << QStringLiteral("V%1 %2 0 ").arg(name, node(c, q))
                               + shape.arg(u"0"_s, u"5"_s, formatNumber(low), formatNumber(high), formatNumber(high + low));
            if (const int qn = s.pinIndex(QStringLiteral("~Q")); qn >= 0 && connected(m_nl.netOf(c.id, qn)))
                m_lines << QStringLiteral("V%1n %2 0 ").arg(name, node(c, qn))
                               + shape.arg(u"5"_s, u"0"_s, formatNumber(low), formatNumber(high), formatNumber(high + low));
            return;
        }
        if (kind == u"key") {
            const int v = p.value(QStringLiteral("value")).toInt();
            for (int i = 0; i < s.pins.size(); ++i) {
                const QString pn = s.pins[i].name;
                if (!pn.startsWith(u'D')) continue;
                const int bit = pn.mid(1).toInt();
                m_lines << QStringLiteral("V%1_%2 %3 0 DC %4").arg(name).arg(i).arg(node(c, i), (v >> bit & 1) ? u"5"_s : u"0"_s);
            }
            return;
        }
        if (kind == u"display" || kind == u"led" || kind == u"scope")
            return;                                 // observers: read back from node voltages

        digitalModels();
        QStringList adcIn, adcOut, dacIn, dacOut;
        if (kind == u"gate") {
            const QString op = s.model.value(u"op").toString();
            const bool invert = s.model.value(u"invert").toBool();
            QStringList inputs;
            for (int i = 0; i < s.pins.size(); ++i)
                if (s.pins[i].dir == SymbolPin::Dir::In)
                    inputs << digitalIn(c, s, i, adcIn, adcOut);
            const QString out = digitalOut(c, s.pinIndex(QStringLiteral("Y")), dacIn, dacOut);
            if (op == u"buf") {
                m_lines << QStringLiteral("A%1 %2 %3 %4").arg(name, inputs.value(0), out, invert ? u"cm_inv"_s : u"cm_buf"_s);
            } else {
                QString model = op;
                if (invert) model = op == u"xor" ? u"xnor"_s : u"n"_s + op;
                m_lines << QStringLiteral("A%1 [%2] %3 cm_%4").arg(name, inputs.join(u' '), out, model);
            }
        } else if (kind == u"tribuf") {
            const QString in = digitalIn(c, s, s.pinIndex(QStringLiteral("A")), adcIn, adcOut);
            const QString en = port(c, s, {"EN", "OE", "G"}, adcIn, adcOut);
            const QString out = digitalOut(c, s.pinIndex(QStringLiteral("Y")), dacIn, dacOut);
            m_lines << QStringLiteral("A%1 %2 %3 %4 cm_tri").arg(name, in, en, out);
            m_deck.warnings << QStringLiteral("%1: tri-state outputs drive 2.5 V when disabled in analog mode").arg(c.designator);
        } else if (kind == u"dff" || kind == u"jkff") {
            const bool jk = kind == u"jkff";
            QString data = jk ? port(c, s, {"J"}, adcIn, adcOut) + u' ' + port(c, s, {"K"}, adcIn, adcOut)
                              : port(c, s, {"D"}, adcIn, adcOut);
            QString clk = port(c, s, {"CLK", "C"}, adcIn, adcOut);
            if (s.model.value(u"edge").toString() == u"fall" && !clk.startsWith(u'~')) clk.prepend(u'~');
            const QString set = port(c, s, {"S", "SET", "PRE", "SD"}, adcIn, adcOut);
            const QString reset = port(c, s, {"R", "RST", "CLR", "RD"}, adcIn, adcOut);
            const QString q = digitalOut(c, s.pinIndex(QStringLiteral("Q")), dacIn, dacOut);
            const QString qn = digitalOut(c, s.pinIndex(QStringLiteral("~Q")), dacIn, dacOut);
            m_lines << QStringLiteral("A%1 %2 %3 %4 %5 %6 %7 %8").arg(name, data, clk, set, reset, q, qn, jk ? u"cm_jkff"_s : u"cm_dff"_s);
        } else {
            m_deck.warnings << QStringLiteral("%1 (%2) has no analog model and is left out").arg(c.designator, s.name);
            return;
        }
        bridges(c, adcIn, adcOut, dacIn, dacOut);
    }

    const Circuit& m_c;
    const LibraryRegistry& m_lib;
    const Netlist& m_nl;
    Deck m_deck;
    QStringList m_lines, m_models, m_subckts;
    QSet<QString> m_subcktHeads, m_names, m_usedNodes;
    bool m_digitalModels = false;
};

QString analysisLine(const AnalysisSettings& a, const Deck& deck, QString* error)
{
    switch (a.type) {
    case AnalysisSettings::Type::OperatingPoint:
        return QStringLiteral(".op");
    case AnalysisSettings::Type::Transient:
        return QStringLiteral(".tran %1 %2").arg(a.tranStep, a.tranStop);
    case AnalysisSettings::Type::AC:
        return QStringLiteral(".ac dec %1 %2 %3").arg(a.acPointsPerDecade).arg(a.acStart, a.acStop);
    case AnalysisSettings::Type::DCSweep: {
        const QString src = deck.sourceOfDesignator.value(a.dcSource);
        if (src.isEmpty()) {
            *error = QStringLiteral("DC sweep source '%1' is not a voltage or current source").arg(a.dcSource);
            return {};
        }
        return QStringLiteral(".dc %1 %2 %3 %4").arg(src, a.dcStart, a.dcStop, a.dcStep);
    }
    }
    return {};
}

} // namespace

Deck build(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist)
{
    Deck deck = Builder(circuit, library, netlist).run();
    QString error;
    const QString line = analysisLine(circuit.analysis(), deck, &error);
    if (!error.isEmpty()) {
        deck.ok = false;
        deck.warnings << error;
    }
    deck.text += line + QStringLiteral("\n.end\n");
    return deck;
}

} // namespace cm::spice
