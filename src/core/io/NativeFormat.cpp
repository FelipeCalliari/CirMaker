#include "core/io/NativeFormat.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "library/Macros.h"

using namespace Qt::StringLiterals;

namespace cm::NativeFormat {

namespace {

QJsonArray point(QPointF p) { return {p.x(), p.y()}; }
QPointF point(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    return {a.at(0).toDouble(), a.at(1).toDouble()};
}

const char* const kAnalysisTypes[] = {"op", "tran", "ac", "dc", "fourier", "tf", "noise"};
const char* const kSweeps[] = {"none", "temp", "param", "montecarlo"};

QJsonObject analysisToJson(const AnalysisSettings& a)
{
    return {{u"analog"_s, a.analogMode}, {u"type"_s, QString::fromLatin1(kAnalysisTypes[int(a.type)])},
            {u"tranStop"_s, a.tranStop}, {u"tranStep"_s, a.tranStep},
            {u"acStart"_s, a.acStart}, {u"acStop"_s, a.acStop}, {u"acPoints"_s, a.acPointsPerDecade},
            {u"dcSource"_s, a.dcSource}, {u"dcStart"_s, a.dcStart}, {u"dcStop"_s, a.dcStop}, {u"dcStep"_s, a.dcStep},
            {u"breakEdge"_s, a.breakEdge}, {u"breakAll"_s, a.breakAll},
            {u"fourierFreq"_s, a.fourierFreq}, {u"fourierHarmonics"_s, a.fourierHarmonics},
            {u"output"_s, a.output}, {u"inputSource"_s, a.inputSource},
            {u"sweep"_s, QString::fromLatin1(kSweeps[int(a.sweep)])},
            {u"tempStart"_s, a.tempStart}, {u"tempStop"_s, a.tempStop}, {u"tempStep"_s, a.tempStep},
            {u"paramDevice"_s, a.paramDevice}, {u"paramStart"_s, a.paramStart}, {u"paramStop"_s, a.paramStop},
            {u"paramStep"_s, a.paramStep}, {u"mcRuns"_s, a.mcRuns}, {u"mcTolerance"_s, a.mcTolerance},
            {u"spiceOptions"_s, a.spiceOptions}, {u"cmAnalyses"_s, a.cmAnalyses},
            {u"tranStart"_s, a.tranStart}, {u"tranMax"_s, a.tranMax}, {u"tranUic"_s, a.tranUic}, {u"acSweep"_s, a.acSweep},
            {u"cmOptions"_s, a.cmOptions}, {u"cmSimLine"_s, a.cmSimLine},
            {u"dcSource2"_s, a.dcSource2}, {u"dcStart2"_s, a.dcStart2}, {u"dcStop2"_s, a.dcStop2}, {u"dcStep2"_s, a.dcStep2}};
}

QJsonObject faultsToJson(const FaultSettings& f)
{
    return {{u"password"_s, f.password}, {u"disabled"_s, QJsonArray::fromStringList(f.disabled)},
            {u"high"_s, f.high}, {u"low"_s, f.low}, {u"open"_s, f.open}, {u"short"_s, f.shorted}};
}

FaultSettings faultsFromJson(const QJsonObject& o)
{
    FaultSettings f;
    f.password = o.value(u"password").toString();
    for (const QJsonValue& v : o.value(u"disabled").toArray()) f.disabled << v.toString();
    f.high = o.value(u"high").toString(f.high);
    f.low = o.value(u"low").toString(f.low);
    f.open = o.value(u"open").toString(f.open);
    f.shorted = o.value(u"short").toString(f.shorted);
    return f;
}

AnalysisSettings analysisFromJson(const QJsonObject& o)
{
    AnalysisSettings a;
    if (o.isEmpty())
        return a;
    a.analogMode = o.value(u"analog").toBool();
    const QString type = o.value(u"type").toString();
    for (int i = 0; i < int(std::size(kAnalysisTypes)); ++i)
        if (type == QLatin1String(kAnalysisTypes[i]))
            a.type = AnalysisSettings::Type(i);
    a.tranStop = o.value(u"tranStop").toString(a.tranStop);
    a.tranStep = o.value(u"tranStep").toString(a.tranStep);
    a.acStart = o.value(u"acStart").toString(a.acStart);
    a.acStop = o.value(u"acStop").toString(a.acStop);
    a.acPointsPerDecade = o.value(u"acPoints").toInt(a.acPointsPerDecade);
    a.dcSource = o.value(u"dcSource").toString();
    a.breakEdge = o.value(u"breakEdge").toBool(a.breakEdge);
    a.fourierFreq = o.value(u"fourierFreq").toString();
    a.fourierHarmonics = o.value(u"fourierHarmonics").toInt(a.fourierHarmonics);
    a.output = o.value(u"output").toString();
    a.inputSource = o.value(u"inputSource").toString();
    const QString sweep = o.value(u"sweep").toString();
    for (int i = 0; i < int(std::size(kSweeps)); ++i)
        if (sweep == QLatin1String(kSweeps[i]))
            a.sweep = AnalysisSettings::Sweep(i);
    a.tempStart = o.value(u"tempStart").toString(a.tempStart);
    a.tempStop = o.value(u"tempStop").toString(a.tempStop);
    a.tempStep = o.value(u"tempStep").toString(a.tempStep);
    a.paramDevice = o.value(u"paramDevice").toString();
    a.paramStart = o.value(u"paramStart").toString(a.paramStart);
    a.paramStop = o.value(u"paramStop").toString(a.paramStop);
    a.paramStep = o.value(u"paramStep").toString(a.paramStep);
    a.mcRuns = o.value(u"mcRuns").toInt(a.mcRuns);
    a.mcTolerance = o.value(u"mcTolerance").toString(a.mcTolerance);
    a.spiceOptions = o.value(u"spiceOptions").toString();
    a.breakAll = o.value(u"breakAll").toBool(a.breakAll);
    a.dcStart = o.value(u"dcStart").toString(a.dcStart);
    a.dcStop = o.value(u"dcStop").toString(a.dcStop);
    a.dcStep = o.value(u"dcStep").toString(a.dcStep);
    a.cmAnalyses = o.value(u"cmAnalyses").toInt(a.cmAnalyses);
    a.tranStart = o.value(u"tranStart").toString(a.tranStart);
    a.tranMax = o.value(u"tranMax").toString();
    a.tranUic = o.value(u"tranUic").toBool();
    a.acSweep = o.value(u"acSweep").toString(a.acSweep);
    a.cmOptions = o.value(u"cmOptions").toInt(a.cmOptions);
    a.cmSimLine = o.value(u"cmSimLine").toString();
    a.dcSource2 = o.value(u"dcSource2").toString();
    a.dcStart2 = o.value(u"dcStart2").toString(a.dcStart2);
    a.dcStop2 = o.value(u"dcStop2").toString(a.dcStop2);
    a.dcStep2 = o.value(u"dcStep2").toString(a.dcStep2);
    return a;
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
        if (c.showDesignator) o.insert(u"showRef", *c.showDesignator);
        if (c.showLabel) o.insert(u"showLabel", *c.showLabel);
        if (c.designatorAt) o.insert(u"refAt", point(*c.designatorAt));
        if (c.labelAt) o.insert(u"labelAt", point(*c.labelAt));
        comps.append(o);
    }
    for (const Wire& w : circuit.wires()) {
        QJsonArray pts;
        for (QPointF p : w.points)
            pts.append(point(p));
        QJsonObject o{{u"points"_s, pts}};
        if (w.bus) o.insert(u"bus", true);
        if (!w.name.isEmpty()) o.insert(u"name", w.name);
        wires.append(o);
    }
    for (const TextNote& t : circuit.texts()) {
        QJsonObject o{{u"pos"_s, point(t.pos)}, {u"text"_s, t.text}, {u"size"_s, t.pointSize}};
        if (t.nameAt) o.insert(u"nameAt"_s, point(*t.nameAt));
        texts.append(o);
    }

    QJsonArray probes;
    for (QPointF p : circuit.probes())
        probes.append(point(p));
    QJsonArray probeNames;
    for (const Circuit::ProbeTag& t : circuit.probeNames())
        probeNames.append(QJsonArray{t.at.x(), t.at.y(), t.name, t.cmRecord, QJsonArray::fromStringList(t.cmHeader),
                                     t.cmAt ? QJsonValue(QJsonArray{t.cmAt->x(), t.cmAt->y()}) : QJsonValue()});
    const TitleBlock& tb = circuit.titleBlock();
    QJsonObject root{
        {u"format"_s, u"cirmaker-qt"_s},
        {u"version"_s, kVersion},
        {u"title"_s, QJsonObject{{u"title"_s, tb.title}, {u"author"_s, tb.author}, {u"date"_s, tb.date},
                                 {u"revision"_s, tb.revision}, {u"visible"_s, tb.visible}, {u"pos"_s, point(tb.pos)}}},
        {u"analysis"_s, analysisToJson(circuit.analysis())},
        {u"probes"_s, probes},
        {u"probeNames"_s, probeNames},
        {u"faults"_s, faultsToJson(circuit.faultSettings())},
        {u"components"_s, comps},
        {u"wires"_s, wires},
        {u"texts"_s, texts},
    };
    if (const QJsonArray macros = macros::embeddedFor(circuit); !macros.isEmpty())
        root.insert(u"macros", macros);
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
    macros::registerEmbedded(root.value(u"macros").toArray());
    const QJsonObject tb = root.value(u"title").toObject();
    TitleBlock block;
    block.title = tb.value(u"title").toString();
    block.author = tb.value(u"author").toString();
    block.date = tb.value(u"date").toString();
    block.revision = tb.value(u"revision").toString();
    block.visible = tb.value(u"visible").toBool();
    block.pos = point(tb.value(u"pos"));
    circuit.setTitleBlock(block);
    circuit.setAnalysis(analysisFromJson(root.value(u"analysis").toObject()));
    QVector<QPointF> probes;
    for (const QJsonValue& v : root.value(u"probes").toArray())
        probes << point(v);
    circuit.setProbes(probes);
    QVector<Circuit::ProbeTag> probeNames;
    for (const QJsonValue& v : root.value(u"probeNames").toArray()) {
        const QJsonArray a = v.toArray();
        QStringList header;
        for (const QJsonValue& h : a.at(4).toArray()) header << h.toString();
        std::optional<QPoint> cmAt;
        if (a.at(5).isArray()) cmAt = QPoint(a.at(5).toArray().at(0).toInt(), a.at(5).toArray().at(1).toInt());
        probeNames.push_back({QPointF(a.at(0).toDouble(), a.at(1).toDouble()), a.at(2).toString(), a.at(3).toString(), header, cmAt});
    }
    circuit.setProbeNames(probeNames);
    circuit.setFaultSettings(faultsFromJson(root.value(u"faults").toObject()));
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
        if (o.contains(u"showRef")) c.showDesignator = o.value(u"showRef").toBool();
        if (o.contains(u"showLabel")) c.showLabel = o.value(u"showLabel").toBool();
        if (o.contains(u"refAt")) c.designatorAt = point(o.value(u"refAt"));
        if (o.contains(u"labelAt")) c.labelAt = point(o.value(u"labelAt"));
        circuit.addComponent(c);
    }
    for (const QJsonValue& v : root.value(u"wires").toArray()) {
        Wire w;
        for (const QJsonValue& p : v.toObject().value(u"points").toArray())
            w.points << point(p);
        w.bus = v.toObject().value(u"bus").toBool();
        w.name = v.toObject().value(u"name").toString();
        if (w.points.size() >= 2)
            circuit.addWire(w);
    }
    for (const QJsonValue& v : root.value(u"texts").toArray()) {
        const QJsonObject o = v.toObject();
        TextNote t{0, point(o.value(u"pos")), o.value(u"text").toString(), o.value(u"size").toDouble(10)};
        if (o.contains(u"nameAt")) t.nameAt = point(o.value(u"nameAt"));
        circuit.addText(t);
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
