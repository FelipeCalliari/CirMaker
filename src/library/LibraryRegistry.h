#pragma once

#include "library/SymbolDef.h"

#include <QHash>
#include <QStringList>
#include <QTransform>

#include <memory>

namespace cm {

struct Component;

// Loads symbol definitions from JSON files. A file holds {"symbols": [...]};
// each entry either lists its graphics and pins or names a generator shape
// ("gate", "box") that produces them.
// Gates in a shared package: the part (1-based) a component is, the package
// pin number of one of its pins (the "pins" property overrides it), and its
// designator as shown ("U1A").
int packagePart(const SymbolDef& s, const Component& c);
QString packagePin(const SymbolDef& s, const Component& c, int pin);
QString shownDesignator(const SymbolDef& s, const Component& c);

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

    // Adds or replaces one symbol from its JSON description (as in the
    // library files). A replaced symbol keeps its address.
    bool addSymbol(const QJsonObject& o, QString* error);
    // The symbol a JSON description makes, without registering it.
    static bool parseSymbol(const QJsonObject& o, SymbolDef& s, QString* error);
    void removeSymbol(const QString& id);

private:

    // Symbols live in stable storage: items keep SymbolDef pointers while
    // macros are added or redefined at run time.
    QHash<QString, std::shared_ptr<SymbolDef>> m_symbols;
    QHash<QString, QString> m_aliases;  // lower-case alias -> id
    QStringList m_order;
};

namespace symbolgen {
void gate(SymbolDef& s, const QString& op, int inputs, bool invert, double pitch = 0);
void box(SymbolDef& s, const QJsonObject& spec);
} // namespace symbolgen

} // namespace cm
