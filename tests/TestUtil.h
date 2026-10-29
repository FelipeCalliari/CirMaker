#pragma once

#include "core/Circuit.h"
#include "core/NetlistBuilder.h"
#include "library/LibraryRegistry.h"
#include "sim/digital/EventSimulator.h"

#include <QtTest>

inline void cmInitLibraryResources() { Q_INIT_RESOURCE(symbols); }

namespace cm::test {

inline LibraryRegistry& library()
{
    static bool loaded = false;
    LibraryRegistry& lib = LibraryRegistry::instance();
    if (!loaded) {
        cmInitLibraryResources();
        lib.loadBuiltin();
        loaded = true;
    }
    return lib;
}

inline ItemId place(Circuit& c, const QString& symbol, QPointF pos, const QString& ref = {}, const QString& label = {})
{
    Component comp;
    comp.symbolId = symbol;
    comp.pos = pos;
    comp.designator = ref;
    comp.label = label;
    return c.addComponent(comp);
}

inline QPointF pinPos(const Circuit& c, ItemId id, const QString& pin)
{
    const Component* comp = c.component(id);
    const SymbolDef* s = library().find(comp->symbolId);
    return library().pinScenePos(*comp, s->pinIndex(pin));
}

// Straight wire between two pins (connectivity does not care about angles).
inline void link(Circuit& c, ItemId a, const QString& pa, ItemId b, const QString& pb)
{
    Wire w;
    w.points = {pinPos(c, a, pa), pinPos(c, b, pb)};
    c.addWire(w);
}

// Connects a pin to a named net with a terminal sitting on the pin.
inline void label(Circuit& c, ItemId comp, const QString& pin, const QString& net)
{
    const SymbolDef* t = library().find(QStringLiteral("terminal"));
    place(c, QStringLiteral("terminal"), pinPos(c, comp, pin) - t->pins[0].pos, {}, net);
}

struct Bench {
    Netlist netlist;
    sim::EventSimulator sim;

    Bench(const Circuit& c)
    {
        netlist = NetlistBuilder::build(c, library());
        sim.build(c, library(), netlist);
        sim.reset();
    }
    sim::Logic value(const Circuit& c, ItemId id, const QString& pin) const
    {
        const SymbolDef* s = library().find(c.component(id)->symbolId);
        return sim.netValue(netlist.netOf(id, s->pinIndex(pin)));
    }
};

} // namespace cm::test
