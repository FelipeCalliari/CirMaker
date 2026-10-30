#pragma once

#include "core/Circuit.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <memory>

namespace cm {

struct SymbolDef;
class LibraryRegistry;

// Macros (CircuitMaker's Macros menu): a circuit saved as a device. Its Input
// and Output ports become the pins of a generated box symbol, inputs on the
// left and outputs on the right, ordered top to bottom. The inner circuit
// travels inside the symbol's model ({"kind": "macro", "circuit": {...}}).
namespace macros {

inline constexpr auto kIdPrefix = "macro:";

bool isMacro(const SymbolDef& s);
QString idFor(const QString& name);

// Symbol JSON for a macro; circuitJson is a serialized .cmq document.
QJsonObject symbolJson(const QString& name, const QJsonObject& circuitJson, QString* error = nullptr);
// Registers (or redefines) a macro. Returns its symbol id, empty on error
// (no ports, bad document...).
QString registerMacro(const QString& name, const QJsonObject& circuitJson, QString* error = nullptr);
// Defines a macro from a circuit and keeps it in the user library; `lock`
// is the Macro Lock password digest, if any.
QString define(const QString& name, const Circuit& inner, QString* error = nullptr, const QString& lock = {});
// Macro Lock: a password digest (faults::hashPassword) that guards
// expanding the macro; it travels in the definition. Empty = unlocked.
QString lockOf(const SymbolDef& s);
bool setLock(const QString& id, const QString& digest, QString* error = nullptr);
bool innerCircuit(const SymbolDef& s, Circuit& out, QString* error = nullptr);

// User library: macros defined on this machine (app data folder).
QString userLibraryPath();
void loadUserLibrary();
bool saveUserLibrary(QString* error = nullptr);
void forget(const QString& id);     // removes from the user library and the registry

// Definitions a document uses, embedded in .cmq files so they open anywhere.
QJsonArray embeddedFor(const Circuit& circuit);
void registerEmbedded(const QJsonArray& macros);

// The circuit with every macro instance's contents laid out beside it (far
// away from the drawing), joined to the instance pins by net labels. Outer
// items keep their ids, so netlists of the result index the same wires and
// pins. Returns nullptr when the circuit has no macros.
std::unique_ptr<Circuit> flatten(const Circuit& circuit, const LibraryRegistry& library, QStringList* warnings = nullptr);

} // namespace macros
} // namespace cm
