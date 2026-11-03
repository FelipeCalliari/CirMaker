#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFontComboBox>
#include <QGridLayout>
#include <QPushButton>
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
    p.gridStyle = s.value(QStringLiteral("prefs/gridStyle"), QStringLiteral("lines")).toString() == u"dots" ? GridStyle::Dots
                                                                                                          : GridStyle::Lines;
    p.zoomPercent = std::clamp(s.value(QStringLiteral("prefs/zoomPercent"), p.zoomPercent).toInt(), 20, 400);
    p.ticksPerFrame = std::clamp(s.value(QStringLiteral("prefs/ticksPerFrame"), p.ticksPerFrame).toInt(), 1, 1000);
    p.stepTicks = std::clamp(s.value(QStringLiteral("prefs/stepTicks"), p.stepTicks).toInt(), 0, 100000);
    p.conflictsUnknown = s.value(QStringLiteral("prefs/conflictsUnknown"), p.conflictsUnknown).toBool();
    p.theme = Theme::current();
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
    s.setValue(QStringLiteral("prefs/stepTicks"), stepTicks);
    s.setValue(QStringLiteral("prefs/conflictsUnknown"), conflictsUnknown);
    Theme::setCurrent(theme);
}

PreferencesDialog::PreferencesDialog(const Preferences& prefs, QWidget* parent)
    : QDialog(parent), m_initialLanguage(prefs.language), m_theme(prefs.theme)
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
    m_step = new QSpinBox;
    m_step->setRange(0, 100000);
    m_step->setSpecialValueText(tr("next event"));
    m_step->setSuffix(tr(" ticks"));
    m_step->setValue(prefs.stepTicks);
    m_step->setToolTip(tr("How far Step advances; a cycle is 10 ticks"));
    sf->addRow(tr("S&tep size:"), m_step);
    m_conflicts = new QCheckBox(tr("Outputs in &conflict make the node unknown (X)"));
    m_conflicts->setChecked(prefs.conflictsUnknown);
    m_conflicts->setToolTip(tr("Off, as in CircuitMaker: when outputs drive a node to different levels, the device that "
                               "comes last in the circuit wins"));
    sf->addRow(m_conflicts);
    layout->addWidget(sim);

    // CircuitMaker's Colors, Device Font and Text Font
    auto* look = new QGroupBox(tr("Colours and fonts"));
    auto* lg = new QGridLayout(look);
    const std::pair<QString, QColor Theme::*> colors[] = {
        {tr("Background"), &Theme::background}, {tr("Grid"), &Theme::grid}, {tr("Devices"), &Theme::device},
        {tr("Wires"), &Theme::wire}, {tr("Buses"), &Theme::bus}, {tr("Text"), &Theme::text},
        {tr("Selection"), &Theme::selection}, {tr("Logic high"), &Theme::high}, {tr("Logic low"), &Theme::low}};
    int i = 0;
    for (const auto& [caption, member] : colors) {
        auto* b = new QPushButton(caption);
        m_colorButtons.insert(b, member);
        connect(b, &QPushButton::clicked, this, [this, b, caption] {
            QColor Theme::*m = m_colorButtons.value(b);
            const QColor c = QColorDialog::getColor(m_theme.*m, this, caption);
            if (c.isValid()) m_theme.*m = c;
            showColors();
        });
        lg->addWidget(b, i / 3, i % 3);
        ++i;
    }
    auto* fonts = new QFormLayout;
    m_deviceFont = new QFontComboBox;
    m_deviceFont->setCurrentFont(QFont(m_theme.deviceFont));
    m_textFont = new QFontComboBox;
    m_textFont->setCurrentFont(m_theme.textFont.isEmpty() ? font() : QFont(m_theme.textFont));
    fonts->addRow(tr("&Device font:"), m_deviceFont);
    fonts->addRow(tr("&Text font:"), m_textFont);
    lg->addLayout(fonts, 3, 0, 1, 3);
    auto* reset = new QPushButton(tr("&Restore defaults"));
    connect(reset, &QPushButton::clicked, this, [this] {
        m_theme = Theme();
        m_deviceFont->setCurrentFont(QFont(m_theme.deviceFont));
        m_textFont->setCurrentFont(font());
        showColors();
    });
    lg->addWidget(reset, 4, 2);
    layout->addWidget(look);
    showColors();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

// each colour button shows its colour as a swatch
void PreferencesDialog::showColors()
{
    for (auto it = m_colorButtons.cbegin(); it != m_colorButtons.cend(); ++it) {
        QPixmap swatch(16, 16);
        swatch.fill(m_theme.*it.value());
        it.key()->setIcon(QIcon(swatch));
    }
}

Preferences PreferencesDialog::result() const
{
    Preferences p;
    p.language = m_language->currentData().toString();
    p.gridVisible = m_grid->isChecked();
    p.gridStyle = m_gridStyle->currentIndex() == 1 ? Preferences::GridStyle::Lines : Preferences::GridStyle::Dots;
    p.zoomPercent = m_zoom->value();
    p.ticksPerFrame = m_ticks->value();
    p.stepTicks = m_step->value();
    p.conflictsUnknown = m_conflicts->isChecked();
    p.theme = m_theme;
    p.theme.deviceFont = m_deviceFont->currentFont().family();
    const QString text = m_textFont->currentFont().family();
    p.theme.textFont = text == font().family() && m_theme.textFont.isEmpty() ? QString() : text;
    return p;
}

} // namespace cm
