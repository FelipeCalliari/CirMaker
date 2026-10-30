#pragma once

#include "sim/digital/Logic.h"

#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>

namespace cm {
struct Component;
struct SymbolDef;
}

namespace cm::sim {

// What a model sees of the simulator, bound to one component instance.
class ModelContext {
public:
    virtual ~ModelContext() = default;
    virtual std::uint64_t now() const = 0;
    // Resolved value on the pin's net; the pin's float value when unconnected
    // or undriven.
    virtual Logic input(int pin) const = 0;
    // The pin's net reaches another pin or a wire.
    virtual bool connected(int pin) const = 0;
    virtual void drive(int pin, Drive d, std::uint32_t delay = 1) = 0;
    virtual void wakeAfter(std::uint32_t ticks) = 0;
    // A switch opened or closed: the simulator recomputes net groups.
    virtual void linksChanged() = 0;
    // True while reset settles the initial state; edge-triggered models must
    // not treat start-up transitions as clock edges.
    virtual bool settling() const = 0;
};

// A conducting path between two pins of a link model (switch, resistor).
struct LinkPath {
    int a = -1, b = -1;
    Strength strength = Strength::Strong;
};

class Model {
public:
    virtual ~Model() = default;
    virtual void init(ModelContext& ctx) { evaluate(ctx); }
    virtual void evaluate(ModelContext& ctx) = 0;
    virtual void wake(ModelContext&) {}
    // User interaction during simulation: "toggle", "press", "release", "key".
    virtual void interact(ModelContext&, const QString& /*action*/, int /*value*/) {}
    // Value shown by the symbol (lamp on, hex digit, switch position...);
    // -1 means unknown.
    virtual int state() const { return 0; }
    // Closed conducting paths; only link models return any.
    virtual QVector<LinkPath> links() const { return {}; }
    // Properties to persist back into the component (e.g. switch position).
    virtual QVariantMap persistentProps() const { return {}; }
    // Memory contents (RAM/ROM); empty for models without memory.
    virtual QVector<int> memory() const { return {}; }
    virtual void setMemory(ModelContext&, const QVector<int>& /*words*/) {}
};

// Stepper motor rotor, in half steps since the start, after the coils
// (1, 2, 3, 4) given are energized; shared with the analog display.
int stepperAdvance(int halfSteps, const bool energized[4]);

// Creates the model named by symbol.model["kind"]; nullptr if the symbol has
// no digital behaviour. *error is set when the kind is unknown or pins are missing.
std::unique_ptr<Model> createModel(const SymbolDef& symbol, const Component& component, QString* error);
// MSI chips (ChipModels.cpp); called by createModel for kinds it does not handle.
std::unique_ptr<Model> createChipModel(const SymbolDef& symbol, const Component& component, QString* error);

} // namespace cm::sim
