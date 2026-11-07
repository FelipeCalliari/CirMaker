#include "sim/digital/Models.h"

#include "core/Circuit.h"
#include "library/SymbolDef.h"

#include <QJsonObject>
#include <QSet>

#include <cmath>

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
    GateModel(Op op, bool invert, QVector<int> inputs, int output, int delay)
        : m_op(op), m_invert(invert), m_inputs(std::move(inputs)), m_out(output), m_delay(delay) {}

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
        ctx.drive(m_out, strong(m_invert ? logicNot(r) : r), m_delay);
        m_state = r == Logic::X ? -1 : int((r == Logic::L1) != m_invert);
    }
    int state() const override { return m_state; }

private:
    Op m_op;
    bool m_invert;
    QVector<int> m_inputs;
    int m_out;
    int m_delay;
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
    FlipFlopModel(bool jk, Pins p, int delay) : m_jk(jk), m_p(p), m_delay(delay) {}

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
        const std::uint32_t delay = ctx.settling() ? 0 : std::uint32_t(m_delay);
        if (m_p.q >= 0) ctx.drive(m_p.q, strong(m_q), delay);
        if (m_p.qn >= 0) ctx.drive(m_p.qn, strong(logicNot(m_q)), delay);
    }

    bool m_jk;
    Pins m_p;
    int m_delay;
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
    // CircuitMaker's Pulser: free running, or (trigger) a one-shot fired by a
    // rise on CP1 or a fall on CP2, active for the high time from the next
    // tick and started again by each new trigger; inverted swaps the outputs
    PulserModel(int q, int qn, int cp1, int cp2, int high, int low, bool inverted, bool trigger)
        : m_q(q), m_qn(qn), m_cp1(cp1), m_cp2(cp2), m_high(std::max(1, high)), m_low(std::max(1, low)),
          m_inverted(inverted), m_trigger(trigger) {}
    void init(ModelContext& ctx) override
    {
        m_level = false;
        output(ctx);
        if (!m_trigger) ctx.wakeAfter(m_low);
    }
    void evaluate(ModelContext& ctx) override
    {
        if (!m_trigger) return;
        const Logic a = m_cp1 >= 0 ? ctx.input(m_cp1) : Logic::L0, b = m_cp2 >= 0 ? ctx.input(m_cp2) : Logic::L1;
        const bool fired = (m_lastA == Logic::L0 && a == Logic::L1) || (m_lastB == Logic::L1 && b == Logic::L0);
        m_lastA = a;
        m_lastB = b;
        if (!fired || ctx.settling()) return;
        // active on the next tick, for the high time from this trigger
        m_until = ctx.now() + 1 + m_high;
        ctx.wakeAfter(1);
    }
    void wake(ModelContext& ctx) override
    {
        if (m_trigger) {
            const bool active = ctx.now() < m_until;
            if (active != m_level) {
                m_level = active;
                output(ctx);
            }
            if (active) ctx.wakeAfter(std::uint32_t(m_until - ctx.now()));
            return;
        }
        m_level = !m_level;
        output(ctx);
        ctx.wakeAfter(m_level ? m_high : m_low);
    }
    int state() const override { return m_level; }

private:
    void output(ModelContext& ctx)
    {
        const bool q = m_level != m_inverted;
        if (m_q >= 0) ctx.drive(m_q, strong(fromBool(q)), 0);
        if (m_qn >= 0) ctx.drive(m_qn, strong(fromBool(!q)), 0);
    }
    int m_q, m_qn, m_cp1, m_cp2, m_high, m_low;
    bool m_inverted, m_trigger;
    bool m_level = false;
    Logic m_lastA = Logic::X, m_lastB = Logic::X;       // nothing seen yet: no edge
    std::uint64_t m_until = 0;
};

// CircuitMaker's animated devices. A car drives along its track once S is
// high and raises F at the finish; Reset brings it back. Position 0..100.
class CarModel : public Model {
public:
    CarModel(int s, int f, int reset, int speed) : m_s(s), m_f(f), m_reset(reset), m_step(std::max(1, 24 / std::max(1, speed))) {}
    void evaluate(ModelContext& ctx) override
    {
        if (m_reset >= 0 && ctx.input(m_reset) == Logic::L1) {
            m_pos = 0;
            m_moving = false;
        } else if (!m_moving && m_pos == 0 && ctx.input(m_s) == Logic::L1 && !ctx.settling()) {
            m_moving = true;
            ctx.wakeAfter(m_step);
        }
        if (m_f >= 0) ctx.drive(m_f, strong(fromBool(m_pos >= 100)), 0);
    }
    void wake(ModelContext& ctx) override
    {
        if (!m_moving) return;
        if (++m_pos >= 100) {
            m_moving = false;
            evaluate(ctx);
        } else {
            ctx.wakeAfter(m_step);
        }
    }
    // the car's Reset button: back to the start
    void interact(ModelContext& ctx, const QString& action, int) override
    {
        if (action != u"reset") return;
        m_pos = 0;
        m_moving = false;
        evaluate(ctx);
    }
    int state() const override { return m_pos; }

private:
    int m_s, m_f, m_reset, m_step;
    int m_pos = 0;
    bool m_moving = false;
};

// A rocket lifts off when + is driven high against -, climbs out of sight
// (height 0..100) and is back on its pad once the input drops after the flight.
class RocketModel : public Model {
public:
    RocketModel(int plus, int minus) : m_plus(plus), m_minus(minus) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool lit = ctx.input(m_plus) == Logic::L1 && (m_minus < 0 || ctx.input(m_minus) != Logic::L1);
        if (lit && m_height == 0 && !ctx.settling()) {
            m_height = 1;
            ctx.wakeAfter(kStep);
        } else if (!lit && m_height >= 100) {
            m_height = 0;
        }
    }
    void wake(ModelContext& ctx) override
    {
        if (m_height > 0 && m_height < 100) {
            ++m_height;
            if (m_height < 100) ctx.wakeAfter(kStep);
            else evaluate(ctx);
        }
    }
    // its Reset button: back on the pad
    void interact(ModelContext&, const QString& action, int) override
    {
        if (action == u"reset") m_height = 0;
    }
    int state() const override { return m_height; }

private:
    static constexpr std::uint32_t kStep = 10;
    int m_plus, m_minus;
    int m_height = 0;
};

} // namespace

// The rotor angle of a unipolar stepper from its energized coils (1 = 0,
// 3 = 90, 2 = 180, 4 = 270 degrees), followed through full and half steps:
// the angle in half steps (45 degrees) since the start.
int stepperAdvance(int halfSteps, const bool energized[4])
{
    static const double angle[4] = {0, 180, 90, 270};          // coils 1, 2, 3, 4
    double x = 0, y = 0;
    for (int k = 0; k < 4; ++k)
        if (energized[k]) {
            x += std::cos(angle[k] * M_PI / 180);
            y += std::sin(angle[k] * M_PI / 180);
        }
    if (std::hypot(x, y) < 0.5) return halfSteps;                // nothing, or opposite coils
    const int target = int(std::lround(std::atan2(y, x) * 180 / M_PI / 45));
    int delta = ((target - halfSteps) % 8 + 8) % 8;              // the shorter way round
    if (delta > 4) delta -= 8;
    if (delta == 4) delta = 0;                                   // half a turn: undecided, stays
    return halfSteps + delta;
}

namespace {

// Stepper motor: a coil is energized while its end and its common differ.
class StepperModel : public Model {
public:
    StepperModel(QVector<int> coils, int comA, int comB) : m_coils(std::move(coils)), m_comA(comA), m_comB(comB) {}
    void evaluate(ModelContext& ctx) override
    {
        bool on[4] = {false, false, false, false};
        for (int k = 0; k < 4; ++k) {
            const Logic end = ctx.input(m_coils[k]), com = ctx.input(k < 2 ? m_comA : m_comB);
            on[k] = isKnown(end) && isKnown(com) && end != com;
        }
        m_halfSteps = stepperAdvance(m_halfSteps, on);
    }
    int state() const override { return m_halfSteps; }

private:
    QVector<int> m_coils;
    int m_comA, m_comB;
    int m_halfSteps = 0;
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

// Relay: the coil is energized while C1 and C2 sit at different levels;
// then COM connects to NO, otherwise to NC.
class RelayModel : public Model {
public:
    RelayModel(int c1, int c2, int com, int nc, int no) : m_c1(c1), m_c2(c2), m_com(com), m_nc(nc), m_no(no) {}
    void evaluate(ModelContext& ctx) override
    {
        const Logic a = ctx.input(m_c1), b = ctx.input(m_c2);
        const bool on = (a == Logic::L1 && b == Logic::L0) || (a == Logic::L0 && b == Logic::L1);
        if (on != m_on) {
            m_on = on;
            ctx.linksChanged();
        }
    }
    QVector<LinkPath> links() const override { return {{m_com, m_on ? m_no : m_nc}}; }
    int state() const override { return m_on; }

private:
    int m_c1, m_c2, m_com, m_nc, m_no;
    bool m_on = false;
};

// Two-pin loads CircuitMaker animates in Digital Mode (lamp, buzzer, motor,
// speaker, relay coil, solenoid): energized while their pins differ. A coil
// also operates the contacts with its relay name.
class LoadModel : public Model {
public:
    LoadModel(int a, int b, QString relay) : m_a(a), m_b(b), m_relay(std::move(relay)) {}
    void evaluate(ModelContext& ctx) override
    {
        const Logic x = ctx.input(m_a), y = ctx.input(m_b);
        const bool on = isKnown(x) && isKnown(y) && x != y;
        if (on != m_on) {
            m_on = on;
            if (!m_relay.isEmpty()) ctx.linksChanged();
        }
    }
    int state() const override { return m_on; }
    QString relayCoil() const override { return m_relay; }

private:
    int m_a, m_b;
    QString m_relay;
    bool m_on = false;
};

// A relay contact drawn on its own: C to the other pin while its coil is
// energized (normally open) or while it is not (normally closed).
class ContactModel : public Model {
public:
    ContactModel(int c, int other, bool closedAtRest, QString relay)
        : m_c(c), m_other(other), m_closedAtRest(closedAtRest), m_relay(std::move(relay)) {}
    void evaluate(ModelContext&) override {}
    QVector<LinkPath> links() const override
    {
        const bool energized = m_coil && m_coil->state() == 1;
        if (energized == m_closedAtRest) return {};
        return {{m_c, m_other}};
    }
    int state() const override { return m_coil && m_coil->state() == 1; }
    QString relayContact() const override { return m_relay; }
    void attachCoil(const Model* coil) override { m_coil = coil; }

private:
    int m_c, m_other;
    bool m_closedAtRest;
    QString m_relay;
    const Model* m_coil = nullptr;
};

// CircuitMaker's Ascii Display: a terminal of 4 lines of 16 characters.
// When CP falls it writes the character on D6..D0 at the cursor: CR (or
// LF) starts the next line, BS steps back and erases, other control codes
// are ignored; past the last line the text scrolls up.
class AsciiTermModel : public Model {
public:
    static constexpr int kCols = 16, kRows = 4;
    AsciiTermModel(QVector<int> bits, int cp, const QString& text, int cursor)
        : m_bits(std::move(bits)), m_cp(cp), m_cursor(std::clamp(cursor, 0, kCols * kRows - 1))
    {
        m_screen.fill(' ', kCols * kRows);
        for (int i = 0; i < std::min<qsizetype>(text.size(), kCols * kRows); ++i)
            m_screen[i] = text.at(i).unicode() >= 32 && text.at(i).unicode() < 127 ? text.at(i).unicode() : ' ';
    }
    void evaluate(ModelContext& ctx) override
    {
        const Logic cp = ctx.input(m_cp);
        if (!ctx.settling() && m_last == Logic::L1 && cp == Logic::L0) {
            int code = 0;
            for (int i = 0; i < m_bits.size(); ++i) {
                const Logic b = ctx.input(m_bits[i]);
                if (!isKnown(b)) return void(m_last = cp);
                code |= int(b == Logic::L1) << i;
            }
            write(code);
        }
        m_last = cp;
    }
    int state() const override { return m_cursor; }
    QVector<int> memory() const override { return m_screen + QVector<int>{m_cursor}; }
    QVariantMap persistentProps() const override
    {
        QString text;
        for (int c : m_screen) text += QChar(c);
        return {{QStringLiteral("text"), text}, {QStringLiteral("cursor"), m_cursor}};
    }

private:
    void newLine()
    {
        const int row = m_cursor / kCols;
        if (row + 1 < kRows) {
            m_cursor = (row + 1) * kCols;
            return;
        }
        m_screen.remove(0, kCols);
        m_screen.append(QVector<int>(kCols, ' '));
        m_cursor = (kRows - 1) * kCols;
    }
    void write(int code)
    {
        if (code == 13 || code == 10) {
            newLine();
        } else if (code == 8) {
            if (m_cursor % kCols > 0) m_screen[--m_cursor] = ' ';
        } else if (code >= 32 && code < 127) {
            m_screen[m_cursor] = code;
            if ((m_cursor + 1) % kCols == 0) newLine();
            else ++m_cursor;
        }
    }
    QVector<int> m_bits;
    int m_cp;
    QVector<int> m_screen;
    int m_cursor;
    Logic m_last = Logic::X;
};

// Opto isolator: its transistor (COL-E) conducts while the LED is lit,
// A high against C.
class OptoModel : public Model {
public:
    OptoModel(int a, int k, int col, int e) : m_a(a), m_k(k), m_col(col), m_e(e) {}
    void evaluate(ModelContext& ctx) override
    {
        const bool on = ctx.input(m_a) == Logic::L1 && ctx.input(m_k) == Logic::L0;
        if (on != m_on) {
            m_on = on;
            ctx.linksChanged();
        }
    }
    QVector<LinkPath> links() const override
    {
        if (!m_on) return {};
        return {{m_col, m_e}};
    }
    int state() const override { return m_on; }

private:
    int m_a, m_k, m_col, m_e;
    bool m_on = false;
};

} // namespace

std::unique_ptr<Model> createModel(const SymbolDef& s, const Component& c, QString* error)
{
    const QString kind = s.model.value(u"kind").toString();
    // analog-only parts have no digital behaviour
    if (kind.isEmpty() || kind == u"siggen" || kind == u"pot" || kind == u"meter" || kind == u"scope"
        || kind == u"macro")   // macros run as their flattened contents
        return nullptr;
    const QVariantMap props = mergedProps(s, c);
    // CircuitMaker propagation delay: 1..14 ticks (Edit > Set Prop Delays)
    const int delay = std::clamp(props.value(QStringLiteral("delay"), 1).toInt(), 1, 14);
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
        return std::make_unique<GateModel>(op, s.model.value(u"invert").toBool(), inputs, out, delay);
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
        return std::make_unique<FlipFlopModel>(jk, p, delay);
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
    if (kind == u"car") {
        const int start = s.pinIndex(QStringLiteral("S"));
        if (start < 0) return fail(QStringLiteral("car needs S"));
        return std::make_unique<CarModel>(start, s.pinIndex(QStringLiteral("F")), s.pinIndex(QStringLiteral("RESET")),
                                          props.value(QStringLiteral("speed"), 3).toInt());
    }
    if (kind == u"rocket") {
        const int plus = s.pinIndex(QStringLiteral("+"));
        if (plus < 0) return fail(QStringLiteral("rocket needs +"));
        return std::make_unique<RocketModel>(plus, s.pinIndex(QStringLiteral("-")));
    }
    if (kind == u"stepper") {
        QVector<int> coils;
        for (const char* n : {"1", "2", "3", "4"}) coils << s.pinIndex(QString::fromLatin1(n));
        const int a = s.pinIndex(QStringLiteral("COMA")), b = s.pinIndex(QStringLiteral("COMB"));
        if (coils.contains(-1) || a < 0 || b < 0) return fail(QStringLiteral("stepper needs 1-4, COMA and COMB"));
        return std::make_unique<StepperModel>(coils, a, b);
    }
    if (kind == u"pulser") {
        const int q = s.pinIndex(QStringLiteral("Q"));
        if (q < 0)
            return fail(QStringLiteral("pulser needs a Q output"));
        return std::make_unique<PulserModel>(q, s.pinIndex(QStringLiteral("~Q")), s.pinIndex(QStringLiteral("CP1")),
                                             s.pinIndex(QStringLiteral("CP2")),
                                             props.value(QStringLiteral("high"), 50).toInt(),
                                             props.value(QStringLiteral("low"), 50).toInt(),
                                             props.value(QStringLiteral("inverted")).toBool(),
                                             props.value(QStringLiteral("trigger")).toBool());
    }
    if (kind == u"key") {
        const int bits = s.model.value(u"bits").toInt(4);
        return std::make_unique<KeyModel>(bitPins(s, bits), s.pinIndex(QStringLiteral("STB")),
                                          props.value(QStringLiteral("value")).toInt());
    }
    if (kind == u"asciiterm") {
        const QVector<int> bits = bitPins(s, 7);
        const int cp = s.pinIndex(QStringLiteral("CP"));
        if (bits.contains(-1) || cp < 0) return fail(QStringLiteral("ascii display needs D0..D6 and CP"));
        return std::make_unique<AsciiTermModel>(bits, cp, props.value(QStringLiteral("text")).toString(),
                                                props.value(QStringLiteral("cursor")).toInt());
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
    if (kind == u"load") {
        if (s.pins.size() < 2) return fail(QStringLiteral("load needs two pins"));
        const QString relay = s.defaultProps.contains(QStringLiteral("relay")) ? props.value(QStringLiteral("relay")).toString().trimmed() : QString();
        return std::make_unique<LoadModel>(0, 1, relay);
    }
    if (kind == u"contact") {
        const bool nc = s.model.value(u"closed").toBool();
        const int com = s.pinIndex(QStringLiteral("C")), other = s.pinIndex(nc ? QStringLiteral("NC") : QStringLiteral("NO"));
        if (com < 0 || other < 0) return fail(QStringLiteral("contact needs C and NO or NC"));
        return std::make_unique<ContactModel>(com, other, nc, props.value(QStringLiteral("relay")).toString().trimmed());
    }
    if (kind == u"opto") {
        const int a = s.pinIndex(QStringLiteral("A")), k = s.pinIndex(QStringLiteral("C")),
                  col = s.pinIndex(QStringLiteral("COL")), e = s.pinIndex(QStringLiteral("E"));
        if (std::min({a, k, col, e}) < 0)
            return fail(QStringLiteral("opto isolator needs A, C, COL and E"));
        return std::make_unique<OptoModel>(a, k, col, e);
    }
    if (kind == u"relay") {
        const int c1 = s.pinIndex(QStringLiteral("C1")), c2 = s.pinIndex(QStringLiteral("C2")),
                  com = s.pinIndex(QStringLiteral("COM")), nc = s.pinIndex(QStringLiteral("NC")),
                  no = s.pinIndex(QStringLiteral("NO"));
        if (std::min({c1, c2, com, nc, no}) < 0)
            return fail(QStringLiteral("relay needs C1, C2, COM, NC and NO"));
        return std::make_unique<RelayModel>(c1, c2, com, nc, no);
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
            return std::make_unique<LinkModel>(LinkModel::Kind::ChangeOver, com, nc, no,
                                               props.value(QStringLiteral("closed")).toInt() != 0);
        }
        const LinkModel::Kind k = type == u"no" ? LinkModel::Kind::NormallyOpen
                                : type == u"nc" ? LinkModel::Kind::NormallyClosed
                                : type == u"resistor" ? LinkModel::Kind::Resistor
                                : LinkModel::Kind::Toggle;
        return std::make_unique<LinkModel>(k, 0, 1, -1, props.value(QStringLiteral("closed")).toInt() != 0);
    }
    QString chipError;
    std::unique_ptr<Model> chip = createChipModel(s, c, &chipError);
    if (chip && hasPropagationDelay(kind))
        chip->extraDelay = std::uint32_t(delay - 1);
    return chip ? std::move(chip) : fail(chipError);
}

bool hasPropagationDelay(const QString& kind)
{
    static const QSet<QString> with{
        QStringLiteral("gate"), QStringLiteral("dff"), QStringLiteral("jkff"), QStringLiteral("tribuf"),
        QStringLiteral("adder"), QStringLiteral("alu181"), QStringLiteral("bcd7seg"), QStringLiteral("comparator"),
        QStringLiteral("decoder"), QStringLiteral("decoder155"), QStringLiteral("dual"), QStringLiteral("johnson"),
        QStringLiteral("latch75"), QStringLiteral("mux"), QStringLiteral("mux2"), QStringLiteral("parity"),
        QStringLiteral("piso"), QStringLiteral("ram"), QStringLiteral("rom"), QStringLiteral("reg173"),
        QStringLiteral("register"), QStringLiteral("ripple"), QStringLiteral("shift"), QStringLiteral("shift95"),
        QStringLiteral("sync"), QStringLiteral("transceiver"), QStringLiteral("tribus"), QStringLiteral("updown"),
        QStringLiteral("updown1"), QStringLiteral("updown169")};
    return with.contains(kind);
}

} // namespace cm::sim
