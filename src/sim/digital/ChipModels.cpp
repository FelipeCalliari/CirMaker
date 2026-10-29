// Behavioural models of MSI chips (74xx/40xx counters, decoders, multiplexers,
// arithmetic, registers). Pins are looked up by name; "~X" marks an active-low
// pin. Behaviour follows the public datasheets.

#include "sim/digital/Models.h"

#include "core/Circuit.h"
#include "library/SymbolDef.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QRegularExpression>

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
    RippleCounter(const Pins& p, int modB, bool seq92)
        : m_modB(modB), m_seq92(seq92), m_cka(p.at(QStringLiteral("~CKA"))), m_ckb(p.at(QStringLiteral("~CKB"))),
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
        // 7492: B section counts 0,1,2,4,5,6 (divide by 3, then by 2 on QD)
        const int b = m_seq92 && m_b >= 3 ? (m_b - 3) | 4 : m_b;
        driveBit(ctx, m_q[0], fromBool(m_a));
        for (int i = 0; i < 3; ++i)
            driveBit(ctx, m_q[i + 1], fromBool(b >> i & 1));
    }
    int state() const override { return m_a | m_b << 1; }

private:
    int m_modB;
    bool m_seq92;
    int m_cka, m_ckb, m_r0a, m_r0b, m_r9a, m_r9b;
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
    Mux(const Pins& p, bool tristate) : m_data(p.run(QStringLiteral("D"))), m_g(control(p, QStringLiteral("G"))),
                                        m_y(p.at(QStringLiteral("Y"))), m_w(p.at(QStringLiteral("~W"))), m_tristate(tristate)
    {
        for (const char* n : {"A", "B", "C"})
            if (const int i = p.at(QString::fromLatin1(n)); i >= 0) m_sel << i;
    }
    void evaluate(ModelContext& ctx) override
    {
        Logic y = Logic::X;
        const int en = m_g.present() ? m_g.state(ctx) : 1;
        const std::optional<int> sel = readBus(ctx, m_sel);
        if (en == 0 && m_tristate) {
            if (m_y >= 0) ctx.drive(m_y, released());
            if (m_w >= 0) ctx.drive(m_w, released());
            return;
        }
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
    bool m_tristate;
};

// Quad 2-to-1 (74157): Yn = S ? Bn : An while enabled.
class QuadMux2 : public Model {
public:
    explicit QuadMux2(const Pins& p) : m_a(p.run(QStringLiteral("A"))), m_b(p.run(QStringLiteral("B"))),
                                       m_y(p.run(QStringLiteral("Y"))), m_s(p.at(QStringLiteral("S"))),
                                       m_g(control(p, QStringLiteral("G"))), m_oe(control(p, QStringLiteral("OE"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const int en = m_g.present() ? m_g.state(ctx) : 1;
        const Logic s = ctx.input(m_s);
        const bool off = m_oe.present() && m_oe.state(ctx) == 0;
        for (int i = 0; i < m_y.size(); ++i) {
            if (off) {
                ctx.drive(m_y[i], released());
                continue;
            }
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
    Control m_g, m_oe;
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
          m_clr(control(p, QStringLiteral("CLR"))), m_oe(control(p, QStringLiteral("OE"))),
          m_en(control(p, QStringLiteral("G"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool edge = m_clk >= 0 && m_edge(ctx, m_clk) && (!m_en.present() || m_en.active(ctx));
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
    Control m_clr, m_oe, m_en;
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


// ------------------------------------------------------------ 74191 / 74190

// Synchronous up/down counter with single clock: ~CTEN enables, D/~U selects
// down (1) or up (0), ~LOAD loads asynchronously.
class UpDownSingleClock : public Model {
public:
    UpDownSingleClock(const Pins& p, int modulus)
        : m_mod(modulus), m_clk(p.at(QStringLiteral("CLK"))), m_cten(control(p, QStringLiteral("CTEN"))),
          m_load(control(p, QStringLiteral("LOAD"))), m_du(p.at(QStringLiteral("D/~U"))),
          m_d(p.run(QStringLiteral("D"))), m_q(p.run(QStringLiteral("Q"))),
          m_maxmin(p.at(QStringLiteral("MAX/MIN"))), m_rco(p.at(QStringLiteral("~RCO"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool edge = m_edge(ctx, m_clk);
        const bool down = ctx.input(m_du) == Logic::L1;
        if (m_load.active(ctx)) {
            m_count = readBus(ctx, m_d).value_or(m_count) % 16;
        } else if (edge && (!m_cten.present() || m_cten.active(ctx))) {
            m_count = down ? (m_count + m_mod - 1) % m_mod : (m_count + 1) % m_mod;
        }
        const bool terminal = down ? m_count == 0 : m_count == m_mod - 1;
        driveBus(ctx, m_q, m_count);
        driveBit(ctx, m_maxmin, fromBool(terminal));
        const bool ripple = terminal && ctx.input(m_clk) == Logic::L0 && (!m_cten.present() || m_cten.active(ctx));
        driveBit(ctx, m_rco, fromBool(!ripple));
    }
    int state() const override { return m_count; }

private:
    int m_mod, m_clk;
    Control m_cten, m_load;
    int m_du;
    QVector<int> m_d, m_q;
    int m_maxmin, m_rco;
    Edge m_edge;
    int m_count = 0;
};

// ------------------------------------------------------------ 74165

// Parallel-in serial-out: ~LD low loads D0..D7 asynchronously; otherwise each
// rising CLK (while INH is low) shifts SER into stage 0; QH is stage 7.
class ShiftPiso : public Model {
public:
    explicit ShiftPiso(const Pins& p)
        : m_d(p.run(QStringLiteral("D"))), m_clk(p.at(QStringLiteral("CLK"))), m_inh(p.at(QStringLiteral("INH"))),
          m_ld(control(p, QStringLiteral("LD"))), m_ser(p.at(QStringLiteral("SER"))),
          m_qh(p.at(QStringLiteral("QH"))), m_qhn(p.at(QStringLiteral("~QH"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool edge = m_edge(ctx, m_clk);
        if (m_ld.active(ctx)) {
            m_v = readBus(ctx, m_d).value_or(m_v);
        } else if (edge && (m_inh < 0 || ctx.input(m_inh) == Logic::L0)) {
            m_v = ((m_v << 1) | int(ctx.input(m_ser) == Logic::L1)) & 0xff;
        }
        const Logic qh = fromBool(m_v >> 7 & 1);
        driveBit(ctx, m_qh, qh);
        driveBit(ctx, m_qhn, logicNot(qh));
    }
    int state() const override { return m_v; }

private:
    QVector<int> m_d;
    int m_clk, m_inh;
    Control m_ld;
    int m_ser, m_qh, m_qhn;
    Edge m_edge;
    int m_v = 0;
};

// ------------------------------------------------------------ 7495

// MODE low: shift on ~CLK1 (Q0 <- SER); MODE high: load D0..D3 on ~CLK2.
class Shift95 : public Model {
public:
    explicit Shift95(const Pins& p)
        : m_mode(p.at(QStringLiteral("MODE"))), m_clk1(p.at(QStringLiteral("~CLK1"))), m_clk2(p.at(QStringLiteral("~CLK2"))),
          m_ser(p.at(QStringLiteral("SER"))), m_d(p.run(QStringLiteral("D"))), m_q(p.run(QStringLiteral("Q")))
    {
        m_e1.falling = m_e2.falling = true;
    }
    void evaluate(ModelContext& ctx) override
    {
        const bool e1 = m_e1(ctx, m_clk1), e2 = m_e2(ctx, m_clk2);
        const bool load = ctx.input(m_mode) == Logic::L1;
        if (load && e2)
            m_v = readBus(ctx, m_d).value_or(m_v);
        else if (!load && e1)
            m_v = ((m_v << 1) | int(ctx.input(m_ser) == Logic::L1)) & 0xf;
        driveBus(ctx, m_q, m_v);
    }
    int state() const override { return m_v; }

private:
    int m_mode, m_clk1, m_clk2, m_ser;
    QVector<int> m_d, m_q;
    Edge m_e1, m_e2;
    int m_v = 0;
};

// ------------------------------------------------------------ 74155

// Dual 2-to-4 decoder with shared select A, B. Section 1 is enabled by
// 1C high and ~1G low, section 2 by ~2C low and ~2G low; outputs active low.
class Decoder155 : public Model {
public:
    explicit Decoder155(const Pins& p)
        : m_a(p.at(QStringLiteral("A"))), m_b(p.at(QStringLiteral("B"))),
          m_en1{control(p, QStringLiteral("1C")), control(p, QStringLiteral("1G"))},
          m_en2{control(p, QStringLiteral("2C")), control(p, QStringLiteral("2G"))},
          m_y1(p.run(QStringLiteral("~1Y"))), m_y2(p.run(QStringLiteral("~2Y"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const std::optional<int> sel = readBus(ctx, {m_a, m_b});
        section(ctx, m_en1, m_y1, sel);
        section(ctx, m_en2, m_y2, sel);
    }

private:
    static void section(ModelContext& ctx, const Control (&en)[2], const QVector<int>& out, std::optional<int> sel)
    {
        const int a = en[0].state(ctx), b = en[1].state(ctx);
        for (int i = 0; i < out.size(); ++i) {
            Logic v;
            if (a == 0 || b == 0) v = Logic::L1;
            else if (a < 0 || b < 0 || !sel) v = Logic::X;
            else v = fromBool(*sel != i);
            driveBit(ctx, out[i], v);
        }
    }
    int m_a, m_b;
    Control m_en1[2], m_en2[2];
    QVector<int> m_y1, m_y2;
};

// ------------------------------------------------------------ 74280

class Parity : public Model {
public:
    explicit Parity(const Pins& p)
        : m_in(p.run(QStringLiteral("I"))), m_even(p.at(QStringLiteral("EVEN"))), m_odd(p.at(QStringLiteral("ODD"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const std::optional<int> v = readBus(ctx, m_in);
        const Logic odd = v ? fromBool(qPopulationCount(quint32(*v)) & 1) : Logic::X;
        driveBit(ctx, m_odd, odd);
        driveBit(ctx, m_even, logicNot(odd));
    }

private:
    QVector<int> m_in;
    int m_even, m_odd;
};

// ------------------------------------------------------------ 74245

// Octal bus transceiver: DIR high drives B from A, low drives A from B;
// ~G high isolates both sides.
class Transceiver : public Model {
public:
    explicit Transceiver(const Pins& p)
        : m_a(p.run(QStringLiteral("A"))), m_b(p.run(QStringLiteral("B"))), m_dir(p.at(QStringLiteral("DIR"))),
          m_g(control(p, QStringLiteral("G"))) {}
    void evaluate(ModelContext& ctx) override
    {
        const int en = m_g.present() ? m_g.state(ctx) : 1;
        const Logic dir = ctx.input(m_dir);
        for (int i = 0; i < std::min(m_a.size(), m_b.size()); ++i) {
            if (en == 0 || !isKnown(dir)) {
                ctx.drive(m_a[i], released());
                ctx.drive(m_b[i], released());
                continue;
            }
            const bool aToB = dir == Logic::L1;
            const int from = aToB ? m_a[i] : m_b[i], to = aToB ? m_b[i] : m_a[i];
            ctx.drive(from, released());
            Logic v = en < 0 ? Logic::X : ctx.input(from);
            if (v == Logic::Z) v = Logic::X;
            ctx.drive(to, strong(v));
        }
    }

private:
    QVector<int> m_a, m_b;
    int m_dir;
    Control m_g;
};

// ------------------------------------------------------------ memories

// Parses "00 1F a3 ..." (hex bytes separated by spaces or commas).
QVector<int> parseBytes(const QString& text)
{
    QVector<int> out;
    for (const QString& t : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts))
        out << (t.toInt(nullptr, 16) & 0xff);
    return out;
}

// Data Sequencer (word generator): outputs data[address] on Q0..Q7, from
// start to stop, then again from start. The address advances every `tick`
// ticks, or with the external clock on CP1 rising and CP2 falling edges.
class DataSequencer : public Model {
public:
    DataSequencer(const Pins& p, QVector<int> data, int lines, int start, int stop, int tick, bool external)
        : m_q(p.run(QStringLiteral("Q"))), m_cp1(p.at(QStringLiteral("CP1"))), m_cp2(p.at(QStringLiteral("CP2"))),
          m_data(std::move(data)), m_tick(std::max(1, tick)), m_external(external)
    {
        m_data.resize(std::max<qsizetype>({1, lines, m_data.size()}));
        m_start = std::clamp(start, 0, int(m_data.size()) - 1);
        m_stop = std::clamp(stop, m_start, int(m_data.size()) - 1);
        m_e2.falling = true;
    }
    void init(ModelContext& ctx) override
    {
        m_addr = m_start;
        output(ctx, 0);
        if (!m_external)
            ctx.wakeAfter(m_tick);
    }
    void evaluate(ModelContext& ctx) override
    {
        if (!m_external)
            return;
        const bool rise = m_cp1 >= 0 && m_e1(ctx, m_cp1);
        const bool fall = m_cp2 >= 0 && m_e2(ctx, m_cp2);
        if (rise || fall) {
            advance();
            output(ctx, 1);
        }
    }
    void wake(ModelContext& ctx) override
    {
        advance();
        output(ctx, 0);
        ctx.wakeAfter(m_tick);
    }
    int state() const override { return m_addr; }

private:
    void advance() { m_addr = m_addr >= m_stop ? m_start : m_addr + 1; }
    void output(ModelContext& ctx, std::uint32_t delay)
    {
        const int word = m_data.value(m_addr);
        for (int i = 0; i < m_q.size(); ++i)
            if (m_q[i] >= 0) ctx.drive(m_q[i], strong(fromBool(word >> i & 1)), delay);
    }

    QVector<int> m_q;
    int m_cp1, m_cp2;
    QVector<int> m_data;
    int m_start = 0, m_stop = 0, m_tick, m_addr = 0;
    bool m_external;
    Edge m_e1, m_e2;
};

// ROM (PROM32) and static RAM (1K RAM): address A0.., data D0..D7, ~CS;
// RAM also has ~WE (low writes, high reads). Outputs float when deselected.
class Memory : public Model {
public:
    Memory(const Pins& p, bool writable, const QVector<int>& contents)
        : m_addr(p.run(QStringLiteral("A"))), m_data(p.run(QStringLiteral("D"))), m_cs(control(p, QStringLiteral("CS"))),
          m_we(control(p, QStringLiteral("WE"))), m_writable(writable)
    {
        m_mem.resize(1 << m_addr.size());
        for (int i = 0; i < std::min(contents.size(), m_mem.size()); ++i)
            m_mem[i] = contents[i];
    }
    void evaluate(ModelContext& ctx) override
    {
        const int cs = m_cs.present() ? m_cs.state(ctx) : 1;
        const bool write = m_writable && m_we.active(ctx);
        const std::optional<int> addr = readBus(ctx, m_addr);
        if (cs == 0 || write) {
            for (int pin : std::as_const(m_data))
                if (pin >= 0) ctx.drive(pin, released());
            if (cs == 1 && write && addr) {
                if (const std::optional<int> v = readBus(ctx, m_data))
                    m_mem[*addr] = *v;
            }
            return;
        }
        driveBus(ctx, m_data, cs == 1 && addr ? std::optional<int>(m_mem[*addr]) : std::nullopt);
    }

private:
    QVector<int> m_addr, m_data;
    Control m_cs, m_we;
    bool m_writable;
    QVector<int> m_mem;
};

// ------------------------------------------------------------ 4x4 keypad

// Key matrix: while key k is held, row k/4 connects to column k%4.
class KeyMatrix : public Model {
public:
    explicit KeyMatrix(const Pins& p) : m_rows(p.run(QStringLiteral("R"))), m_cols(p.run(QStringLiteral("C"))) {}
    void evaluate(ModelContext&) override {}
    void interact(ModelContext& ctx, const QString& action, int value) override
    {
        const int before = m_key;
        if (action == u"key") m_key = value;
        else if (action == u"release") m_key = -1;
        if (m_key != before) ctx.linksChanged();
    }
    QVector<LinkPath> links() const override
    {
        if (m_key < 0) return {};
        return {{m_rows.value(m_key / 4, -1), m_cols.value(m_key % 4, -1)}};
    }
    int state() const override { return m_key; }

private:
    QVector<int> m_rows, m_cols;
    int m_key = -1;
};

} // namespace

namespace {

// Two independent halves of a dual chip (74139, 74153). Pins "1X"/"2X"
// belong to one half and are seen by its model as "X"; other pins (shared
// selects) belong to both.
class DualModel : public Model {
public:
    explicit DualModel(std::vector<std::unique_ptr<Model>> halves) : m_halves(std::move(halves)) {}
    void init(ModelContext& ctx) override { for (auto& h : m_halves) h->init(ctx); }
    void evaluate(ModelContext& ctx) override { for (auto& h : m_halves) h->evaluate(ctx); }
    void wake(ModelContext& ctx) override { for (auto& h : m_halves) h->wake(ctx); }

private:
    std::vector<std::unique_ptr<Model>> m_halves;
};

SymbolDef halfOf(const SymbolDef& s, QChar half)
{
    SymbolDef h = s;
    h.model = QJsonObject{{QStringLiteral("kind"), s.model.value(u"inner")}};
    for (SymbolPin& pin : h.pins) {
        const bool low = pin.name.startsWith(u'~');
        const QString bare = pin.name.mid(low ? 1 : 0);
        if (bare.size() > 1 && (bare[0] == u'1' || bare[0] == u'2'))
            pin.name = bare[0] == half ? (low ? QStringLiteral("~") : QString()) + bare.mid(1) : QString();
    }
    return h;
}

} // namespace

std::unique_ptr<Model> createChipModel(const SymbolDef& s, const Component& c, QString* error)
{
    const Pins p(s);
    const QString kind = s.model.value(u"kind").toString();
    if (kind == u"dual") {
        std::vector<std::unique_ptr<Model>> halves;
        for (QChar half : {u'1', u'2'}) {
            const SymbolDef h = halfOf(s, half);
            std::unique_ptr<Model> m = createModel(h, c, error);
            if (!m) return nullptr;
            halves.push_back(std::move(m));
        }
        return std::make_unique<DualModel>(std::move(halves));
    }
    if (kind == u"ripple")
        return std::make_unique<RippleCounter>(p, s.model.value(u"modB").toInt(8), s.model.value(u"seq92").toBool());
    if (kind == u"sync") return std::make_unique<SyncCounter>(p, s.model.value(u"syncClear").toBool());
    if (kind == u"updown") return std::make_unique<UpDownCounter>(p, s.model.value(u"modulus").toInt(16));
    if (kind == u"decoder") return std::make_unique<Decoder>(p);
    if (kind == u"bcd7seg") return std::make_unique<SevenSegDecoder>(p);
    if (kind == u"seg7") return std::make_unique<SevenSegDisplay>(p, s.model.value(u"anode").toBool());
    if (kind == u"mux") return std::make_unique<Mux>(p, s.model.value(u"tristate").toBool());
    if (kind == u"mux2") return std::make_unique<QuadMux2>(p);
    if (kind == u"adder") return std::make_unique<Adder>(p);
    if (kind == u"comparator") return std::make_unique<Comparator>(p);
    if (kind == u"register") return std::make_unique<Register>(p);
    if (kind == u"shift") return std::make_unique<ShiftRegister>(p);
    if (kind == u"tribus") return std::make_unique<TriBus>(p, s.model.value(u"groups").toArray());
    if (kind == u"updown1") return std::make_unique<UpDownSingleClock>(p, s.model.value(u"modulus").toInt(16));
    if (kind == u"piso") return std::make_unique<ShiftPiso>(p);
    if (kind == u"shift95") return std::make_unique<Shift95>(p);
    if (kind == u"decoder155") return std::make_unique<Decoder155>(p);
    if (kind == u"parity") return std::make_unique<Parity>(p);
    if (kind == u"transceiver") return std::make_unique<Transceiver>(p);
    if (kind == u"keymatrix") return std::make_unique<KeyMatrix>(p);
    QVariantMap props = s.defaultProps;
    for (auto it = c.props.cbegin(); it != c.props.cend(); ++it)
        props.insert(it.key(), it.value());
    if (kind == u"dataseq")
        return std::make_unique<DataSequencer>(p, parseBytes(props.value(QStringLiteral("data")).toString()),
                                               props.value(QStringLiteral("lines"), 32).toInt(),
                                               props.value(QStringLiteral("start")).toInt(),
                                               props.value(QStringLiteral("stop"), 31).toInt(),
                                               props.value(QStringLiteral("tick"), 1).toInt(),
                                               props.value(QStringLiteral("external")).toInt() != 0);
    if (kind == u"rom" || kind == u"ram") {
        return std::make_unique<Memory>(p, kind == u"ram", parseBytes(props.value(QStringLiteral("data")).toString()));
    }
    if (error) *error = QStringLiteral("unknown model kind '%1'").arg(kind);
    return nullptr;
}

} // namespace cm::sim
