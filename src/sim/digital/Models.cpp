#include "sim/digital/Models.h"

#include "core/Circuit.h"
#include "library/SymbolDef.h"

#include <QJsonObject>

namespace cm::sim {

namespace {

QVariantMap mergedProps(const SymbolDef& s, const Component& c)
{
    QVariantMap p = s.defaultProps;
    for (auto it = c.props.cbegin(); it != c.props.cend(); ++it)
        p.insert(it.key(), it.value());
    return p;
}

// Finds the first pin whose name is one of `names`; reports whether it is the
// active-low ("~") variant.
int findPin(const SymbolDef& s, std::initializer_list<const char*> names, bool* activeLow = nullptr)
{
    for (const char* n : names) {
        const QString name = QString::fromLatin1(n);
        int idx = s.pinIndex(name);
        if (idx >= 0) {
            if (activeLow) *activeLow = false;
            return idx;
        }
        idx = s.pinIndex(u'~' + name);
        if (idx >= 0) {
            if (activeLow) *activeLow = true;
            return idx;
        }
    }
    return -1;
}

QVector<int> bitPins(const SymbolDef& s, int count)
{
    QVector<int> out;
    for (int i = 0; i < count; ++i)
        out << s.pinIndex(QStringLiteral("D%1").arg(i));
    return out;
}

class GateModel : public Model {
public:
    enum class Op { And, Or, Xor, Buf };
    GateModel(Op op, bool invert, QVector<int> inputs, int output)
        : m_op(op), m_invert(invert), m_inputs(std::move(inputs)), m_out(output) {}

    void evaluate(ModelContext& ctx) override
    {
        Logic r = Logic::X;
        switch (m_op) {
        case Op::And: {
            bool unknown = false;
            r = Logic::L1;
            for (int p : m_inputs) {
                const Logic v = ctx.input(p);
                if (v == Logic::L0) { r = Logic::L0; unknown = false; break; }
                if (!isKnown(v)) unknown = true;
            }
            if (unknown) r = Logic::X;
            break;
        }
        case Op::Or: {
            bool unknown = false;
            r = Logic::L0;
            for (int p : m_inputs) {
                const Logic v = ctx.input(p);
                if (v == Logic::L1) { r = Logic::L1; unknown = false; break; }
                if (!isKnown(v)) unknown = true;
            }
            if (unknown) r = Logic::X;
            break;
        }
        case Op::Xor: {
            bool parity = false;
            r = Logic::L0;
            for (int p : m_inputs) {
                const Logic v = ctx.input(p);
                if (!isKnown(v)) { r = Logic::X; break; }
                parity ^= v == Logic::L1;
            }
            if (r != Logic::X) r = fromBool(parity);
            break;
        }
        case Op::Buf:
            r = m_inputs.isEmpty() ? Logic::X : ctx.input(m_inputs.first());
            if (r == Logic::Z) r = Logic::X;
            break;
        }
        ctx.drive(m_out, strong(m_invert ? logicNot(r) : r));
        m_state = r == Logic::X ? -1 : int((r == Logic::L1) != m_invert);
    }
    int state() const override { return m_state; }

private:
    Op m_op;
    bool m_invert;
    QVector<int> m_inputs;
    int m_out;
    int m_state = -1;
};

class TriBufModel : public Model {
public:
    TriBufModel(int in, int en, bool enLow, int out, bool invert)
        : m_in(in), m_en(en), m_enLow(enLow), m_out(out), m_invert(invert) {}

    void evaluate(ModelContext& ctx) override
    {
        const Logic en = ctx.input(m_en);
        const Logic active = m_enLow ? logicNot(en) : en;
        if (active == Logic::L0) {
            ctx.drive(m_out, released());
            return;
        }
        Logic v = ctx.input(m_in);
        if (v == Logic::Z) v = Logic::X;
        if (m_invert) v = logicNot(v);
        ctx.drive(m_out, strong(active == Logic::L1 ? v : Logic::X));
    }

private:
    int m_in, m_en;
    bool m_enLow;
    int m_out;
    bool m_invert;
};

// D or JK flip-flop with optional asynchronous set/reset. Starts with Q = 0.
class FlipFlopModel : public Model {
public:
    struct Pins {
        int d = -1, j = -1, k = -1, clk = -1, q = -1, qn = -1, set = -1, reset = -1;
        bool fallingEdge = false, setLow = false, resetLow = false;
    };
    FlipFlopModel(bool jk, Pins p) : m_jk(jk), m_p(p) {}

    void init(ModelContext& ctx) override
    {
        m_q = Logic::L0;
        m_lastClk = Logic::X;
        output(ctx);
    }

    void evaluate(ModelContext& ctx) override
    {
        const Logic clk = ctx.input(m_p.clk);
        const bool edge = !ctx.settling()
            && (m_p.fallingEdge ? (m_lastClk == Logic::L1 && clk == Logic::L0)
                                : (m_lastClk == Logic::L0 && clk == Logic::L1));
        m_lastClk = clk;

        const bool set = active(ctx, m_p.set, m_p.setLow);
        const bool reset = active(ctx, m_p.reset, m_p.resetLow);
        if (set || reset) {
            m_q = set && reset ? Logic::X : fromBool(set);
        } else if (edge) {
            if (m_jk) {
                const Logic j = ctx.input(m_p.j), k = ctx.input(m_p.k);
                if (!isKnown(j) || !isKnown(k))
                    m_q = Logic::X;
                else if (j == Logic::L1 && k == Logic::L1)
                    m_q = logicNot(m_q);
                else if (j == Logic::L1)
                    m_q = Logic::L1;
                else if (k == Logic::L1)
                    m_q = Logic::L0;
            } else {
                const Logic d = ctx.input(m_p.d);
                m_q = isKnown(d) ? d : Logic::X;
            }
        }
        output(ctx);
    }

    int state() const override { return isKnown(m_q) ? int(m_q == Logic::L1) : -1; }

private:
    static bool active(ModelContext& ctx, int pin, bool low)
    {
        if (pin < 0) return false;
        return ctx.input(pin) == (low ? Logic::L0 : Logic::L1);
    }
    void output(ModelContext& ctx)
    {
        const std::uint32_t delay = ctx.settling() ? 0 : 1;
        if (m_p.q >= 0) ctx.drive(m_p.q, strong(m_q), delay);
        if (m_p.qn >= 0) ctx.drive(m_p.qn, strong(logicNot(m_q)), delay);
    }

    bool m_jk;
    Pins m_p;
    Logic m_q = Logic::L0;
    Logic m_lastClk = Logic::X;
};

class ConstModel : public Model {
public:
    ConstModel(int pin, Logic v) : m_pin(pin), m_v(v) {}
    void evaluate(ModelContext& ctx) override { ctx.drive(m_pin, strong(m_v), 0); }
    int state() const override { return m_v == Logic::L1; }

private:
    int m_pin;
    Logic m_v;
};

class LogicSwitchModel : public Model {
public:
    LogicSwitchModel(int pin, bool on) : m_pin(pin), m_on(on) {}
    void evaluate(ModelContext& ctx) override { ctx.drive(m_pin, strong(fromBool(m_on)), 0); }
    void interact(ModelContext& ctx, const QString& action, int value) override
    {
        if (action == u"toggle") m_on = !m_on;
        else if (action == u"set") m_on = value != 0;
        else return;
        evaluate(ctx);
    }
    int state() const override { return m_on; }
    QVariantMap persistentProps() const override { return {{QStringLiteral("state"), int(m_on)}}; }

private:
    int m_pin;
    bool m_on;
};

class PulserModel : public Model {
public:
    PulserModel(int q, int qn, int high, int low) : m_q(q), m_qn(qn), m_high(std::max(1, high)), m_low(std::max(1, low)) {}
    void init(ModelContext& ctx) override
    {
        m_level = false;
        output(ctx);
        ctx.wakeAfter(m_low);
    }
    void evaluate(ModelContext&) override {}
    void wake(ModelContext& ctx) override
    {
        m_level = !m_level;
        output(ctx);
        ctx.wakeAfter(m_level ? m_high : m_low);
    }
    int state() const override { return m_level; }

private:
    void output(ModelContext& ctx)
    {
        if (m_q >= 0) ctx.drive(m_q, strong(fromBool(m_level)), 0);
        if (m_qn >= 0) ctx.drive(m_qn, strong(fromBool(!m_level)), 0);
    }
    int m_q, m_qn, m_high, m_low;
    bool m_level = false;
};

// Drives a binary value on D0..Dn (hex key, ASCII key).
class KeyModel : public Model {
public:
    KeyModel(QVector<int> bits, int strobe, int value) : m_bits(std::move(bits)), m_strobe(strobe), m_value(value) {}
    void evaluate(ModelContext& ctx) override
    {
        for (int i = 0; i < m_bits.size(); ++i)
            if (m_bits[i] >= 0)
                ctx.drive(m_bits[i], strong(fromBool(m_value >> i & 1)), 0);
        if (m_strobe >= 0)
            ctx.drive(m_strobe, strong(fromBool(m_pressed)), 0);
    }
    void interact(ModelContext& ctx, const QString& action, int value) override
    {
        if (action == u"key") {
            m_value = value & ((1 << m_bits.size()) - 1);
            m_pressed = true;
        } else if (action == u"release") {
            m_pressed = false;
        } else {
            return;
        }
        evaluate(ctx);
    }
    int state() const override { return m_value; }
    QVariantMap persistentProps() const override { return {{QStringLiteral("value"), m_value}}; }

private:
    QVector<int> m_bits;
    int m_strobe;
    int m_value;
    bool m_pressed = false;
};

// Reads D0..Dn as a binary number (lamp: one input; hex: 4; ASCII: 7).
class DisplayModel : public Model {
public:
    explicit DisplayModel(QVector<int> bits) : m_bits(std::move(bits)) {}
    void init(ModelContext& ctx) override { evaluate(ctx); }
    void evaluate(ModelContext& ctx) override
    {
        int v = 0;
        for (int i = 0; i < m_bits.size(); ++i) {
            const Logic b = ctx.input(m_bits[i]);
            if (!isKnown(b)) { v = -1; break; }
            v |= int(b == Logic::L1) << i;
        }
        m_value = v;
    }
    int state() const override { return m_value; }

private:
    QVector<int> m_bits;
    int m_value = -1;
};

class LedModel : public Model {
public:
    LedModel(int anode, int cathode) : m_a(anode), m_k(cathode) {}
    void evaluate(ModelContext& ctx) override
    {
        const Logic a = ctx.input(m_a), k = ctx.input(m_k);
        m_on = a == Logic::L1 && k == Logic::L0;
    }
    int state() const override { return m_on; }

private:
    int m_a, m_k;
    bool m_on = false;
};

// Switches, push buttons and resistors: conducting paths between pins.
class LinkModel : public Model {
public:
    enum class Kind { NormallyOpen, NormallyClosed, Toggle, ChangeOver, Resistor };
    LinkModel(Kind kind, int a, int b, int c, bool closed) : m_kind(kind), m_a(a), m_b(b), m_c(c), m_closed(closed) {}

    void evaluate(ModelContext&) override {}
    void interact(ModelContext& ctx, const QString& action, int value) override
    {
        const bool before = m_closed;
        if (action == u"press") m_closed = true;
        else if (action == u"release") m_closed = false;
        else if (action == u"toggle") m_closed = !m_closed;
        else if (action == u"set") m_closed = value != 0;
        if (m_kind == Kind::Resistor) m_closed = before;
        if (m_closed != before)
            ctx.linksChanged();
    }
    QVector<LinkPath> links() const override
    {
        switch (m_kind) {
        case Kind::Resistor: return {{m_a, m_b, Strength::Weak}};
        case Kind::NormallyOpen:
        case Kind::Toggle: return m_closed ? QVector<LinkPath>{{m_a, m_b}} : QVector<LinkPath>{};
        case Kind::NormallyClosed: return m_closed ? QVector<LinkPath>{} : QVector<LinkPath>{{m_a, m_b}};
        case Kind::ChangeOver: return {{m_a, m_closed ? m_c : m_b}};
        }
        return {};
    }
    int state() const override { return m_closed; }
    QVariantMap persistentProps() const override
    {
        return m_kind == Kind::Toggle ? QVariantMap{{QStringLiteral("closed"), int(m_closed)}} : QVariantMap{};
    }

private:
    Kind m_kind;
    int m_a, m_b, m_c;
    bool m_closed;
};

} // namespace

std::unique_ptr<Model> createModel(const SymbolDef& s, const Component& c, QString* error)
{
    const QString kind = s.model.value(u"kind").toString();
    if (kind.isEmpty())
        return nullptr;
    const QVariantMap props = mergedProps(s, c);
    auto fail = [&](const QString& why) -> std::unique_ptr<Model> {
        if (error) *error = QStringLiteral("%1 (%2): %3").arg(c.designator, s.id, why);
        return nullptr;
    };

    if (kind == u"gate") {
        const QString opName = s.model.value(u"op").toString();
        const GateModel::Op op = opName == u"and" ? GateModel::Op::And
                               : opName == u"or" ? GateModel::Op::Or
                               : opName == u"xor" ? GateModel::Op::Xor
                               : GateModel::Op::Buf;
        QVector<int> inputs;
        for (int i = 0; i < s.pins.size(); ++i)
            if (s.pins[i].dir == SymbolPin::Dir::In)
                inputs << i;
        const int out = s.pinIndex(QStringLiteral("Y"));
        if (out < 0 || inputs.isEmpty())
            return fail(QStringLiteral("gate needs inputs and a Y output"));
        return std::make_unique<GateModel>(op, s.model.value(u"invert").toBool(), inputs, out);
    }
    if (kind == u"tribuf") {
        bool enLow = false;
        const int en = findPin(s, {"EN", "OE", "G"}, &enLow);
        const int in = s.pinIndex(QStringLiteral("A")), out = s.pinIndex(QStringLiteral("Y"));
        if (en < 0 || in < 0 || out < 0)
            return fail(QStringLiteral("tri-state buffer needs A, EN and Y"));
        return std::make_unique<TriBufModel>(in, en, enLow, out, s.model.value(u"invert").toBool());
    }
    if (kind == u"dff" || kind == u"jkff") {
        FlipFlopModel::Pins p;
        const bool jk = kind == u"jkff";
        p.clk = findPin(s, {"CLK", "C"}, &p.fallingEdge);
        if (s.model.value(u"edge").toString() == u"fall")
            p.fallingEdge = true;
        p.d = s.pinIndex(QStringLiteral("D"));
        p.j = s.pinIndex(QStringLiteral("J"));
        p.k = s.pinIndex(QStringLiteral("K"));
        p.q = s.pinIndex(QStringLiteral("Q"));
        p.qn = s.pinIndex(QStringLiteral("~Q"));
        p.set = findPin(s, {"S", "SET", "PRE", "SD"}, &p.setLow);
        p.reset = findPin(s, {"R", "RST", "CLR", "RD"}, &p.resetLow);
        if (p.clk < 0 || (jk ? (p.j < 0 || p.k < 0) : p.d < 0))
            return fail(QStringLiteral("flip-flop is missing data or clock pins"));
        return std::make_unique<FlipFlopModel>(jk, p);
    }
    if (kind == u"const") {
        if (s.pins.isEmpty())
            return fail(QStringLiteral("constant without pin"));
        return std::make_unique<ConstModel>(0, fromBool(s.model.value(u"value").toInt() != 0));
    }
    if (kind == u"logicswitch") {
        if (s.pins.isEmpty())
            return fail(QStringLiteral("switch without pin"));
        return std::make_unique<LogicSwitchModel>(0, props.value(QStringLiteral("state")).toInt() != 0);
    }
    if (kind == u"pulser") {
        const int q = s.pinIndex(QStringLiteral("Q"));
        if (q < 0)
            return fail(QStringLiteral("pulser needs a Q output"));
        return std::make_unique<PulserModel>(q, s.pinIndex(QStringLiteral("~Q")),
                                             props.value(QStringLiteral("high"), 50).toInt(),
                                             props.value(QStringLiteral("low"), 50).toInt());
    }
    if (kind == u"key") {
        const int bits = s.model.value(u"bits").toInt(4);
        return std::make_unique<KeyModel>(bitPins(s, bits), s.pinIndex(QStringLiteral("STB")),
                                          props.value(QStringLiteral("value")).toInt());
    }
    if (kind == u"display") {
        const int bits = s.model.value(u"bits").toInt(1);
        QVector<int> pins = bits == 1 && s.pinIndex(QStringLiteral("D0")) < 0 ? QVector<int>{0} : bitPins(s, bits);
        if (pins.contains(-1))
            return fail(QStringLiteral("display is missing D0..D%1").arg(bits - 1));
        return std::make_unique<DisplayModel>(pins);
    }
    if (kind == u"led") {
        const int a = s.pinIndex(QStringLiteral("A")), k = s.pinIndex(QStringLiteral("K"));
        if (a < 0 || k < 0)
            return fail(QStringLiteral("LED needs A and K"));
        return std::make_unique<LedModel>(a, k);
    }
    if (kind == u"link") {
        const QString type = s.model.value(u"type").toString();
        if (s.pins.size() < 2)
            return fail(QStringLiteral("link needs two pins"));
        if (type == u"spdt") {
            const int com = s.pinIndex(QStringLiteral("C")), nc = s.pinIndex(QStringLiteral("NC")),
                      no = s.pinIndex(QStringLiteral("NO"));
            if (com < 0 || nc < 0 || no < 0)
                return fail(QStringLiteral("SPDT needs C, NC and NO"));
            return std::make_unique<LinkModel>(LinkModel::Kind::ChangeOver, com, nc, no, false);
        }
        const LinkModel::Kind k = type == u"no" ? LinkModel::Kind::NormallyOpen
                                : type == u"nc" ? LinkModel::Kind::NormallyClosed
                                : type == u"resistor" ? LinkModel::Kind::Resistor
                                : LinkModel::Kind::Toggle;
        return std::make_unique<LinkModel>(k, 0, 1, -1, props.value(QStringLiteral("closed")).toInt() != 0);
    }
    QString chipError;
    std::unique_ptr<Model> chip = createChipModel(s, c, &chipError);
    return chip ? std::move(chip) : fail(chipError);
}

} // namespace cm::sim
