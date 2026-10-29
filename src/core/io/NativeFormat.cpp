#include "core/io/NativeFormat.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace cm::NativeFormat {

namespace {

QJsonArray point(QPointF p) { return {p.x(), p.y()}; }
QPointF point(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    return {a.at(0).toDouble(), a.at(1).toDouble()};
}

} // namespace

QByteArray serialize(const Circuit& circuit)
{
    QJsonArray comps, wires, texts;
    for (const Component& c : circuit.components()) {
        QJsonObject o{{u"symbol"_s, c.symbolId}, {u"ref"_s, c.designator}, {u"pos"_s, point(c.pos)}};
        if (!c.label.isEmpty()) o.insert(u"label", c.label);
        if (c.rotation) o.insert(u"rot", c.rotation);
        if (c.mirrored) o.insert(u"mirror", true);
        if (!c.props.isEmpty()) o.insert(u"props", QJsonObject::fromVariantMap(c.props));
        comps.append(o);
    }
    for (const Wire& w : circuit.wires()) {
        QJsonArray pts;
        for (QPointF p : w.points)
            pts.append(point(p));
        QJsonObject o{{u"points"_s, pts}};
        if (w.bus) o.insert(u"bus", true);
        wires.append(o);
    }
    for (const TextNote& t : circuit.texts())
        texts.append(QJsonObject{{u"pos"_s, point(t.pos)}, {u"text"_s, t.text}, {u"size"_s, t.pointSize}});

    const TitleBlock& tb = circuit.titleBlock();
    QJsonObject root{
        {u"format"_s, u"cirmaker-qt"_s},
        {u"version"_s, kVersion},
        {u"title"_s, QJsonObject{{u"title"_s, tb.title}, {u"author"_s, tb.author}, {u"date"_s, tb.date},
                                 {u"revision"_s, tb.revision}, {u"visible"_s, tb.visible}, {u"pos"_s, point(tb.pos)}}},
        {u"components"_s, comps},
        {u"wires"_s, wires},
        {u"texts"_s, texts},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool deserialize(const QByteArray& data, Circuit& circuit, QString* error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    const QJsonObject root = doc.object();
    if (doc.isNull() || root.value(u"format").toString() != u"cirmaker-qt") {
        if (error) *error = doc.isNull() ? pe.errorString() : QStringLiteral("not a CirMaker Qt document");
        return false;
    }
    if (root.value(u"version").toInt() > kVersion) {
        if (error) *error = QStringLiteral("document version %1 is newer than this program").arg(root.value(u"version").toInt());
        return false;
    }

    circuit.clear();
    const QJsonObject tb = root.value(u"title").toObject();
    TitleBlock block;
    block.title = tb.value(u"title").toString();
    block.author = tb.value(u"author").toString();
    block.date = tb.value(u"date").toString();
    block.revision = tb.value(u"revision").toString();
    block.visible = tb.value(u"visible").toBool();
    block.pos = point(tb.value(u"pos"));
    circuit.setTitleBlock(block);
    for (const QJsonValue& v : root.value(u"components").toArray()) {
        const QJsonObject o = v.toObject();
        Component c;
        c.symbolId = o.value(u"symbol").toString();
        c.designator = o.value(u"ref").toString();
        c.label = o.value(u"label").toString();
        c.pos = point(o.value(u"pos"));
        c.rotation = o.value(u"rot").toInt() & 3;
        c.mirrored = o.value(u"mirror").toBool();
        c.props = o.value(u"props").toObject().toVariantMap();
        circuit.addComponent(c);
    }
    for (const QJsonValue& v : root.value(u"wires").toArray()) {
        Wire w;
        for (const QJsonValue& p : v.toObject().value(u"points").toArray())
            w.points << point(p);
        w.bus = v.toObject().value(u"bus").toBool();
        if (w.points.size() >= 2)
            circuit.addWire(w);
    }
    for (const QJsonValue& v : root.value(u"texts").toArray()) {
        const QJsonObject o = v.toObject();
        circuit.addText({0, point(o.value(u"pos")), o.value(u"text").toString(), o.value(u"size").toDouble(10)});
    }
    return true;
}

bool save(const QString& path, const Circuit& circuit, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(serialize(circuit));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool load(const QString& path, Circuit& circuit, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    return deserialize(f.readAll(), circuit, error);
}

} // namespace cm::NativeFormat
