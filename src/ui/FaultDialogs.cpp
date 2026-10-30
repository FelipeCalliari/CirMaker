#include "ui/FaultDialogs.h"

#include "library/SymbolDef.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace cm {

// ------------------------------------------------------------ device faults

DeviceFaultsDialog::DeviceFaultsDialog(const Component& c, const SymbolDef& symbol, QWidget* parent)
    : QDialog(parent), m_f(faults::read(c))
{
    setWindowTitle(tr("Device faults: %1").arg(c.designator.isEmpty() ? symbol.name : c.designator));
    for (const SymbolPin& p : symbol.pins)
        if (!p.name.isEmpty() && !m_pins.contains(p.name)) m_pins << p.name;
    m_assigned[High] = m_f.high;
    m_assigned[Low] = m_f.low;
    m_assigned[Open] = m_f.open;
    m_assigned[Short] = m_f.shorted;

    auto* top = new QVBoxLayout(this);
    m_enabled = new QCheckBox(tr("&Enable device faults"));
    m_enabled->setChecked(m_f.enabled);
    top->addWidget(m_enabled);

    auto* row = new QHBoxLayout;
    auto* pinsBox = new QGroupBox(tr("Device pins"));
    auto* pinsLayout = new QVBoxLayout(pinsBox);
    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pinsLayout->addWidget(m_list);
    row->addWidget(pinsBox);

    auto* faultsBox = new QGroupBox(tr("Faults"));
    auto* grid = new QGridLayout(faultsBox);
    const QString names[KindCount] = {tr("&HIGH"), tr("&LOW"), tr("&OPEN"), tr("&SHORT")};
    const QString tips[KindCount] = {tr("Pins stuck high"), tr("Pins stuck low"), tr("Pins disconnected from the device"),
                                     tr("Pins shorted together (at least two)")};
    for (int k = 0; k < KindCount; ++k) {
        auto* button = new QPushButton(names[k]);
        button->setToolTip(tips[k] + u'\n' + tr("Select pins and press to add; press with no selection to remove"));
        connect(button, &QPushButton::clicked, this, [this, k] { toggle(Kind(k)); });
        m_labels[k] = new QLabel;
        m_labels[k]->setMinimumWidth(160);
        m_labels[k]->setWordWrap(true);
        grid->addWidget(button, k, 0);
        grid->addWidget(m_labels[k], k, 1);
    }
    grid->setRowStretch(KindCount, 1);
    row->addWidget(faultsBox, 1);
    top->addLayout(row);

    auto* form = new QFormLayout;
    m_value = new QLineEdit(m_f.value);
    m_value->setToolTip(tr("Replaces the device's Label-Value while simulating (wrong value, faulty SPICE model)"));
    form->addRow(tr("Fault &Label-Value:"), m_value);
    m_hint = new QPlainTextEdit(m_f.hint);
    m_hint->setMaximumHeight(70);
    form->addRow(tr("&Hint:"), m_hint);
    top->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);
    refresh();
}

void DeviceFaultsDialog::selectPins(const QStringList& names)
{
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setSelected(names.contains(m_list->item(i)->text()));
}

void DeviceFaultsDialog::toggle(Kind kind)
{
    QStringList selected;
    for (QListWidgetItem* it : m_list->selectedItems()) selected << it->text();
    if (selected.isEmpty()) {
        m_assigned[kind].clear();
    } else {
        for (const QString& p : std::as_const(m_pins))      // keep pin order
            if (selected.contains(p)) m_assigned[kind] << p;
        m_enabled->setChecked(true);    // editing fault data enables it, as in CircuitMaker
    }
    refresh();
}

void DeviceFaultsDialog::refresh()
{
    QStringList used;
    for (const QStringList& a : m_assigned) used += a;
    m_list->clear();
    for (const QString& p : std::as_const(m_pins))
        if (!used.contains(p)) m_list->addItem(p);
    for (int k = 0; k < KindCount; ++k)
        m_labels[k]->setText(m_assigned[k].isEmpty() ? QStringLiteral("—") : m_assigned[k].join(QStringLiteral(", ")));
}

faults::DeviceFaults DeviceFaultsDialog::result() const
{
    faults::DeviceFaults f = m_f;
    f.enabled = m_enabled->isChecked();
    f.high = m_assigned[High];
    f.low = m_assigned[Low];
    f.open = m_assigned[Open];
    f.shorted = m_assigned[Short];
    f.value = m_value->text().trimmed();
    f.hint = m_hint->toPlainText().trimmed();
    return f;
}

// ------------------------------------------------------------ circuit faults

QList<std::pair<QString, QString>> CircuitFaultsDialog::options()
{
    return {
        {QStringLiteral("edit"), tr("Cut/Copy/Paste/Move")},
        {QStringLiteral("delete"), tr("Delete tool")},
        {QStringLiteral("wire"), tr("Wire tool")},
        {QStringLiteral("library"), tr("Device library")},
        {QStringLiteral("properties"), tr("Device properties")},
        {QStringLiteral("replace"), tr("Device replacement")},
        {QStringLiteral("replaceStatus"), tr("Replacement status")},
        {QStringLiteral("hints"), tr("Display hint")},
        {QStringLiteral("nodeNames"), tr("Show node names")},
        {QStringLiteral("trace"), tr("Digital trace")},
    };
}

CircuitFaultsDialog::CircuitFaultsDialog(const FaultSettings& settings, Counts counts, QWidget* parent)
    : QDialog(parent), m_s(settings)
{
    setWindowTitle(tr("Circuit fault data"));
    auto* top = new QVBoxLayout(this);

    auto* lock = new QGroupBox(tr("Fault lock password"));
    auto* lockForm = new QFormLayout(lock);
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMaxLength(15);
    m_password->setPlaceholderText(settings.locked() ? tr("(unchanged)") : tr("(none)"));
    m_password->setToolTip(tr("While a password is set, students need it to see or change fault data, "
                              "and the options checked below are disabled"));
    lockForm->addRow(tr("&New password:"), m_password);
    m_unlock = new QCheckBox(tr("&Remove the password"));
    m_unlock->setVisible(settings.locked());
    lockForm->addRow(m_unlock);
    top->addWidget(lock);

    auto* disable = new QGroupBox(tr("Disable circuit options (while locked)"));
    auto* grid = new QGridLayout(disable);
    int i = 0;
    for (const auto& [key, caption] : options()) {
        auto* box = new QCheckBox(caption);
        box->setChecked(settings.disabled.contains(key));
        m_options.insert(key, box);
        grid->addWidget(box, i / 2, i % 2);
        ++i;
    }
    top->addWidget(disable);

    auto* values = new QGroupBox(tr("Analog default values"));
    auto* valuesForm = new QFormLayout(values);
    m_high = new QLineEdit(settings.high);
    m_low = new QLineEdit(settings.low);
    m_open = new QLineEdit(settings.open);
    m_short = new QLineEdit(settings.shorted);
    valuesForm->addRow(tr("HIGH source (V):"), m_high);
    valuesForm->addRow(tr("LOW source (V):"), m_low);
    valuesForm->addRow(tr("OPEN resistance (Ω):"), m_open);
    valuesForm->addRow(tr("SHORT resistance (Ω):"), m_short);
    top->addWidget(values);

    auto* marks = new QGroupBox(tr("Hints and replacements"));
    auto* marksLayout = new QGridLayout(marks);
    marksLayout->addWidget(new QLabel(tr("Devices with faults: %1").arg(counts.faulty)), 0, 0);
    marksLayout->addWidget(new QLabel(tr("Devices replaced: %1").arg(counts.replaced)), 1, 0);
    marksLayout->addWidget(new QLabel(tr("Hints displayed: %1").arg(counts.hinted)), 2, 0);
    const std::pair<QString, int> selects[] = {{tr("Select &faulty devices"), SelectFaulty},
                                               {tr("Select re&placed devices"), SelectReplaced},
                                               {tr("Select &hinted devices"), SelectHinted}};
    int r = 0;
    for (const auto& [text, code] : selects) {
        auto* b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, [this, code] { done(code); });
        marksLayout->addWidget(b, r++, 1);
    }
    m_clear = new QCheckBox(tr("&Clear hints/replacements"));
    marksLayout->addWidget(m_clear, 3, 0, 1, 2);
    top->addWidget(marks);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);
}

FaultSettings CircuitFaultsDialog::result() const
{
    FaultSettings s = m_s;
    if (m_unlock->isChecked())
        s.password.clear();
    if (!m_password->text().isEmpty())
        s.password = faults::hashPassword(m_password->text());
    s.disabled.clear();
    for (const auto& [key, caption] : options())
        if (m_options.value(key)->isChecked()) s.disabled << key;
    s.high = m_high->text().trimmed();
    s.low = m_low->text().trimmed();
    s.open = m_open->text().trimmed();
    s.shorted = m_short->text().trimmed();
    return s;
}

bool CircuitFaultsDialog::clearMarks() const { return m_clear->isChecked(); }

// ------------------------------------------------------------ access faults

AccessFaultsDialog::AccessFaultsDialog(const QString& device, bool hintAllowed, bool replaceAllowed,
                                       const QString& passwordHash, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Access faults: %1").arg(device));
    auto* top = new QVBoxLayout(this);
    top->addWidget(new QLabel(tr("The fault data of this circuit is password protected.")));
    auto* form = new QFormLayout;
    auto* password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("&Password:"), password);
    top->addLayout(form);
    auto* buttons = new QDialogButtonBox;
    QPushButton* unlock = buttons->addButton(tr("&Unlock"), QDialogButtonBox::AcceptRole);
    connect(unlock, &QPushButton::clicked, this, [this, password, passwordHash] {
        if (faults::hashPassword(password->text()) == passwordHash) {
            done(Unlocked);
        } else {
            QMessageBox::warning(this, windowTitle(), tr("Wrong password."));
            password->clear();
        }
    });
    if (hintAllowed) {
        QPushButton* hint = buttons->addButton(tr("Show &Hint"), QDialogButtonBox::ActionRole);
        connect(hint, &QPushButton::clicked, this, [this] { done(ShowHint); });
    }
    if (replaceAllowed) {
        QPushButton* replace = buttons->addButton(tr("&Replace Device"), QDialogButtonBox::ActionRole);
        replace->setToolTip(tr("Swaps the device for a good one (its faults stop being simulated)"));
        connect(replace, &QPushButton::clicked, this, [this] { done(Replace); });
    }
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    unlock->setDefault(true);
    top->addWidget(buttons);
}

} // namespace cm
