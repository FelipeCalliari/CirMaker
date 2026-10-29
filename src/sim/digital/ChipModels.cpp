// Behavioural models of MSI chips (74xx/40xx counters, decoders, multiplexers,
// arithmetic, registers). Pins are looked up by name; "~X" marks an active-low
// pin. Behaviour follows the public datasheets.

#include "sim/digital/Models.h"

#include "core/Circuit.h"
#include "library/SymbolDef.h"

#include <QJsonArray>
#include <QJsonObject>

#include <optional>

namespace cm::sim {

namespace {

// Pin lookup helpers bound to one symbol.
class Pins {
public:
    explicit Pins(const SymbolDef& s) : m_s(s) {}

    int at(const QString& name) const { return m_s.pinIndex(name); }
    // Index of `name` or its active-low form; *low tells which one exists.
    int either(const QString& name, bool* low) const
    {
        if (const int i = at(name); i >= 0) { *low = false; return i; }
        if (const int i = at(u'~' + name); i >= 0) { *low = true; return i; }
        return -1;
    }
    QVector<int> series(const QString& prefix, int from, int count) const
    {
        QVector<int> out;
        for (int i = 0; i < count; ++i)
            out << at(prefix + QString::number(from + i));
        return out;
    }
    // Consecutive pins prefix0, prefix1, ... as long as they exist.
    QVector<int> run(const QString& prefix) const
    {
        QVector<int> out;
        for (int i = 0;; ++i) {
            const int p = at(prefix + QString::number(i));
            if (p < 0) break;
            out << p;
        }
        return out;
    }

private:
    const SymbolDef& m_s;
};

// Control input with polarity; absent pins read as inactive.
struct Control {
    int pin = -1;
    bool low = false;
    bool present() const { return pin >= 0; }
    // 1 active, 0 inactive, -1 unknown
    int state(ModelContext& ctx) const
    {
        if (pin < 0) return 0;
        const Logic v = ctx.input(pin);
        if (!isKnown(v)) return -1;
        return (v == Logic::L1) != low;
    }
    bool active(ModelContext& ctx) const { return state(ctx) == 1; }
};

Control control(const Pins& p, const QString& name)
{
    Control c;
    c.pin = p.either(name, &c.low);
    return c;
}

// Rising/falling edge detector that ignores reset start-up transitions.
struct Edge {
    Logic last = Logic::X;
    bool falling = false;
    bool operator()(ModelContext& ctx, int pin)
    {
        const Logic now = ctx.input(pin);
        const bool edge = !ctx.settling() && (falling ? (last == Logic::L1 && now == Logic::L0)
                                                      : (last == Logic::L0 && now == Logic::L1));
        last = now;
        return edge;
    }
};

// Reads pins as an unsigned number (index 0 = LSB); nullopt if any bit is unknown.
std::optional<int> readBus(ModelContext& ctx, const QVector<int>& pins)
{
    int v = 0;
    for (int i = 0; i < pins.size(); ++i) {
        if (pins[i] < 0) continue;
        const Logic b = ctx.input(pins[i]);
        if (!isKnown(b)) return std::nullopt;
        v |= int(b == Logic::L1) << i;
    }
    return v;
}

void driveBus(ModelContext& ctx, const QVector<int>& pins, std::optional<int> value, bool invert = false)
{
    for (int i = 0; i < pins.size(); ++i) {
        if (pins[i] < 0) continue;
        const Logic b = value ? fromBool((*value >> i & 1) != invert) : Logic::X;
        ctx.drive(pins[i], strong(b));
    }
}

void driveBit(ModelContext& ctx, int pin, Logic v)
{
    if (pin >= 0) ctx.drive(pin, strong(v));
}

// ------------------------------------------------------------ 7490 / 7493

// Ripple counter: QA toggles on CKA; QB..QD count modulo `modB` on CKB.
class RippleCounter : public Model {
public:
    RippleCounter(const Pins& p, int modB)
        : m_modB(modB), m_cka(p.at(QStringLiteral("~CKA"))), m_ckb(p.at(QStringLiteral("~CKB"))),
          m_r0a(p.at(QStringLiteral("R01"))), m_r0b(p.at(QStringLiteral("R02"))),
          m_r9a(p.at(QStringLiteral("R91"))), m_r9b(p.at(QStringLiteral("R92"))),
          m_q{p.at(QStringLiteral("QA")), p.at(QStringLiteral("QB")), p.at(QStringLiteral("QC")), p.at(QStringLiteral("QD"))}
    {
        m_ea.falling = m_eb.falling = true;
    }
    void evaluate(ModelContext& ctx) override
    {
        const bool ea = m_ea(ctx, m_cka), eb = m_eb(ctx, m_ckb);
        auto high = [&](int pin) { return pin >= 0 && ctx.input(pin) == Logic::L1; };
        if (high(m_r9a) && high(m_r9b)) {
            m_a = 1; m_b = 4;                       // 9 = 1001
        } else if (high(m_r0a) && high(m_r0b)) {
            m_a = 0; m_b = 0;
        } else {
            if (ea) m_a ^= 1;
            if (eb) m_b = (m_b + 1) % m_modB;
        }
        driveBit(ctx, m_q[0], fromBool(m_a));
        for (int i = 0; i < 3; ++i)
            driveBit(ctx, m_q[i + 1], fromBool(m_b >> i & 1));
    }
    int state() const override { return m_a | m_b << 1; }

private:
    int m_modB, m_cka, m_ckb, m_r0a, m_r0b, m_r9a, m_r9b;
    int m_q[4];
    Edge m_ea, m_eb;
    int m_a = 0, m_b = 0;
};

// ------------------------------------------------------------ 74161 / 74163

class SyncCounter : public Model {
public:
    SyncCounter(const Pins& p, bool syncClear)
        : m_syncClear(syncClear), m_clk(p.at(QStringLiteral("CLK"))), m_clr(control(p, QStringLiteral("CLR"))),
          m_load(control(p, QStringLiteral("LOAD"))), m_enp(control(p, QStringLiteral("ENP"))),
          m_ent(control(p, QStringLiteral("ENT"))), m_d(p.run(QStringLiteral("D"))), m_q(p.run(QStringLiteral("Q"))),
          m_rco(p.at(QStringLiteral("RCO")))
    {}
    void evaluate(ModelContext& ctx) override
    {
        const bool edge = m_edge(ctx, m_clk);
        const int mask = (1 << m_q.size()) - 1;
        if (!m_syncClear && m_clr.active(ctx)) {
            m_count = 0;
        } else if (edge) {
            if (m_syncClear && m_clr.active(ctx))
                m_count = 0;
            else if (m_load.active(ctx))
                m_count = readBus(ctx, m_d).value_or(m_count);
            else if (enabled(ctx, m_enp) && enabled(ctx, m_ent))
                m_count = (m_count + 1) & mask;
        }
        driveBus(ctx, m_q, m_count);
        driveBit(ctx, m_rco, fromBool(enabled(ctx, m_ent) && m_count == mask));
    }
    int state() const override { return m_count; }

private:
    static bool enabled(ModelContext& ctx, const Control& c) { return !c.present() || c.active(ctx); }
    bool m_syncClear;
    int m_clk;
    Control m_clr, m_load, m_enp, m_ent;
    QVector<int> m_d, m_q;
    int m_rco;
    Edge m_edge;
    int m_count = 0;
};

// ------------------------------------------------------------ 74192 / 74193

class UpDownCounter : public Model {
public:
    UpDownCounter(const Pins& p, int modulus)
        : m_mod(modulus), m_up(p.at(QStringLiteral("UP"))), m_down(p.at(QStringLiteral("DOWN"))),
          m_clr(control(p, QStringLiteral("CLR"))), m_load(control(p, QStringLiteral("LOAD"))),
          m_d(p.run(QStringLiteral("D"))), m_q(p.run(QStringLiteral("Q"))),
          m_co(p.at(QStringLiteral("~CO"))), m_bo(p.at(QStringLiteral("~BO")))
    {}
    void evaluate(ModelContext& ctx) override
    {
        const bool up = m_eu(ctx, m_up), down = m_ed(ctx, m_down);
        if (m_clr.active(ctx)) {
            m_count = 0;
        } else if (m_load.active(ctx)) {
            m_count = readBus(ctx, m_d).value_or(m_count) % 16;
        } else {
            if (up) m_count = (m_count + 1) % m_mod;
            if (down) m_count = (m_count + m_mod - 1) % m_mod;
        }
        driveBus(ctx, m_q, m_count);
        driveBit(ctx, m_co, fromBool(!(m_count == m_mod - 1 && ctx.input(m_up) == Logic::L0)));
        driveBit(ctx, m_bo, fromBool(!(m_count == 0 && ctx.input(m_down) == Logic::L0)));
    }
    int state() const override { return m_count; }

private:
    int m_mod, m_up, m_down;
    Control m_clr, m_load;
    QVector<int> m_d, m_q;
    int m_co, m_bo;
    Edge m_eu, m_ed;
    int m_count = 0;
};

// ------------------------------------------------------------ decoders

// Binary decoder: select A (LSB), B, C, D; enables G, G1, G2, G2A, G2B
// (any polarity); outputs Y0.. or ~Y0.. (active low).
class Decoder : public Model {
public:
    explicit Decoder(const Pins& p)
    {
        for (const char* n : {"A", "B", "C", "D"})
            if (const int i = p.at(QString::fromLatin1(n)); i >= 0) m_sel << i;
        for (const char* n : {"G", "G1", "G2", "G2A", "G2B", "E"})
            if (Control c = control(p, QString::fromLatin1(n)); c.present()) m_en << c;
        m_out = p.run(QStringLiteral("Y"));
        if (m_out.isEmpty()) {
            m_out = p.run(QStringLiteral("~Y"));
            m_low = true;
        }
    }
    void evaluate(ModelContext& ctx) override
    {
        int enabled = 1;
        for (const Control& c : std::as_const(m_en))
            enabled = std::min(enabled, c.state(ctx) < 0 ? -1 : c.state(ctx));
        const std::optional<int> sel = readBus(ctx, m_sel);
        for (int i = 0; i < m_out.size(); ++i) {
            Logic v;
            if (enabled == 0) v = Logic::L0;
            else if (enabled < 0 || !sel) v = Logic::X;
            else v = fromBool(*sel == i);
            driveBit(ctx, m_out[i], m_low ? logicNot(v) : v);
        }
    }

private:
    QVector<int> m_sel;
    QVector<Control> m_en;
    QVector<int> m_out;
    bool m_low = false;
};

// BCD to seven-segment decoder/driver (7447 active-low, 7448 active-high).
class SevenSegDecoder : public Model {
public:
    explicit SevenSegDecoder(const Pins& p)
        : m_in(QVector<int>{p.at(QStringLiteral("A")), p.at(QStringLiteral("B")), p.at(QStringLiteral("C")), p.at(QStringLiteral("D"))}),
          m_lt(control(p, QStringLiteral("LT"))), m_bi(control(p, QStringLiteral("BI"))), m_rbi(control(p, QStringLiteral("RBI")))
    {
        for (const char* seg : {"a", "b", "c", "d", "e", "f", "g"}) {
            int i = p.at(QString::fromLatin1(seg));
            if (i < 0) {
                i = p.at(u'~' + QString::fromLatin1(seg));
                m_low = i >= 0 || m_low;
            }
            m_seg << i;
        }
    }
    void evaluate(ModelContext& ctx) override
    {
        // segment bits a..g = bit 0..6; 10..15 are the 7447's odd glyphs
        static constexpr int glyph[16] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7c, 0x07,
                                          0x7f, 0x67, 0x58, 0x4c, 0x62, 0x69, 0x78, 0x00};
        std::optional<int> pattern;
        const std::optional<int> v = readBus(ctx, m_in);
        if (m_bi.active(ctx)) pattern = 0;
        else if (m_lt.active(ctx)) pattern = 0x7f;
        else if (v && *v == 0 && m_rbi.active(ctx)) pattern = 0;
        else if (v) pattern = glyph[*v];
        driveBus(ctx, m_seg, pattern, m_low);
    }

private:
    QVector<int> m_in;
    Control m_lt, m_bi, m_rbi;
    QVector<int> m_seg;
    bool m_low = false;
};

// Seven-segment display; state() = lit segments a..g, dp as bits 0..7.
class SevenSegDisplay : public Model {
public:
    SevenSegDisplay(const Pins& p, bool commonAnode) : m_anode(commonAnode), m_com(p.at(QStringLiteral("COM")))
    {
        for (const char* seg : {"a", "b", "c", "d", "e", "f", "g", "dp"})
            m_seg << p.at(QString::fromLatin1(seg));
    }
    void evaluate(ModelContext& ctx) override
    {
        const Logic com = m_com >= 0 && ctx.connected(m_com) ? ctx.input(m_com) : (m_anode ? Logic::L1 : Logic::L0);
        const bool powered = m_anode ? com == Logic::L1 : com == Logic::L0;
        int lit = 0;
        for (int i = 0; i < m_seg.size(); ++i) {
            if (m_seg[i] < 0 || !powered || !ctx.connected(m_seg[i])) continue;
            const Logic v = ctx.input(m_seg[i]);
            if (v == (m_anode ? Logic::L0 : Logic::L1)) lit |= 1 << i;
        }
        m_lit = lit;
    }
    int state() const override { return m_lit; }

private:
    bool m_anode;
    int m_com;
    QVector<int> m_seg;
    int m_lit = 0;
};

// ------------------------------------------------------------ multiplexers

class Mux : public Model {
public:
    explicit Mux(const Pins& p) : m_data(p.run(QStringLiteral("D"))), m_g(control(p, QStringLiteral("G"))),
                                  m_y(p.at(QStringLiteral("Y"))), m_w(p.at(QStringLiteral("~W")))
    {
        for (const char* n : {"A", "B", "C"})
            if (const int i = p.at(QString::fromLatin1(n)); i >= 0) m_sel << i;
    }
    void evaluate(ModelContext& ctx) override
    {
        Logic y = Logic::X;
        const int en = m_g.present() ? m_g.state(ctx) : 1;
        const std::optional<int> sel = readBus(ctx, m_sel);
        if (en == 0) y = Logic::L0;
        else if (en == 1 && sel && *sel < m_data.size()) {
            y = ctx.input(m_data[*sel]);
            if (y == Logic::Z) y = Logic::X;
        }
        driveBit(ctx, m_y, y);
        driveBit(ctx, m_w, logicNot(y));
    }

private:
    QVector<int> m_data, m_sel;
    Control m_g;
    int m_y, m_w;
};

// Quad 2-to-1 (74157): Yn = S ? Bn : An while enabled.
class QuadMux2 : public Model {
public:
    explicit QuadMux2(const Pins& p) : m_a(p.run(QStringLiteral("A"))), m_b(p.run(QStringLiteral("B"))),
                                       m_y(p.run(QStringLiteral("Y"))), m_s(p.at(QStringLiteral("S"))),
                                       m_g(control(p, QStringLiteral("G"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const int en = m_g.present() ? m_g.state(ctx) : 1;
        const Logic s = ctx.input(m_s);
        for (int i = 0; i < m_y.size(); ++i) {
            Logic v = Logic::X;
            if (en == 0) v = Logic::L0;
            else if (en == 1 && isKnown(s)) v = ctx.input(s == Logic::L1 ? m_b.value(i, -1) : m_a.value(i, -1));
            if (v == Logic::Z) v = Logic::X;
            driveBit(ctx, m_y[i], v);
        }
    }

private:
    QVector<int> m_a, m_b, m_y;
    int m_s;
    Control m_g;
};

// ------------------------------------------------------------ arithmetic

class Adder : public Model {
public:
    explicit Adder(const Pins& p) : m_a(p.run(QStringLiteral("A"))), m_b(p.run(QStringLiteral("B"))),
                                    m_s(p.run(QStringLiteral("S"))), m_ci(p.at(QStringLiteral("CI"))),
                                    m_co(p.at(QStringLiteral("CO"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const auto a = readBus(ctx, m_a), b = readBus(ctx, m_b);
        const Logic ci = m_ci >= 0 ? ctx.input(m_ci) : Logic::L0;
        std::optional<int> sum;
        if (a && b && isKnown(ci))
            sum = *a + *b + (ci == Logic::L1);
        driveBus(ctx, m_s, sum);
        driveBit(ctx, m_co, sum ? fromBool(*sum >> m_s.size() & 1) : Logic::X);
    }

private:
    QVector<int> m_a, m_b, m_s;
    int m_ci, m_co;
};

// 4-bit magnitude comparator with cascade inputs (7485).
class Comparator : public Model {
public:
    explicit Comparator(const Pins& p)
        : m_a(p.run(QStringLiteral("A"))), m_b(p.run(QStringLiteral("B"))),
          m_ilt(p.at(QStringLiteral("I<"))), m_ieq(p.at(QStringLiteral("I="))), m_igt(p.at(QStringLiteral("I>"))),
          m_lt(p.at(QStringLiteral("A<B"))), m_eq(p.at(QStringLiteral("A=B"))), m_gt(p.at(QStringLiteral("A>B"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const auto a = readBus(ctx, m_a), b = readBus(ctx, m_b);
        Logic lt = Logic::X, eq = Logic::X, gt = Logic::X;
        if (a && b) {
            if (*a != *b) {
                gt = fromBool(*a > *b);
                lt = fromBool(*a < *b);
                eq = Logic::L0;
            } else {
                auto in = [&](int pin, bool def) { return pin < 0 ? fromBool(def) : ctx.input(pin); };
                const Logic ieq = in(m_ieq, true), igt = in(m_igt, false), ilt = in(m_ilt, false);
                if (ieq == Logic::L1) {
                    eq = Logic::L1; gt = lt = Logic::L0;
                } else {
                    eq = Logic::L0;
                    gt = logicNot(ilt);
                    lt = logicNot(igt);
                    if (igt == Logic::L0 && ilt == Logic::L0) gt = lt = Logic::L1;
                    if (igt == Logic::L1 && ilt == Logic::L1) gt = lt = Logic::L0;
                }
            }
        }
        driveBit(ctx, m_lt, lt);
        driveBit(ctx, m_eq, eq);
        driveBit(ctx, m_gt, gt);
    }

private:
    QVector<int> m_a, m_b;
    int m_ilt, m_ieq, m_igt, m_lt, m_eq, m_gt;
};

// ------------------------------------------------------------ registers

// D register or transparent latch, with optional clear and output enable.
class Register : public Model {
public:
    explicit Register(const Pins& p)
        : m_d(p.run(QStringLiteral("D"))), m_q(p.run(QStringLiteral("Q"))), m_qn(p.run(QStringLiteral("~Q"))),
          m_clk(p.at(QStringLiteral("CLK"))), m_le(p.at(QStringLiteral("LE"))),
          m_clr(control(p, QStringLiteral("CLR"))), m_oe(control(p, QStringLiteral("OE"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool edge = m_clk >= 0 && m_edge(ctx, m_clk);
        if (m_clr.active(ctx))
            m_value = 0;
        else if (edge || (m_le >= 0 && ctx.input(m_le) == Logic::L1))
            m_value = readBus(ctx, m_d);
        const bool enabled = !m_oe.present() || m_oe.active(ctx);
        for (int i = 0; i < m_q.size(); ++i) {
            if (!enabled) {
                ctx.drive(m_q[i], released());
                continue;
            }
            driveBit(ctx, m_q[i], m_value ? fromBool(*m_value >> i & 1) : Logic::X);
        }
        driveBus(ctx, m_qn, m_value, true);
    }
    int state() const override { return m_value.value_or(-1); }

private:
    QVector<int> m_d, m_q, m_qn;
    int m_clk, m_le;
    Control m_clr, m_oe;
    Edge m_edge;
    std::optional<int> m_value = 0;
};

// 74164 (serial in A·B) and 74194 (universal: S1 S0 = hold, right, left, load).
class ShiftRegister : public Model {
public:
    explicit ShiftRegister(const Pins& p)
        : m_q(p.run(QStringLiteral("Q"))), m_d(p.run(QStringLiteral("D"))), m_clk(p.at(QStringLiteral("CLK"))),
          m_clr(control(p, QStringLiteral("CLR"))), m_a(p.at(QStringLiteral("A"))), m_b(p.at(QStringLiteral("B"))),
          m_sr(p.at(QStringLiteral("SR"))), m_sl(p.at(QStringLiteral("SL"))),
          m_s0(p.at(QStringLiteral("S0"))), m_s1(p.at(QStringLiteral("S1"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool edge = m_edge(ctx, m_clk);
        const int n = int(m_q.size());
        const int mask = (1 << n) - 1;
        auto bit = [&](int pin) { return pin >= 0 && ctx.input(pin) == Logic::L1; };
        if (m_clr.active(ctx)) {
            m_v = 0;
        } else if (edge) {
            int mode = 1;                                   // shift right (towards Q(n-1))
            if (m_s0 >= 0) mode = int(bit(m_s0)) | int(bit(m_s1)) << 1;
            const bool serial = m_s0 >= 0 ? bit(m_sr) : (bit(m_a) && (m_b < 0 || bit(m_b)));
            switch (mode) {
            case 0: break;
            case 1: m_v = ((m_v << 1) | int(serial)) & mask; break;
            case 2: m_v = (m_v >> 1) | int(bit(m_sl)) << (n - 1); break;
            case 3: m_v = readBus(ctx, m_d).value_or(m_v); break;
            }
        }
        driveBus(ctx, m_q, m_v);
    }
    int state() const override { return m_v; }

private:
    QVector<int> m_q, m_d;
    int m_clk;
    Control m_clr;
    int m_a, m_b, m_sr, m_sl, m_s0, m_s1;
    Edge m_edge;
    int m_v = 0;
};

// Groups of tri-state buffers An -> Yn sharing an enable (74244, Quad 3-State).
class TriBus : public Model {
public:
    TriBus(const Pins& p, const QJsonArray& groups)
    {
        const QVector<int> a = p.run(QStringLiteral("A")), y = p.run(QStringLiteral("Y"));
        for (const QJsonValue& g : groups) {
            const QJsonObject o = g.toObject();
            Group grp;
            grp.en = control(p, o.value(u"enable").toString());
            for (const QJsonValue& b : o.value(u"bits").toArray()) {
                const int i = b.toInt();
                grp.pairs << std::pair{a.value(i, -1), y.value(i, -1)};
            }
            m_groups << grp;
        }
    }
    void evaluate(ModelContext& ctx) override
    {
        for (const Group& g : std::as_const(m_groups)) {
            const int en = g.en.present() ? g.en.state(ctx) : 1;
            for (auto [in, out] : g.pairs) {
                if (out < 0) continue;
                if (en == 0) { ctx.drive(out, released()); continue; }
                Logic v = en < 0 ? Logic::X : ctx.input(in);
                if (v == Logic::Z) v = Logic::X;
                ctx.drive(out, strong(v));
            }
        }
    }

private:
    struct Group {
        Control en;
        QVector<std::pair<int, int>> pairs;
    };
    QVector<Group> m_groups;
};

} // namespace

std::unique_ptr<Model> createChipModel(const SymbolDef& s, const Component&, QString* error)
{
    const Pins p(s);
    const QString kind = s.model.value(u"kind").toString();
    if (kind == u"ripple") return std::make_unique<RippleCounter>(p, s.model.value(u"modB").toInt(8));
    if (kind == u"sync") return std::make_unique<SyncCounter>(p, s.model.value(u"syncClear").toBool());
    if (kind == u"updown") return std::make_unique<UpDownCounter>(p, s.model.value(u"modulus").toInt(16));
    if (kind == u"decoder") return std::make_unique<Decoder>(p);
    if (kind == u"bcd7seg") return std::make_unique<SevenSegDecoder>(p);
    if (kind == u"seg7") return std::make_unique<SevenSegDisplay>(p, s.model.value(u"anode").toBool());
    if (kind == u"mux") return std::make_unique<Mux>(p);
    if (kind == u"mux2") return std::make_unique<QuadMux2>(p);
    if (kind == u"adder") return std::make_unique<Adder>(p);
    if (kind == u"comparator") return std::make_unique<Comparator>(p);
    if (kind == u"register") return std::make_unique<Register>(p);
    if (kind == u"shift") return std::make_unique<ShiftRegister>(p);
    if (kind == u"tribus") return std::make_unique<TriBus>(p, s.model.value(u"groups").toArray());
    if (error) *error = QStringLiteral("unknown model kind '%1'").arg(kind);
    return nullptr;
}

} // namespace cm::sim
