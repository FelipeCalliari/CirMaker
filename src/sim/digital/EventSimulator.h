#pragma once

#include "core/NetlistBuilder.h"
#include "sim/digital/Models.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <memory>
#include <queue>
#include <vector>

namespace cm {
class Circuit;
class LibraryRegistry;
}

namespace cm::sim {

// Event-driven digital simulator over a netlist. Time advances in integer
// ticks; a gate output change appears one tick after its input change.
// Switches and resistors join nets into groups that resolve together.
class EventSimulator {
public:
    EventSimulator();
    ~EventSimulator();

    // Builds models for the circuit. Returns false when nothing can be
    // simulated; per-component problems are reported in warnings().
    bool build(const Circuit& circuit, const LibraryRegistry& library, const Netlist& netlist);
    const QStringList& warnings() const { return m_warnings; }

    void reset();
    void step(std::uint64_t ticks = 1);
    // Advances to the next instant at which anything happens.
    void stepToNextEvent();
    std::uint64_t time() const { return m_now; }

    Logic netValue(int net) const;
    int componentState(ItemId id) const;
    bool hasModel(ItemId id) const { return m_byId.contains(id); }
    void interact(ItemId id, const QString& action, int value = 0);
    // Properties that interactive models want saved (switch positions...).
    QVariantMap persistentProps(ItemId id) const;
    // Live memory contents of a RAM/ROM; setMemory updates the outputs.
    QVector<int> memory(ItemId id) const;
    void setMemory(ItemId id, const QVector<int>& words);
    // Records every change of these nets (for the digital waveform view).
    void watch(const QVector<int>& nets);
    using History = QVector<std::pair<std::uint64_t, Logic>>;
    const QHash<int, History>& history() const { return m_history; }
    // Set when zero-delay feedback did not settle at some instant.
    bool oscillated() const { return m_oscillated; }

private:
    struct Instance;
    class Context;
    struct Event {
        std::uint64_t time;
        std::uint64_t seq;
        int comp;
        int pin;            // -1: wake-up
        Drive drive;
        bool operator>(const Event& o) const { return time != o.time ? time > o.time : seq > o.seq; }
    };

    void schedule(std::uint64_t time, int comp, int pin, Drive d);
    void runInstant();
    void rebuildGroups();
    void resolve(const QSet<int>& dirtyNets, QSet<int>& changedNets);
    void evaluateFanout(const QSet<int>& changedNets);

    std::vector<Instance> m_inst;
    QHash<ItemId, int> m_byId;
    int m_netCount = 0;
    std::vector<std::vector<std::pair<int, int>>> m_netPins;   // net -> (instance, pin)
    std::vector<bool> m_netConnected;                          // net joins something besides one pin
    std::vector<int> m_group;                                  // net -> group
    std::vector<std::vector<int>> m_groupNets;
    std::vector<std::vector<int>> m_weakNeighbours;            // group -> groups via resistors
    std::vector<Logic> m_groupStrong;
    std::vector<Logic> m_netValue;
    std::priority_queue<Event, std::vector<Event>, std::greater<>> m_queue;
    std::uint64_t m_now = 0;
    std::uint64_t m_seq = 0;
    bool m_linksDirty = false;
    bool m_settling = false;
    bool m_initializing = false;
    bool m_oscillated = false;
    QStringList m_warnings;
    QSet<int> m_watched;
    QHash<int, History> m_history;
};

} // namespace cm::sim
