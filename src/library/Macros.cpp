#include "library/Macros.h"

#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace cm::macros {

namespace {

constexpr int kMaxDepth = 8;            // nesting limit (and recursion guard)
constexpr double kRegion = 1e6;         // inner circuits are laid out this far apart
constexpr ItemId kInnerIds = ItemId(1) << 40;

// Macros the user defined here (persisted); others came from opened documents.
QSet<QString>& userIds()
{
    static QSet<QString> ids;
    return ids;
}

struct Port {
    QString name;
    double y;
};

} // namespace

bool isMacro(const SymbolDef& s) { return s.model.value(u"kind").toString() == u"macro"; }

QString idFor(const QString& name) { return QLatin1String(kIdPrefix) + name.trimmed(); }

QJsonObject symbolJson(const QString& name, const QJsonObject& circuitJson, QString* error)
{
    Circuit inner;
    if (!NativeFormat::deserialize(QJsonDocument(circuitJson).toJson(QJsonDocument::Compact), inner, error))
        return {};
    QVector<Port> ins, outs;
    for (const Component& c : inner.components()) {
        const bool in = c.symbolId == u"port_in", out = c.symbolId == u"port_out";
        const QString label = c.label.trimmed();
        if ((!in && !out) || label.isEmpty())
            continue;
        QVector<Port>& list = in ? ins : outs;
        if (std::none_of(list.cbegin(), list.cend(), [&](const Port& p) { return p.name == label; }))
            list << Port{label, c.pos.y()};
    }
    if (ins.isEmpty() && outs.isEmpty()) {
        if (error) *error = QStringLiteral("the circuit has no Input or Output ports");
        return {};
    }
    auto byY = [](const Port& a, const Port& b) { return a.y < b.y; };
    std::stable_sort(ins.begin(), ins.end(), byY);
    std::stable_sort(outs.begin(), outs.end(), byY);

    QJsonArray left, right;
    qsizetype longest = 0, longestLeft = 0, longestRight = 0;
    for (const Port& p : std::as_const(ins)) { left.append(p.name); longestLeft = std::max(longestLeft, p.name.size()); }
    for (const Port& p : std::as_const(outs)) { right.append(p.name); longestRight = std::max(longestRight, p.name.size()); }
    longest = longestLeft + longestRight;
    const double width = std::max(60.0, std::ceil((longest * 7 + 24) / 20.0) * 20);
    const qsizetype rows = std::max<qsizetype>({ins.size(), outs.size(), 1});
    const double pitch = 20;
    const double height = std::max(rows * pitch, 2 * pitch);
    QJsonObject shape{{u"type"_s, u"box"_s}, {u"width"_s, width}, {u"pitch"_s, pitch}, {u"lead"_s, 20},
                      {u"left"_s, left}, {u"right"_s, right}, {u"title"_s, name.trimmed()},
                      {u"titleY"_s, -height / 2 - 8}};
    return {{u"id"_s, idFor(name)}, {u"name"_s, name.trimmed()}, {u"category"_s, u"Macros"_s},
            {u"prefix"_s, u"U"_s}, {u"aliases"_s, QJsonArray{name.trimmed()}},
            {u"description"_s, QStringLiteral("Macro: %1 inputs, %2 outputs").arg(ins.size()).arg(outs.size())},
            {u"shape"_s, shape},
            {u"model"_s, QJsonObject{{u"kind"_s, u"macro"_s}, {u"circuit"_s, circuitJson}}}};
}

QString registerMacro(const QString& name, const QJsonObject& circuitJson, QString* error)
{
    if (name.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("the macro needs a name");
        return {};
    }
    // nested macros come with the inner document
    registerEmbedded(circuitJson.value(u"macros").toArray());
    const QJsonObject sym = symbolJson(name, circuitJson, error);
    if (sym.isEmpty() || !LibraryRegistry::instance().addSymbol(sym, error))
        return {};
    return sym.value(u"id").toString();
}

QString define(const QString& name, const Circuit& inner, QString* error, const QString& lock)
{
    QJsonObject doc = QJsonDocument::fromJson(NativeFormat::serialize(inner)).object();
    if (!lock.isEmpty()) doc.insert(u"lock", lock);
    const QString id = registerMacro(name, doc, error);
    if (id.isEmpty())
        return {};
    userIds().insert(id);
    saveUserLibrary(error);
    return id;
}

QString lockOf(const SymbolDef& s) { return s.model.value(u"circuit").toObject().value(u"lock").toString(); }

bool setLock(const QString& id, const QString& digest, QString* error)
{
    const SymbolDef* s = LibraryRegistry::instance().find(id);
    if (!s || !isMacro(*s)) {
        if (error) *error = QStringLiteral("%1 is not a macro").arg(id);
        return false;
    }
    QJsonObject doc = s->model.value(u"circuit").toObject();
    if (digest.isEmpty()) doc.remove(u"lock");
    else doc.insert(u"lock", digest);
    if (registerMacro(s->name, doc, error).isEmpty())
        return false;
    return !userIds().contains(id) || saveUserLibrary(error);
}

bool innerCircuit(const SymbolDef& s, Circuit& out, QString* error)
{
    if (!isMacro(s)) {
        if (error) *error = QStringLiteral("%1 is not a macro").arg(s.id);
        return false;
    }
    return NativeFormat::deserialize(QJsonDocument(s.model.value(u"circuit").toObject()).toJson(QJsonDocument::Compact),
                                     out, error);
}

QString userLibraryPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(u"macros.json"_s);
}

void loadUserLibrary()
{
    QFile f(userLibraryPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value(u"macros").toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = registerMacro(o.value(u"name").toString(), o.value(u"circuit").toObject());
        if (!id.isEmpty())
            userIds().insert(id);
    }
}

bool saveUserLibrary(QString* error)
{
    QJsonArray list;
    const LibraryRegistry& lib = LibraryRegistry::instance();
    QStringList ids(userIds().cbegin(), userIds().cend());
    ids.sort();
    for (const QString& id : std::as_const(ids))
        if (const SymbolDef* s = lib.find(id))
            list.append(QJsonObject{{u"name"_s, s->name}, {u"circuit"_s, s->model.value(u"circuit")}});
    const QString path = userLibraryPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(QJsonObject{{u"macros"_s, list}}).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

void forget(const QString& id)
{
    userIds().remove(id);
    LibraryRegistry::instance().removeSymbol(id);
    saveUserLibrary();
}

QJsonArray embeddedFor(const Circuit& circuit)
{
    QJsonArray out;
    QSet<QString> seen;
    const LibraryRegistry& lib = LibraryRegistry::instance();
    for (const Component& c : circuit.components()) {
        const SymbolDef* s = lib.find(c.symbolId);
        if (!s || !isMacro(*s) || seen.contains(s->id))
            continue;
        seen.insert(s->id);
        // the inner document already embeds the macros it uses
        out.append(QJsonObject{{u"name"_s, s->name}, {u"circuit"_s, s->model.value(u"circuit")}});
    }
    return out;
}

void registerEmbedded(const QJsonArray& list)
{
    const LibraryRegistry& lib = LibraryRegistry::instance();
    for (const QJsonValue& v : list) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(u"name").toString();
        const QJsonObject circuit = o.value(u"circuit").toObject();
        // a document's own copy wins, except over an identical definition
        if (const SymbolDef* s = lib.find(idFor(name)); s && s->model.value(u"circuit").toObject() == circuit)
            continue;
        registerMacro(name, circuit);
    }
}

namespace {

class Flattener {
public:
    Flattener(Circuit& out, const LibraryRegistry& lib, QStringList* warnings)
        : m_out(out), m_lib(lib), m_warnings(warnings)
    {
        if (const SymbolDef* nl = lib.find(u"netlabel"_s); nl && !nl->pins.isEmpty())
            m_labelPin = nl->pins.first().pos;
    }

    // Lays out one instance's contents. scope names the instance path ("U3/").
    void expand(const Component& inst, const SymbolDef& s, const QString& scope, int depth)
    {
        if (depth >= kMaxDepth) {
            warn(QStringLiteral("%1: macros nested too deep (recursive macro?)").arg(inst.designator));
            return;
        }
        Circuit inner;
        QString error;
        if (!innerCircuit(s, inner, &error)) {
            warn(QStringLiteral("%1 (%2): %3").arg(inst.designator, s.name, error));
            return;
        }
        const QString name = scope + (inst.designator.isEmpty() ? QString::number(inst.id) : inst.designator) + u'/';
        const QString suffix = u'_' + QString(name).chopped(1).replace(u'/', u'_');
        const QPointF offset(kRegion * ++m_region, 0);

        // instance pins -> net labels named after the ports
        for (int i = 0; i < s.pins.size(); ++i) {
            Component nl;
            nl.id = m_nextId++;
            nl.symbolId = u"netlabel"_s;
            nl.label = name + s.pins[i].name;
            nl.pos = m_lib.pinScenePos(inst, i) - m_labelPin;
            m_out.addComponent(nl);
        }
        for (Component c : inner.components()) {
            const SymbolDef* cs = m_lib.find(c.symbolId);
            c.id = m_nextId++;
            c.pos += offset;
            if (!c.designator.isEmpty())
                c.designator += suffix;
            // ports and terminals are local to the instance
            if (cs && cs->netLabel && !c.label.trimmed().isEmpty())
                c.label = name + c.label.trimmed();
            m_out.addComponent(c);
            if (cs && isMacro(*cs))
                expand(c, *cs, name, depth + 1);
        }
        for (Wire w : inner.wires()) {
            w.id = m_nextId++;
            for (QPointF& p : w.points)
                p += offset;
            m_out.addWire(w);
        }
    }

private:
    void warn(const QString& text)
    {
        if (m_warnings) *m_warnings << text;
    }

    Circuit& m_out;
    const LibraryRegistry& m_lib;
    QStringList* m_warnings;
    QPointF m_labelPin;
    ItemId m_nextId = kInnerIds;
    int m_region = 0;
};

} // namespace

std::unique_ptr<Circuit> flatten(const Circuit& circuit, const LibraryRegistry& library, QStringList* warnings)
{
    QVector<std::pair<const Component*, const SymbolDef*>> instances;
    for (const Component& c : circuit.components())
        if (const SymbolDef* s = library.find(c.symbolId); s && isMacro(*s))
            instances.push_back({&c, s});
    if (instances.isEmpty())
        return nullptr;

    auto out = std::make_unique<Circuit>();
    for (const Component& c : circuit.components())
        out->addComponent(c);
    for (const Wire& w : circuit.wires())
        out->addWire(w);
    out->setAnalysis(circuit.analysis());
    out->setProbes(circuit.probes());
    Flattener f(*out, library, warnings);
    for (const auto& [c, s] : std::as_const(instances))
        f.expand(*c, *s, QString(), 0);
    return out;
}

} // namespace cm::macros
