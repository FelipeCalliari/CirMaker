#include "core/io/CmModels.h"

#include "core/io/CmDeviceLib.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>

namespace cm::cmmodels {

namespace {

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
        for (QString line : QString::fromLatin1(f.readAll()).split(u'\n')) {
            line.remove(u'\r');
            const QString t = line.trimmed();
            if (t.startsWith(u'+') && !statements.isEmpty()) statements.last() += u' ' + t.mid(1).trimmed();
            else if (!t.isEmpty() && !t.startsWith(u'*')) statements << t;
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

Model find(const QString& name)
{
    static const QHash<QString, Model> models = index();
    return models.value(name.trimmed().toUpper());
}

} // namespace cm::cmmodels
