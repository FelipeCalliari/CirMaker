#include "core/io/CmModels.h"

#include "core/io/CmDeviceLib.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

namespace cm::cmmodels {

namespace {

QHash<QString, Subckt>& subckts()
{
    static QHash<QString, Subckt> s;
    return s;
}

QHash<QString, Model> index()
{
    QHash<QString, Model> out;
    const QString lib = CmDeviceLib::locate(QSettings().value(QStringLiteral("cm/folder")).toString());
    if (lib.isEmpty()) return out;
    const QDir dir(QFileInfo(lib).absolutePath() + QStringLiteral("/Models"));
    static const QRegularExpression head(QStringLiteral("^\\.MODEL\\s+(\\S+)\\s+([A-Za-z]+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.mod"), QStringLiteral("*.MOD"), QStringLiteral("*.lib"), QStringLiteral("*.LIB")},
                                                 QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        // statements with their "+" continuation lines
        QStringList statements;
        QSet<int> packaged;                     // statements after a "pkg:" comment (CircuitMaker's own entries)
        bool pkgComment = false;
        for (QString line : QString::fromLatin1(f.readAll()).split(u'\n')) {
            line.remove(u'\r');
            const QString t = line.trimmed();
            if (t.startsWith(u'*')) {
                if (t.contains(QStringLiteral("pkg:"), Qt::CaseInsensitive)) pkgComment = true;
                continue;
            }
            if (t.startsWith(u'+') && !statements.isEmpty()) statements.last() += u' ' + t.mid(1).trimmed();
            else if (!t.isEmpty()) {
                if (pkgComment) packaged << int(statements.size());
                pkgComment = false;
                statements << t;
            }
        }
        // subcircuits, whole
        static const QRegularExpression subHead(QStringLiteral("^\\.SUBCKT\\s+(\\S+)((?:\\s+[^\\s=]+(?=\\s|$))*)"),
                                                QRegularExpression::CaseInsensitiveOption);
        for (int i = 0; i < statements.size(); ++i) {
            const auto m = subHead.match(statements[i]);
            if (!m.hasMatch()) continue;
            Subckt sc;
            sc.name = m.captured(1);
            sc.nodes = int(m.captured(2).split(u' ', Qt::SkipEmptyParts).size());
            int j = i;
            for (; j < statements.size(); ++j) {
                sc.lines << statements[j];
                if (statements[j].startsWith(QStringLiteral(".ENDS"), Qt::CaseInsensitive)) break;
            }
            // the first one, or the one CircuitMaker describes ("*uA741 ... pkg:DIP8 3,2,7,4,6")
            sc.packaged = packaged.contains(i);
            const auto have = subckts().constFind(sc.name.toUpper());
            if (have == subckts().cend() || (sc.packaged && !have->packaged)) subckts().insert(sc.name.toUpper(), sc);
            i = j;
        }
        for (const QString& st : std::as_const(statements)) {
            const auto m = head.match(st);
            if (!m.hasMatch()) continue;
            QString name = m.captured(1);
            if (name.endsWith(u'~')) name.chop(1);
            const QString key = name.toUpper();
            if (!out.contains(key)) out.insert(key, Model{name, m.captured(2).toUpper(), st.simplified()});
        }
    }
    return out;
}

} // namespace

const QHash<QString, Model>& models()
{
    static const QHash<QString, Model> m = index();
    return m;
}

Model find(const QString& name)
{
    return models().value(name.trimmed().toUpper());
}

Subckt findSubckt(const QString& name)
{
    models();                                   // the index reads the subcircuits too
    Subckt sc = subckts().value(name.trimmed().toUpper());
    if (sc.lines.isEmpty()) return sc;
    // the models it names that it does not define itself
    QSet<QString> local, used;
    static const QRegularExpression localModel(QStringLiteral("^\\.MODEL\\s+(\\S+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& l : std::as_const(sc.lines)) {
        if (const auto m = localModel.match(l); m.hasMatch()) local << m.captured(1).toUpper();
        else if (!l.startsWith(u'.'))
            for (const QString& t : l.split(u' ', Qt::SkipEmptyParts)) used << t.toUpper();
    }
    for (const QString& u : std::as_const(used))
        if (!local.contains(u))
            if (const Model m = find(u); !m.text.isEmpty()) sc.lines << m.text;
    return sc;
}

} // namespace cm::cmmodels
