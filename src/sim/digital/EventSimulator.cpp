#include "sim/digital/EventSimulator.h"

#include "core/Circuit.h"
#include "library/LibraryRegistry.h"

#include <algorithm>

namespace cm::sim {

struct EventSimulator::Instance {
    ItemId id = 0;
    std::unique_ptr<Model> model;
    std::vector<int> pinNet;
    std::vector<Drive> out;
    std::vector<Logic> floatValue;
};

class EventSimulator::Context final : public ModelContext {
public:
    Context(EventSimulator& sim, int comp) : m_sim(sim), m_comp(comp) {}

    std::uint64_t now() const override { return m_sim.m_now; }
    Logic input(int pin) const override
    {
        const Instance& in = m_sim.m_inst[m_comp];
        if (pin < 0 || pin >= int(in.pinNet.size()) || m_sim.m_initializing)
            return Logic::X;
        const int net = in.pinNet[pin];
        const Logic v = net < 0 ? Logic::Z : m_sim.m_netValue[net];
        return v == Logic::Z ? in.floatValue[pin] : v;
    }
    bool connected(int pin) const override
    {
        const Instance& in = m_sim.m_inst[m_comp];
        return pin >= 0 && pin < int(in.pinNet.size()) && in.pinNet[pin] >= 0 && m_sim.m_netConnected[in.pinNet[pin]];
    }
    void drive(int pin, Drive d, std::uint32_t delay) override
    {
        if (pin >= 0)
            m_sim.schedule(m_sim.m_now + delay, m_comp, pin, d);
    }
    void wakeAfter(std::uint32_t ticks) override { m_sim.schedule(m_sim.m_now + std::max(1u, ticks), m_comp, -1, {}); }
    void linksChanged() override { m_sim.m_linksDirty = true; }
    bool settling() const override { return m_sim.m_settling; }

private:
    EventSimulator& m_sim;
    int m_comp;
};

EventSimulator::EventSimulator() = default;
EventSimulator::~EventSimulator() = default;

bool EventSimulator::build(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist)
{
    m_inst.clear();
    m_byId.clear();
    m_warnings.clear();
    m_netCount = int(netlist.nets.size());

    for (const Component& c : circuit.components()) {
        const SymbolDef* s = library.find(c.symbolId);
        if (!s) {
            m_warnings << QStringLiteral("%1: unknown symbol '%2'").arg(c.designator, c.symbolId);
            continue;
        }
        QString error;
        std::unique_ptr<Model> model = createModel(*s, c, &error);
        if (!error.isEmpty())
            m_warnings << error;
        if (!model)
            continue;
        Instance in;
        in.id = c.id;
        in.model = std::move(model);
        for (int i = 0; i < s->pins.size(); ++i) {
            in.pinNet.push_back(netlist.netOf(c.id, i));
            in.floatValue.push_back(fromBool(s->pins[i].floatValue != 0));
        }
        in.out.assign(in.pinNet.size(), released());
        m_byId.insert(c.id, int(m_inst.size()));
        m_inst.push_back(std::move(in));
    }

    m_netConnected.assign(m_netCount, false);
    for (int n = 0; n < m_netCount; ++n)
        m_netConnected[n] = netlist.nets[n].pins.size() + netlist.nets[n].wires.size() > 1;
    m_netPins.assign(m_netCount, {});
    for (int i = 0; i < int(m_inst.size()); ++i)
        for (int p = 0; p < int(m_inst[i].pinNet.size()); ++p)
            if (const int net = m_inst[i].pinNet[p]; net >= 0)
                m_netPins[net].push_back({i, p});
    return !m_inst.empty();
}

void EventSimulator::reset()
{
    m_queue = {};
    m_now = 0;
    m_seq = 0;
    m_oscillated = false;
    m_netValue.assign(m_netCount, Logic::Z);
    for (Instance& in : m_inst)
        std::fill(in.out.begin(), in.out.end(), released());
    rebuildGroups();
    m_settling = true;
    // init() sees every input as X so power-on state does not depend on the
    // order in which nets resolve; then every model evaluates real values once
    m_initializing = true;
    for (int i = 0; i < int(m_inst.size()); ++i) {
        Context ctx(*this, i);
        m_inst[i].model->init(ctx);
    }
    m_initializing = false;
    runInstant();
    for (int i = 0; i < int(m_inst.size()); ++i) {
        Context ctx(*this, i);
        m_inst[i].model->evaluate(ctx);
    }
    runInstant();
    // let gate outputs propagate before the first clock edge; stop at the
    // first wake-up (a pulser) so sources keep their timing
    constexpr std::uint64_t kSettleLimit = 1000;
    while (!m_queue.empty() && m_queue.top().pin >= 0 && m_queue.top().time <= kSettleLimit) {
        m_now = m_queue.top().time;
        runInstant();
    }
    m_settling = false;
    // restart the clock so time 0 is the settled state
    std::vector<Event> pending;
    for (; !m_queue.empty(); m_queue.pop())
        pending.push_back(m_queue.top());
    for (Event& e : pending) {
        e.time -= std::min(e.time, m_now);
        m_queue.push(e);
    }
    m_now = 0;
}

void EventSimulator::step(std::uint64_t ticks)
{
    const std::uint64_t target = m_now + ticks;
    runInstant();
    while (!m_queue.empty() && m_queue.top().time <= target) {
        m_now = std::max(m_now, m_queue.top().time);
        runInstant();
    }
    m_now = target;
}

void EventSimulator::stepToNextEvent()
{
    runInstant();
    if (m_queue.empty()) {
        ++m_now;
        return;
    }
    m_now = std::max(m_now, m_queue.top().time);
    runInstant();
}

void EventSimulator::schedule(std::uint64_t time, int comp, int pin, Drive d)
{
    m_queue.push({time, m_seq++, comp, pin, d});
}

void EventSimulator::runInstant()
{
    constexpr int kMaxDeltaCycles = 1000;
    for (int cycle = 0;; ++cycle) {
        if (cycle == kMaxDeltaCycles) {
            // zero-delay feedback that never settles: push the rest one tick on
            m_oscillated = true;
            std::vector<Event> pending;
            while (!m_queue.empty() && m_queue.top().time <= m_now) {
                pending.push_back(m_queue.top());
                m_queue.pop();
            }
            for (Event& e : pending) {
                e.time = m_now + 1;
                m_queue.push(e);
            }
            return;
        }

        QSet<int> dirty;
        bool any = false;
        while (!m_queue.empty() && m_queue.top().time <= m_now) {
            const Event e = m_queue.top();
            m_queue.pop();
            any = true;
            Instance& in = m_inst[e.comp];
            if (e.pin < 0) {
                Context ctx(*this, e.comp);
                in.model->wake(ctx);
                continue;
            }
            if (in.out[e.pin] == e.drive)
                continue;
            in.out[e.pin] = e.drive;
            if (const int net = in.pinNet[e.pin]; net >= 0)
                dirty.insert(net);
        }
        if (m_linksDirty) {
            m_linksDirty = false;
            any = true;
            rebuildGroups();
            for (int n = 0; n < m_netCount; ++n)
                dirty.insert(n);
        }
        if (!any)
            return;
        QSet<int> changed;
        resolve(dirty, changed);
        evaluateFanout(changed);
    }
}

void EventSimulator::rebuildGroups()
{
    std::vector<int> parent(m_netCount);
    for (int i = 0; i < m_netCount; ++i)
        parent[i] = i;
    auto find = [&](int x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };

    std::vector<std::pair<int, int>> weakPairs;
    for (const Instance& in : m_inst) {
        for (const LinkPath& l : in.model->links()) {
            if (l.a < 0 || l.b < 0)
                continue;
            const int na = in.pinNet[l.a], nb = in.pinNet[l.b];
            if (na < 0 || nb < 0 || na == nb)
                continue;
            if (l.strength == Strength::Strong)
                parent[find(na)] = find(nb);
            else
                weakPairs.push_back({na, nb});
        }
    }

    m_group.assign(m_netCount, -1);
    m_groupNets.clear();
    std::vector<int> rootGroup(m_netCount, -1);
    for (int n = 0; n < m_netCount; ++n) {
        const int r = find(n);
        if (rootGroup[r] < 0) {
            rootGroup[r] = int(m_groupNets.size());
            m_groupNets.emplace_back();
        }
        m_group[n] = rootGroup[r];
        m_groupNets[rootGroup[r]].push_back(n);
    }
    m_weakNeighbours.assign(m_groupNets.size(), {});
    for (auto [a, b] : weakPairs) {
        const int ga = m_group[a], gb = m_group[b];
        if (ga == gb)
            continue;
        m_weakNeighbours[ga].push_back(gb);
        m_weakNeighbours[gb].push_back(ga);
    }
    m_groupStrong.assign(m_groupNets.size(), Logic::Z);
}

void EventSimulator::resolve(const QSet<int>& dirtyNets, QSet<int>& changedNets)
{
    QSet<int> groups;
    for (int n : dirtyNets)
        groups.insert(m_group[n]);

    QSet<int> toValue = groups;
    for (int g : groups) {
        Logic s = Logic::Z;
        for (int net : m_groupNets[g])
            for (auto [inst, pin] : m_netPins[net])
                if (const Drive& d = m_inst[inst].out[pin]; d.strength == Strength::Strong)
                    s = combine(s, d.value);
        if (s != m_groupStrong[g]) {
            m_groupStrong[g] = s;
            for (int nb : m_weakNeighbours[g])
                toValue.insert(nb);
        }
    }

    for (int g : toValue) {
        Logic v = m_groupStrong[g];
        if (v == Logic::Z) {
            for (int net : m_groupNets[g])
                for (auto [inst, pin] : m_netPins[net])
                    if (const Drive& d = m_inst[inst].out[pin]; d.strength == Strength::Weak)
                        v = combine(v, d.value);
            for (int nb : m_weakNeighbours[g])
                v = combine(v, m_groupStrong[nb]);
        }
        for (int net : m_groupNets[g]) {
            if (m_netValue[net] != v) {
                m_netValue[net] = v;
                changedNets.insert(net);
            }
        }
    }
}

void EventSimulator::evaluateFanout(const QSet<int>& changedNets)
{
    std::vector<int> comps;
    for (int net : changedNets)
        for (auto [inst, pin] : m_netPins[net])
            comps.push_back(inst);
    std::sort(comps.begin(), comps.end());
    comps.erase(std::unique(comps.begin(), comps.end()), comps.end());
    for (int i : comps) {
        Context ctx(*this, i);
        m_inst[i].model->evaluate(ctx);
    }
}

Logic EventSimulator::netValue(int net) const
{
    return net >= 0 && net < int(m_netValue.size()) ? m_netValue[net] : Logic::Z;
}

int EventSimulator::componentState(ItemId id) const
{
    auto it = m_byId.constFind(id);
    return it == m_byId.cend() ? 0 : m_inst[*it].model->state();
}

void EventSimulator::interact(ItemId id, const QString& action, int value)
{
    auto it = m_byId.constFind(id);
    if (it == m_byId.cend())
        return;
    Context ctx(*this, *it);
    m_inst[*it].model->interact(ctx, action, value);
    runInstant();
}

QVariantMap EventSimulator::persistentProps(ItemId id) const
{
    auto it = m_byId.constFind(id);
    return it == m_byId.cend() ? QVariantMap{} : m_inst[*it].model->persistentProps();
}

} // namespace cm::sim
