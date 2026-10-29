#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace cm {

Preferences Preferences::load()
{
    QSettings s;
    Preferences p;
    p.language = s.value(QStringLiteral("prefs/language")).toString();
    p.gridVisible = s.value(QStringLiteral("prefs/gridVisible"), p.gridVisible).toBool();
    p.gridStyle = s.value(QStringLiteral("prefs/gridStyle")).toString() == u"lines" ? GridStyle::Lines : GridStyle::Dots;
    p.zoomPercent = std::clamp(s.value(QStringLiteral("prefs/zoomPercent"), p.zoomPercent).toInt(), 20, 400);
    p.ticksPerFrame = std::clamp(s.value(QStringLiteral("prefs/ticksPerFrame"), p.ticksPerFrame).toInt(), 1, 1000);
    return p;
}

void Preferences::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("prefs/language"), language);
    s.setValue(QStringLiteral("prefs/gridVisible"), gridVisible);
    s.setValue(QStringLiteral("prefs/gridStyle"), gridStyle == GridStyle::Lines ? QStringLiteral("lines") : QStringLiteral("dots"));
    s.setValue(QStringLiteral("prefs/zoomPercent"), zoomPercent);
    s.setValue(QStringLiteral("prefs/ticksPerFrame"), ticksPerFrame);
}

PreferencesDialog::PreferencesDialog(const Preferences& prefs, QWidget* parent)
    : QDialog(parent), m_initialLanguage(prefs.language)
{
    setWindowTitle(tr("Preferences"));
    auto* layout = new QVBoxLayout(this);

    auto* general = new QGroupBox(tr("General"));
    auto* gf = new QFormLayout(general);
    m_language = new QComboBox;
    m_language->addItem(tr("System default"), QString());
    m_language->addItem(QStringLiteral("Português (Brasil)"), QStringLiteral("pt_BR"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->setCurrentIndex(std::max(0, m_language->findData(prefs.language)));
    gf->addRow(tr("&Language:"), m_language);
    auto* note = new QLabel(tr("A language change takes effect after restarting."));
    note->setEnabled(false);
    gf->addRow(note);
    layout->addWidget(general);

    auto* drawing = new QGroupBox(tr("Schematic"));
    auto* df = new QFormLayout(drawing);
    m_grid = new QCheckBox(tr("Show grid"));
    m_grid->setChecked(prefs.gridVisible);
    df->addRow(m_grid);
    m_gridStyle = new QComboBox;
    m_gridStyle->addItem(tr("Dots"));
    m_gridStyle->addItem(tr("Lines"));
    m_gridStyle->setCurrentIndex(prefs.gridStyle == Preferences::GridStyle::Lines ? 1 : 0);
    df->addRow(tr("Grid &style:"), m_gridStyle);
    m_zoom = new QSpinBox;
    m_zoom->setRange(20, 400);
    m_zoom->setSingleStep(10);
    m_zoom->setSuffix(QStringLiteral(" %"));
    m_zoom->setValue(prefs.zoomPercent);
    m_zoom->setToolTip(tr("Zoom used for new and opened circuits; \"Fit to window\" never enlarges beyond it"));
    df->addRow(tr("Default &zoom:"), m_zoom);
    layout->addWidget(drawing);

    auto* sim = new QGroupBox(tr("Digital simulation"));
    auto* sf = new QFormLayout(sim);
    m_ticks = new QSpinBox;
    m_ticks->setRange(1, 1000);
    m_ticks->setSuffix(tr(" ticks/frame"));
    m_ticks->setValue(prefs.ticksPerFrame);
    sf->addRow(tr("Initial &speed:"), m_ticks);
    layout->addWidget(sim);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

Preferences PreferencesDialog::result() const
{
    Preferences p;
    p.language = m_language->currentData().toString();
    p.gridVisible = m_grid->isChecked();
    p.gridStyle = m_gridStyle->currentIndex() == 1 ? Preferences::GridStyle::Lines : Preferences::GridStyle::Dots;
    p.zoomPercent = m_zoom->value();
    p.ticksPerFrame = m_ticks->value();
    return p;
}

} // namespace cm
