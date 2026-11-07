#include "ui/PropertiesDialog.h"

#include "core/Faults.h"
#include "library/LibraryRegistry.h"
#include "library/SymbolDef.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
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
    if (key == u"color") return PropertiesDialog::tr("Colour");
    if (key == u"delay") return PropertiesDialog::tr("Propagation delay (ticks)");
    if (key == u"inverted") return PropertiesDialog::tr("Inverted pulse format");
    if (key == u"trigger") return PropertiesDialog::tr("External trigger (one-shot on CP1/CP2)");
    if (key == u"distance") return PropertiesDialog::tr("Travel distance");
    if (key == u"speed") return PropertiesDialog::tr("Speed");
    return key;
}

QString choiceCaption(const QString& value)
{
    if (value == u"red") return PropertiesDialog::tr("Red");
    if (value == u"green") return PropertiesDialog::tr("Green");
    if (value == u"yellow") return PropertiesDialog::tr("Yellow");
    if (value == u"blue") return PropertiesDialog::tr("Blue");
    if (value == u"orange") return PropertiesDialog::tr("Orange");
    if (value == u"white") return PropertiesDialog::tr("White");
    return value;
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
    // CircuitMaker's Visible boxes; unchecked/checked override the symbol's default
    auto* visible = new QHBoxLayout;
    m_showDesignator = new QCheckBox(tr("Show d&esignator"));
    m_showLabel = new QCheckBox(tr("Show la&bel"));
    m_designatorDefault = !symbol.netLabel && symbol.model.value(u"kind").toString() != u"const";
    m_labelDefault = symbol.netLabel || symbol.prefix != u"U";
    m_showDesignator->setChecked(c.showDesignator.value_or(m_designatorDefault));
    m_showLabel->setChecked(c.showLabel.value_or(m_labelDefault));
    visible->addWidget(m_showDesignator);
    visible->addWidget(m_showLabel);
    form->addRow(visible);

    QVariantMap props = symbol.defaultProps;
    for (auto it = c.props.cbegin(); it != c.props.cend(); ++it)
        props.insert(it.key(), it.value());
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        const QVariant& v = it.value();
        if (const QStringList choices = symbol.propChoices.value(it.key()); !choices.isEmpty()) {
            auto* combo = new QComboBox;
            for (const QString& c : choices) combo->addItem(choiceCaption(c), c);
            combo->setCurrentIndex(std::max(0, combo->findData(v.toString())));
            m_choiceProps.insert(it.key(), combo);
            form->addRow(propertyCaption(it.key()) + u':', combo);
            continue;
        }
        if (it.key() == u"travel" || faults::isFaultProperty(it.key()) || it.key() == u"break"
            || it.key() == u"probe"
            || it.key() == u"package" || it.key() == u"excludePcb" || it.key() == u"excludeBom"
            || it.key() == u"pins" || it.key() == u"spice" || it.key() == u"spiceModels" || it.key() == u"cmDevice"
            || it.key() == u"cmRef" || it.key() == u"cmSpice"
            || it.key() == u"cmModelAt" || it.key() == u"cmPackage" || it.key() == u"cmPins" || it.key() == u"cmFlags" || it.key() == u"cmKind" || it.key() == u"cmStamp" || it.key() == u"spiceTail" || it.key() == u"cmPackageTable" || it.key() == u"cmPower" || it.key() == u"cmCode" || it.key() == u"cmNetsTail" || it.key() == u"cmSpan" || it.key() == u"cmAt" || it.key() == u"cmValueBox" || it.key() == u"cmDesignatorBox" || it.key() == u"cmTurnShift" || it.key() == u"cmTag" || it.key() == u"cmGenKey" || it.key() == u"cmGenLabel" || it.key() == u"cmTagAt" || it.key() == u"pwlScale")
            continue;
        if (it.key() == u"data" && (symbol.model.value(u"kind").toString() == u"ram" || symbol.model.value(u"kind").toString() == u"rom"))
            continue;       // edited with Edit PROM/RAM
        if (v.typeId() == QMetaType::Bool) {
            auto* box = new QCheckBox(propertyCaption(it.key()));
            box->setChecked(v.toBool());
            m_boolProps.insert(it.key(), box);
            form->addRow(box);
        } else if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong || v.typeId() == QMetaType::Double) {
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

    if (!symbol.netLabel) {
        auto* pcb = new QGroupBox(tr("PCB and parts list"));
        auto* pf = new QFormLayout(pcb);
        m_package = new QLineEdit(c.props.value(QStringLiteral("package")).toString());
        m_package->setPlaceholderText(symbol.package.isEmpty() ? symbol.id : symbol.package);
        pf->addRow(tr("&Package (footprint):"), m_package);
        m_pins = c.props.value(QStringLiteral("pins")).toString();
        auto* pinsButton = new QPushButton(tr("P&in numbers..."));
        pinsButton->setToolTip(tr("Package pin numbers used by the PCB netlist"));
        connect(pinsButton, &QPushButton::clicked, this, [this, &symbol] { editPins(symbol); });
        pf->addRow(pinsButton);
        m_excludePcb = new QCheckBox(tr("Exclude from the &PCB netlist"));
        m_excludePcb->setChecked(c.props.value(QStringLiteral("excludePcb")).toBool());
        m_excludeBom = new QCheckBox(tr("Exclude from the parts &list (BOM)"));
        m_excludeBom->setChecked(c.props.value(QStringLiteral("excludeBom")).toBool());
        pf->addRow(m_excludePcb);
        pf->addRow(m_excludeBom);
        form->addRow(pcb);

        // CircuitMaker's Edit/Select Spice Model: the part's own netlist line
        auto* spice = new QGroupBox(tr("SPICE"));
        auto* sf = new QFormLayout(spice);
        m_spiceLine = new QLineEdit(c.props.value(QStringLiteral("spice")).toString());
        const QString builtIn = symbol.spice.value(u"line").toString();
        m_spiceLine->setPlaceholderText(builtIn.isEmpty() ? tr("(generated)") : builtIn.section(u'\n', 0, 0));
        m_spiceLine->setToolTip(tr("Replaces the part's SPICE line. {name} is its name, {value} its label, {pin} the node "
                                   "of a pin (e.g. {1}, {A}), {prop:x} a property; the first letter is the SPICE prefix "
                                   "(X for a subcircuit)"));
        sf->addRow(tr("&Line:"), m_spiceLine);
        m_spiceModels = new QPlainTextEdit(c.props.value(QStringLiteral("spiceModels")).toString());
        m_spiceModels->setPlaceholderText(tr(".model / .subckt ... .ends lines this part needs"));
        m_spiceModels->setMaximumHeight(80);
        sf->addRow(tr("&Models:"), m_spiceModels);
        form->addRow(spice);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

// CircuitMaker's Edit Pin Data: one package pin number per symbol pin
void PropertiesDialog::editPins(const SymbolDef& symbol)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Pin numbers"));
    auto* layout = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(int(symbol.pins.size()), 2);
    table->setHorizontalHeaderLabels({tr("Pin"), tr("Package pin")});
    table->horizontalHeader()->setStretchLastSection(true);
    Component c = m_c;
    c.props.insert(QStringLiteral("pins"), m_pins);
    for (int i = 0; i < symbol.pins.size(); ++i) {
        auto* name = new QTableWidgetItem(symbol.pins[i].name);
        name->setFlags(Qt::ItemIsEnabled);
        table->setItem(i, 0, name);
        table->setItem(i, 1, new QTableWidgetItem(packagePin(symbol, c, i)));
    }
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    // only the numbers that differ from the symbol's are kept
    Component plain = m_c;
    plain.props.remove(QStringLiteral("pins"));
    QStringList pairs;
    for (int i = 0; i < symbol.pins.size(); ++i) {
        const QString n = table->item(i, 1)->text().trimmed();
        if (!n.isEmpty() && n != packagePin(symbol, plain, i)) pairs << symbol.pins[i].name + u'=' + n;
    }
    m_pins = pairs.join(u',');
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
    for (auto it = m_boolProps.cbegin(); it != m_boolProps.cend(); ++it)
        c.props.insert(it.key(), it.value()->isChecked());
    for (auto it = m_choiceProps.cbegin(); it != m_choiceProps.cend(); ++it)
        c.props.insert(it.key(), it.value()->currentData().toString());
    // only store what differs from the symbol's default
    if (m_c.showDesignator || m_showDesignator->isChecked() != m_designatorDefault)
        c.showDesignator = m_showDesignator->isChecked();
    if (m_c.showLabel || m_showLabel->isChecked() != m_labelDefault)
        c.showLabel = m_showLabel->isChecked();
    auto flag = [&](QCheckBox* box, const QString& key) {
        if (!box) return;
        if (box->isChecked()) c.props.insert(key, true);
        else c.props.remove(key);
    };
    flag(m_excludePcb, QStringLiteral("excludePcb"));
    flag(m_excludeBom, QStringLiteral("excludeBom"));
    if (m_spiceLine) {
        const QString line = m_spiceLine->text().trimmed(), models = m_spiceModels->toPlainText().trimmed();
        if (line.isEmpty()) c.props.remove(QStringLiteral("spice"));
        else c.props.insert(QStringLiteral("spice"), line);
        if (models.isEmpty()) c.props.remove(QStringLiteral("spiceModels"));
        else c.props.insert(QStringLiteral("spiceModels"), models);
    }
    if (m_pins.isEmpty()) c.props.remove(QStringLiteral("pins"));
    else c.props.insert(QStringLiteral("pins"), m_pins);
    if (m_package) {
        if (m_package->text().trimmed().isEmpty()) c.props.remove(QStringLiteral("package"));
        else c.props.insert(QStringLiteral("package"), m_package->text().trimmed());
    }
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
