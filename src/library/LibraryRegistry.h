#pragma once

#include "library/SymbolDef.h"

#include <QHash>
#include <QStringList>
#include <QTransform>

namespace cm {

struct Component;

// Loads symbol definitions from JSON files. A file holds {"symbols": [...]};
// each entry either lists its graphics and pins or names a generator shape
// ("gate", "box") that produces them.
class LibraryRegistry {
public:
    static LibraryRegistry& instance();

    // Loads every *.json under ":/library/symbols" (the embedded library).
    void loadBuiltin();
    bool loadFile(const QString& path, QString* error = nullptr);
    void clear();

    const SymbolDef* find(const QString& id) const;
    // Matches CircuitMaker 2000 device names, with or without the trailing '~'.
    const SymbolDef* findByAlias(const QString& cmName) const;
    QList<const SymbolDef*> all() const;
    QStringList categories() const;

    // Component placement transform: symbol-local -> scene coordinates.
    static QTransform placement(const Component& c);
    QPointF pinScenePos(const Component& c, int pin) const;

private:
    bool addSymbol(const QJsonObject& o, QString* error);

    QHash<QString, SymbolDef> m_symbols;
    QHash<QString, QString> m_aliases;  // lower-case alias -> id
    QStringList m_order;
};

namespace symbolgen {
void gate(SymbolDef& s, const QString& op, int inputs, bool invert);
void box(SymbolDef& s, const QJsonObject& spec);
} // namespace symbolgen

} // namespace cm
