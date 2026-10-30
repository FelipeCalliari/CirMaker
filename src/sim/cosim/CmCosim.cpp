// Co-simulation shim for ngspice's d_cosim XSPICE code model: runs one of our
// digital chip models (the same C++ the digital simulator uses) inside an
// analog/mixed SPICE simulation. The deck names the symbol in sim_args:
//   .model chip d_cosim simulation="libcmcosim.so" sim_args=["74161"]
//   Au1 [inputs...] [outputs...] chip
// Ports follow the symbol's pin order: inputs, then outputs.

#include "core/Circuit.h"
#include "library/LibraryRegistry.h"
#include "sim/digital/Models.h"

#include <QFile>

#include <cmath>
#include <map>
#include <memory>
#include <vector>

// --- the interface of ngspice's src/include/ngspice/cosim.h and cmtypes.h

extern "C" {
typedef enum { ZERO, ONE, UNKNOWN } Digital_State_t;
typedef enum { STRONG, RESISTIVE, HI_IMPEDANCE, UNDETERMINED } Digital_Strength_t;
typedef struct {
    Digital_State_t state;
    Digital_Strength_t strength;
} Digital_t;
typedef enum { Normal, After_input, Both } Cosim_method;

struct co_info {
    unsigned int in_count;
    unsigned int out_count;
    unsigned int inout_count;
    void (*cleanup)(struct co_info*);
    void (*step)(struct co_info* pinfo);
    void (*in_fn)(struct co_info*, unsigned int, Digital_t*);
    void (*out_fn)(struct co_info*, unsigned int, Digital_t*);
    void* handle;
    volatile double vtime;
    Cosim_method method;
    unsigned int lib_argc;
    unsigned int sim_argc;
    const char* const* const lib_argv;
    const char* const* const sim_argv;
    void* (*dlopen_fn)(const char* fn);
};
}

static void initResources() { Q_INIT_RESOURCE(symbols); }

namespace {

using namespace cm;
using namespace cm::sim;

constexpr double kTick = 1e-9;      // one digital tick: the chips' gate delays become nanoseconds

class Chip final : public ModelContext {
public:
    Chip(const SymbolDef& s, std::unique_ptr<Model> model) : m_model(std::move(model))
    {
        m_value.assign(s.pins.size(), Logic::X);
        m_port.assign(s.pins.size(), -1);
        for (int i = 0; i < s.pins.size(); ++i)
            if (s.pins[i].dir != SymbolPin::Dir::Out) m_inputs << i;
        for (int i = 0; i < s.pins.size(); ++i)
            if (s.pins[i].dir == SymbolPin::Dir::Out) {
                m_port[i] = int(m_outputs.size());
                m_outputs << i;
            }
        m_reported.assign(m_outputs.size(), Logic::Z);
        m_model->init(*this);
        m_settling = false;
        m_dirty = true;
    }

    int inputs() const { return int(m_inputs.size()); }
    int outputs() const { return int(m_outputs.size()); }

    void input(unsigned int port, Digital_t* v)
    {
        if (port >= unsigned(m_inputs.size())) return;
        const Logic l = v->strength == HI_IMPEDANCE ? Logic::Z
                      : v->state == ZERO ? Logic::L0 : v->state == ONE ? Logic::L1 : Logic::X;
        Logic& now = m_value[m_inputs[int(port)]];
        // the bridge reports "unknown" while an analog edge crosses between
        // its thresholds; keep the last level so 1 -> X -> 0 is still a falling edge
        if (l == Logic::X && isKnown(now))
            return;
        if (now != l) {
            now = l;
            m_dirty = true;
        }
    }

    // Runs queued events up to the target time; stops early, at the time
    // of the first output change, after reporting it.
    void step(co_info* info)
    {
        const std::uint64_t target = std::uint64_t(std::llround(info->vtime / kTick));
        if (m_dirty) {
            m_dirty = false;
            m_model->evaluate(*this);
        }
        while (!m_queue.empty() && m_queue.begin()->first <= target) {
            const std::uint64_t t = m_queue.begin()->first;
            m_now = t;
            std::vector<Event> due = std::move(m_queue.begin()->second);
            m_queue.erase(m_queue.begin());
            bool wake = false;
            for (const Event& e : due) {
                if (e.pin < 0) wake = true;
                else m_value[e.pin] = e.value;
            }
            if (wake) m_model->wake(*this);
            bool reported = false;
            for (int k = 0; k < m_outputs.size(); ++k) {
                const Logic v = m_value[m_outputs[k]];
                if (v == m_reported[k]) continue;
                m_reported[k] = v;
                Digital_t d{v == Logic::L0 ? ZERO : v == Logic::L1 ? ONE : UNKNOWN,
                            v == Logic::Z ? HI_IMPEDANCE : STRONG};
                info->out_fn(info, unsigned(k), &d);
                reported = true;
            }
            if (reported && t < target) {
                info->vtime = double(t) * kTick;
                return;
            }
        }
        m_now = target;
    }

    // ModelContext
    std::uint64_t now() const override { return m_now; }
    Logic input(int pin) const override { return pin >= 0 && pin < int(m_value.size()) ? m_value[pin] : Logic::X; }
    bool connected(int) const override { return true; }
    void drive(int pin, Drive d, std::uint32_t delay) override
    {
        if (pin < 0 || pin >= int(m_port.size()) || m_port[pin] < 0) return;
        const Logic v = d.strength == Strength::None ? Logic::Z : d.value;
        m_queue[m_now + std::max<std::uint32_t>(delay, 1)].push_back({pin, v});
    }
    void wakeAfter(std::uint32_t ticks) override { m_queue[m_now + std::max<std::uint32_t>(ticks, 1)].push_back({-1, Logic::X}); }
    void linksChanged() override {}
    bool settling() const override { return m_settling; }

private:
    struct Event { int pin; Logic value; };
    std::unique_ptr<Model> m_model;
    QVector<int> m_inputs, m_outputs;
    std::vector<Logic> m_value, m_reported;
    std::vector<int> m_port;
    std::map<std::uint64_t, std::vector<Event>> m_queue;
    std::uint64_t m_now = 0;
    bool m_settling = true, m_dirty = false;
};

void stepFn(co_info* info) { static_cast<Chip*>(info->handle)->step(info); }
void inFn(co_info* info, unsigned int port, Digital_t* v) { static_cast<Chip*>(info->handle)->input(port, v); }
void cleanupFn(co_info* info)
{
    delete static_cast<Chip*>(info->handle);
    info->handle = nullptr;
}

} // namespace

extern "C" Q_DECL_EXPORT void Cosim_setup(co_info* info)
{
    initResources();
    LibraryRegistry& lib = LibraryRegistry::instance();
    if (lib.all().isEmpty())
        lib.loadBuiltin();
    const QString id = info->sim_argc > 0 ? QString::fromUtf8(info->sim_argv[0]) : QString();
    // ngspice lower-cases the deck, sim_args included
    const SymbolDef* s = lib.find(id);
    for (const SymbolDef* candidate : lib.all())
        if (!s && candidate->id.compare(id, Qt::CaseInsensitive) == 0) s = candidate;
    Component c;
    c.symbolId = s ? s->id : id;
    QString error;
    std::unique_ptr<Model> model = s ? createModel(*s, c, &error) : nullptr;
    if (!model) {
        fprintf(stderr, "cmcosim: no digital model for '%s' %s\n", qPrintable(id), qPrintable(error));
        info->in_count = info->out_count = info->inout_count = 0;
        return;
    }
    auto* chip = new Chip(*s, std::move(model));
    info->handle = chip;
    info->in_count = unsigned(chip->inputs());
    info->out_count = unsigned(chip->outputs());
    info->inout_count = 0;
    info->step = stepFn;
    info->in_fn = inFn;
    info->cleanup = cleanupFn;
    info->method = Both;
}
