#pragma once

#include <QHash>
#include <QString>

namespace cm {

// Device hotkeys as in CircuitMaker: pressing 0-9, a-z or A-Z on the
// schematic picks the device assigned to that key for placement.
namespace Hotkeys {

QHash<QChar, QString> load();               // key -> symbol id
void save(const QHash<QChar, QString>& map);
QString symbolFor(QChar key);
void assign(QChar key, const QString& symbolId); // empty id clears the key
QChar keyFor(const QString& symbolId);      // QChar() when none
bool isHotkey(QChar key);

} // namespace Hotkeys

} // namespace cm
