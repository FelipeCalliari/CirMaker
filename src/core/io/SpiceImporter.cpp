#include "core/io/SpiceImporter.h"

#include "library/LibraryRegistry.h"

#include <QHash>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace cm::SpiceImporter {

namespace {

// Lines of a netlist with continuations joined and comments dropped; the
// first line is the title.
QStringList logicalLines(const QString& text)
{
    QStringList out;
    const QStringList raw = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (int i = 0; i < raw.size(); ++i) {
        QString l = raw[i];
        if (i == 0) continue;                               // title
        if (const int c = l.indexOf(u';'); c >= 0) l.truncate(c);
        l = l.trimmed();
        if (l.isEmpty() || l.startsWith(u'*')) continue;
        if (l.startsWith(u'+') && !out.isEmpty()) out.last() += u' ' + l.mid(1).trimmed();
        else out << l;
    }
    return out;
}

// "DC 5", "SIN(0 1 1k)", "PULSE(...)": splits on blanks, keeping a
// parenthesised group as one token
QStringList tokens(const QString& line)
{
    QStringList out;
    QString cur;
    int depth = 0;
    for (const QChar ch : line) {
        if (ch == u'(') ++depth;
        if (ch == u')') --depth;
        if ((ch.isSpace() || ch == u',') && depth == 0) {
            if (!cur.isEmpty()) out << cur;
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.isEmpty()) out << cur;
    return out;
}

QStringList args(const QString& group)          // "SIN(0 1 1k)" -> {0, 1, 1k}
{
    const int open = group.indexOf(u'(');
    return group.mid(open + 1, group.size() - open - 2).split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
}

} // namespace

bool importText(const QString& text, Circuit& circuit, const LibraryRegistry& library, SpiceImportReport* report,
                QString* error)
{
    SpiceImportReport rep;
    const QStringList lines = logicalLines(text);
    // model types: .model Q1 PNP(...) decides npn/pnp, nmos/pmos
    QHash<QString, QString> modelType, modelText;
    static const QRegularExpression model(QStringLiteral("^\\.model\\s+(\\S+)\\s+([a-zA-Z]+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& l : lines)
        if (const auto m = model.match(l); m.hasMatch()) {
            modelType.insert(m.captured(1).toLower(), m.captured(2).toLower());
            modelText.insert(m.captured(1).toLower(), l.simplified());
        }

    AnalysisSettings analysis = circuit.analysis();
    analysis.analogMode = true;
    int count = 0;
    int depth = 0;                                          // inside .subckt
    for (const QString& l : lines) {
        const QStringList t = tokens(l);
        const QString head = t.value(0).toLower();
        if (head == u".subckt") { ++depth; rep.skipped << l; continue; }
        if (head == u".ends") { depth = std::max(0, depth - 1); continue; }
        if (depth > 0) continue;
        if (head == u".end") break;
        if (head.startsWith(u'.')) {
            if (head == u".op") analysis.type = AnalysisSettings::Type::OperatingPoint;
            else if (head == u".tran" && t.size() >= 3) {
                analysis.type = AnalysisSettings::Type::Transient;
                analysis.tranStep = t[1];
                analysis.tranStop = t[2];
            } else if (head == u".ac" && t.size() >= 5) {
                analysis.type = AnalysisSettings::Type::AC;
                analysis.acPointsPerDecade = t[2].toInt();
                analysis.acStart = t[3];
                analysis.acStop = t[4];
            } else if (head == u".dc" && t.size() >= 5) {
                analysis.type = AnalysisSettings::Type::DCSweep;
                analysis.dcSource = t[1].toUpper();
                analysis.dcStart = t[2];
                analysis.dcStop = t[3];
                analysis.dcStep = t[4];
            } else if (head != u".model" && head != u".options" && head != u".option" && head != u".print"
                       && head != u".plot" && head != u".probe" && head != u".control" && head != u".endc") {
                rep.skipped << l;
            }
            continue;
        }

        const QChar kind = head.at(0);
        QString symbol;
        int nodes = 2;
        QString label;
        QVariantMap props;
        switch (kind.unicode()) {
        case u'r': symbol = QStringLiteral("resistor"); label = t.value(3); break;
        case u'c': symbol = QStringLiteral("capacitor"); label = t.value(3); break;
        case u'l': symbol = QStringLiteral("inductor"); label = t.value(3); break;
        case u'd': symbol = QStringLiteral("diode"); label = t.value(3); break;
        case u'q':
            nodes = 3;
            label = t.value(4);
            symbol = modelType.value(label.toLower()) == u"pnp" ? QStringLiteral("pnp") : QStringLiteral("npn");
            break;
        case u'm':
            nodes = 3;                                      // D G S; the bulk is the source here
            label = t.value(5);
            symbol = modelType.value(label.toLower()) == u"pmos" ? QStringLiteral("pmos") : QStringLiteral("nmos");
            break;
        case u'j': nodes = 3; symbol = QStringLiteral("njfet"); label = t.value(4); break;
        case u'v':
        case u'i': {
            // DC value, or a SIN/PULSE source as the signal generator
            QString dc = QStringLiteral("0");
            for (int i = 3; i < t.size(); ++i) {
                const QString up = t[i].toUpper();
                if (up == u"DC" && i + 1 < t.size()) dc = t[++i];
                else if (up == u"AC") { i += 1; }
                else if (up.startsWith(u"SIN(")) {
                    const QStringList a = args(t[i]);
                    symbol = QStringLiteral("siggen");
                    props = {{QStringLiteral("wave"), QStringLiteral("sine")}, {QStringLiteral("offset"), a.value(0, u"0"_s)},
                             {QStringLiteral("amplitude"), a.value(1, u"1"_s)}, {QStringLiteral("frequency"), a.value(2, u"1k"_s)},
                             {QStringLiteral("delay"), a.value(3, u"0"_s)}, {QStringLiteral("damping"), a.value(4, u"0"_s)}};
                } else if (up.startsWith(u"PULSE(")) {
                    const QStringList a = args(t[i]);
                    symbol = QStringLiteral("siggen");
                    props = {{QStringLiteral("wave"), QStringLiteral("pulse")}, {QStringLiteral("v1"), a.value(0, u"0"_s)},
                             {QStringLiteral("v2"), a.value(1, u"5"_s)}, {QStringLiteral("delay"), a.value(2, u"0"_s)},
                             {QStringLiteral("rise"), a.value(3, u"1n"_s)}, {QStringLiteral("fall"), a.value(4, u"1n"_s)},
                             {QStringLiteral("width"), a.value(5, u"0.5m"_s)}, {QStringLiteral("period"), a.value(6, u"1m"_s)}};
                } else if (i == 3 && !up.contains(u'(')) {
                    dc = t[i];
                }
            }
            if (symbol.isEmpty()) {
                symbol = kind == u'v' ? QStringLiteral("vsource") : QStringLiteral("isource");
                label = dc;
            } else if (kind == u'i') {
                props.insert(QStringLiteral("current"), true);
            }
            break;
        }
        default:
            rep.skipped << l;                               // X, E, F, G, H, B, K, T...
            continue;
        }
        const SymbolDef* s = library.find(symbol);
        if (!s || t.size() < 1 + nodes) {
            rep.skipped << l;
            continue;
        }
        Component c;
        c.symbolId = symbol;
        c.designator = t[0].toUpper();
        c.label = label;
        c.props = props;
        // the netlist's own model for this part
        if (const QString m = modelText.value(label.toLower()); !m.isEmpty()) c.props.insert(QStringLiteral("spiceModels"), m);
        // six parts a row, 300 apart
        c.pos = QPointF((count % 6) * 300, (count / 6) * 300);
        const ItemId id = circuit.addComponent(c);
        ++count;
        const SymbolDef* terminal = library.find(QStringLiteral("terminal"));
        const SymbolDef* ground = library.find(QStringLiteral("ground"));
        for (int i = 0; i < nodes && i < s->pins.size(); ++i) {
            const QString node = t[1 + i];
            const QPointF at = library.pinScenePos(*circuit.component(id), i);
            Component tie;
            if (node == u"0" || node.compare(u"gnd", Qt::CaseInsensitive) == 0) {
                tie.symbolId = QStringLiteral("ground");
                tie.pos = at - ground->pins[0].pos;
            } else {
                tie.symbolId = QStringLiteral("terminal");
                tie.label = node;
                tie.pos = at - terminal->pins[0].pos;
            }
            circuit.addComponent(tie);
        }
    }
    circuit.setAnalysis(analysis);
    rep.components = count;
    if (report) *report = rep;
    if (count == 0) {
        if (error) *error = QStringLiteral("no parts this program can place");
        return false;
    }
    return true;
}

} // namespace cm::SpiceImporter
