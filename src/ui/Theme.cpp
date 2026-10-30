#include "ui/Theme.h"

#include <QSettings>

namespace cm {

namespace {

Theme& instance()
{
    static Theme t = Theme::load();
    return t;
}

struct Entry { const char* key; QColor Theme::*color; };
const Entry kColors[] = {
    {"background", &Theme::background}, {"grid", &Theme::grid}, {"device", &Theme::device},
    {"wire", &Theme::wire}, {"bus", &Theme::bus}, {"text", &Theme::text},
    {"selection", &Theme::selection}, {"high", &Theme::high}, {"low", &Theme::low},
};

} // namespace

const Theme& Theme::current() { return instance(); }

void Theme::setCurrent(const Theme& theme)
{
    instance() = theme;
    theme.save();
}

Theme Theme::load()
{
    Theme t;
    QSettings s;
    for (const Entry& e : kColors) {
        const QColor c(s.value(QStringLiteral("colors/%1").arg(QLatin1String(e.key))).toString());
        if (c.isValid()) t.*e.color = c;
    }
    t.deviceFont = s.value(QStringLiteral("fonts/device"), t.deviceFont).toString();
    t.textFont = s.value(QStringLiteral("fonts/text"), t.textFont).toString();
    return t;
}

void Theme::save() const
{
    QSettings s;
    for (const Entry& e : kColors)
        s.setValue(QStringLiteral("colors/%1").arg(QLatin1String(e.key)), (this->*e.color).name());
    s.setValue(QStringLiteral("fonts/device"), deviceFont);
    s.setValue(QStringLiteral("fonts/text"), textFont);
}

} // namespace cm
