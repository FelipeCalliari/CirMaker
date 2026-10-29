#include "ui/Hotkeys.h"

#include <QSettings>

namespace cm::Hotkeys {

namespace {

// Starting assignments: the devices used most in digital course circuits.
// CircuitMaker shipped without assignments; users set their own.
const QHash<QChar, QString>& defaults()
{
    static const QHash<QChar, QString> map = {
        {u'1', QStringLiteral("logicswitch")}, {u'2', QStringLiteral("logicdisplay")},
        {u'3', QStringLiteral("inv")},         {u'4', QStringLiteral("and2")},
        {u'5', QStringLiteral("or2")},         {u'6', QStringLiteral("dff")},
        {u'7', QStringLiteral("jkff")},        {u'8', QStringLiteral("pulser")},
        {u'9', QStringLiteral("hexdisplay")},  {u'0', QStringLiteral("ground")},
        {u'v', QStringLiteral("vcc")},         {u't', QStringLiteral("terminal")},
        {u'r', QStringLiteral("resistor")},
    };
    return map;
}

} // namespace

bool isHotkey(QChar key)
{
    return key.unicode() < 128 && key.isLetterOrNumber();
}

QHash<QChar, QString> load()
{
    QSettings s;
    if (!s.contains(QStringLiteral("hotkeys/assigned")))
        return defaults();
    QHash<QChar, QString> map;
    s.beginGroup(QStringLiteral("hotkeys"));
    for (const QString& key : s.childKeys()) {
        // keys are stored as code points: settings keys are case-insensitive on some platforms
        bool ok = false;
        const int code = key.toInt(&ok);
        if (ok && code > 0)
            map.insert(QChar(code), s.value(key).toString());
    }
    s.endGroup();
    return map;
}

void save(const QHash<QChar, QString>& map)
{
    QSettings s;
    s.remove(QStringLiteral("hotkeys"));
    s.beginGroup(QStringLiteral("hotkeys"));
    s.setValue(QStringLiteral("assigned"), true);
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        if (!it.value().isEmpty())
            s.setValue(QString::number(it.key().unicode()), it.value());
    s.endGroup();
}

QString symbolFor(QChar key) { return load().value(key); }

void assign(QChar key, const QString& symbolId)
{
    QHash<QChar, QString> map = load();
    for (auto it = map.begin(); it != map.end();)       // one key per device
        it = it.value() == symbolId ? map.erase(it) : std::next(it);
    if (symbolId.isEmpty()) map.remove(key);
    else map.insert(key, symbolId);
    save(map);
}

QChar keyFor(const QString& symbolId)
{
    const QHash<QChar, QString> map = load();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        if (it.value() == symbolId) return it.key();
    return {};
}

} // namespace cm::Hotkeys
