#include "ui/PropertiesDialog.h"

#include "library/SymbolDef.h"

#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

namespace cm {

namespace {

QString propertyCaption(const QString& key)
{
    if (key == u"high") return PropertiesDialog::tr("High time (ticks)");
    if (key == u"low") return PropertiesDialog::tr("Low time (ticks)");
    if (key == u"state") return PropertiesDialog::tr("Initial state (0/1)");
    if (key == u"closed") return PropertiesDialog::tr("Closed (0/1)");
    if (key == u"value") return PropertiesDialog::tr("Value");
    return key;
}

} // namespace

PropertiesDialog::PropertiesDialog(const Component& c, const SymbolDef& symbol, QWidget* parent)
    : QDialog(parent), m_c(c)
{
    setWindowTitle(tr("%1 properties").arg(symbol.name));
    auto* form = new QFormLayout(this);
    if (!symbol.description.isEmpty()) {
        auto* desc = new QLabel(symbol.description);
        desc->setWordWrap(true);
        form->addRow(desc);
    }
    m_designator = new QLineEdit(c.designator);
    form->addRow(tr("&Designator:"), m_designator);
    m_label = new QLineEdit(c.label);
    form->addRow(symbol.netLabel ? tr("&Net name:") : tr("&Label:"), m_label);

    QVariantMap props = symbol.defaultProps;
    for (auto it = c.props.cbegin(); it != c.props.cend(); ++it)
        props.insert(it.key(), it.value());
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        const QVariant& v = it.value();
        if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong || v.typeId() == QMetaType::Double) {
            auto* spin = new QSpinBox;
            spin->setRange(0, 1000000);
            spin->setValue(v.toInt());
            m_intProps.insert(it.key(), spin);
            form->addRow(propertyCaption(it.key()) + u':', spin);
        } else {
            auto* edit = new QLineEdit(v.toString());
            m_textProps.insert(it.key(), edit);
            form->addRow(propertyCaption(it.key()) + u':', edit);
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

Component PropertiesDialog::result() const
{
    Component c = m_c;
    c.designator = m_designator->text().trimmed();
    c.label = m_label->text().trimmed();
    for (auto it = m_intProps.cbegin(); it != m_intProps.cend(); ++it)
        c.props.insert(it.key(), it.value()->value());
    for (auto it = m_textProps.cbegin(); it != m_textProps.cend(); ++it)
        c.props.insert(it.key(), it.value()->text());
    return c;
}

TitleBlockDialog::TitleBlockDialog(const TitleBlock& tb, QWidget* parent) : QDialog(parent), m_tb(tb)
{
    setWindowTitle(tr("Title block"));
    auto* form = new QFormLayout(this);
    m_title = new QLineEdit(tb.title);
    m_author = new QLineEdit(tb.author);
    m_date = new QLineEdit(tb.date.isEmpty() ? QLocale().toString(QDate::currentDate(), QLocale::ShortFormat) : tb.date);
    m_revision = new QLineEdit(tb.revision);
    m_title->setMinimumWidth(320);
    form->addRow(tr("&Title:"), m_title);
    form->addRow(tr("&Author:"), m_author);
    form->addRow(tr("&Date:"), m_date);
    form->addRow(tr("&Revision:"), m_revision);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

TitleBlock TitleBlockDialog::result() const
{
    TitleBlock tb = m_tb;
    tb.title = m_title->text();
    tb.author = m_author->text();
    tb.date = m_date->text();
    tb.revision = m_revision->text();
    return tb;
}

} // namespace cm
