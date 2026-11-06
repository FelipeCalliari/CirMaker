#include "ui/AnalysisDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cm {

namespace {

QComboBox* editableCombo(const QStringList& items, const QString& current)
{
    auto* c = new QComboBox;
    c->setEditable(true);
    c->addItems(items);
    c->setCurrentText(current.isEmpty() ? items.value(0) : current);
    return c;
}

QSpinBox* spin(int min, int max, int value)
{
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

} // namespace

AnalysisDialog::AnalysisDialog(const AnalysisSettings& settings, const QStringList& sources, const QStringList& nets,
                               const QStringList& devices, QWidget* parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(tr("Analog analysis"));
    auto* layout = new QVBoxLayout(this);
    auto* top = new QFormLayout;
    m_type = new QComboBox;
    m_type->addItems({tr("Operating point (DC bias)"), tr("Transient (time domain)"), tr("AC sweep (frequency response)"),
                      tr("DC sweep"), tr("Fourier (harmonics of the transient)"), tr("Transfer function (DC gain, impedances)"),
                      tr("Noise")});
    top->addRow(tr("&Analysis:"), m_type);
    layout->addLayout(top);

    m_pages = new QStackedWidget;
    auto page = [&](QFormLayout*& form) {
        auto* w = new QWidget;
        form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        m_pages->addWidget(w);
    };
    QFormLayout* f;
    page(f);
    f->addRow(new QLabel(tr("Node voltages and meter readings are shown on the schematic.")));

    page(f);
    m_tranStart = new QLineEdit(settings.tranStart);
    m_tranStop = new QLineEdit(settings.tranStop);
    m_tranStep = new QLineEdit(settings.tranStep);
    m_tranMax = new QLineEdit(settings.tranMax);
    m_tranMax->setPlaceholderText(tr("automatic"));
    f->addRow(tr("St&art time:"), m_tranStart);
    f->addRow(tr("Stop &time:"), m_tranStop);
    f->addRow(tr("Time &step:"), m_tranStep);
    f->addRow(tr("Ma&ximum step:"), m_tranMax);

    page(f);
    m_acStart = new QLineEdit(settings.acStart);
    m_acStop = new QLineEdit(settings.acStop);
    m_acPoints = spin(1, 10000, settings.acPointsPerDecade);
    m_acSweep = new QComboBox;
    m_acSweep->addItem(tr("Decade"), QStringLiteral("dec"));
    m_acSweep->addItem(tr("Octave"), QStringLiteral("oct"));
    m_acSweep->addItem(tr("Linear"), QStringLiteral("lin"));
    m_acSweep->setCurrentIndex(std::max(0, m_acSweep->findData(settings.acSweep)));
    f->addRow(tr("&Start frequency (Hz):"), m_acStart);
    f->addRow(tr("St&op frequency (Hz):"), m_acStop);
    f->addRow(tr("S&weep:"), m_acSweep);
    f->addRow(tr("&Points (per decade, per octave or in all):"), m_acPoints);
    f->addRow(new QLabel(tr("Signal generators are the AC sources (magnitude and phase in their properties).")));

    page(f);
    m_dcSource = new QComboBox;
    m_dcSource->addItems(sources);
    m_dcSource->setCurrentText(settings.dcSource);
    m_dcStart = new QLineEdit(settings.dcStart);
    m_dcStop = new QLineEdit(settings.dcStop);
    m_dcStep = new QLineEdit(settings.dcStep);
    f->addRow(tr("S&weep source:"), m_dcSource);
    f->addRow(tr("Sta&rt:"), m_dcStart);
    f->addRow(tr("Sto&p:"), m_dcStop);
    f->addRow(tr("Ste&p:"), m_dcStep);

    page(f);
    m_fourierStop = new QLineEdit(settings.tranStop);
    m_fourierStep = new QLineEdit(settings.tranStep);
    m_fourierFreq = new QLineEdit(settings.fourierFreq);
    m_fourierFreq->setPlaceholderText(tr("first signal generator's"));
    m_harmonics = spin(2, 50, settings.fourierHarmonics);
    f->addRow(tr("Stop &time:"), m_fourierStop);
    f->addRow(tr("Maximum &step:"), m_fourierStep);
    f->addRow(tr("&Fundamental (Hz):"), m_fourierFreq);
    f->addRow(tr("&Harmonics:"), m_harmonics);
    f->addRow(new QLabel(tr("The last period of the transient is analysed; the THD is shown in the status bar.")));

    page(f);
    m_tfOutput = editableCombo(nets, settings.output);
    m_tfSource = editableCombo(sources, settings.inputSource);
    f->addRow(tr("&Output net:"), m_tfOutput);
    f->addRow(tr("&Input source:"), m_tfSource);

    page(f);
    m_noiseOutput = editableCombo(nets, settings.output);
    m_noiseSource = editableCombo(sources, settings.inputSource);
    m_noiseStart = new QLineEdit(settings.acStart);
    m_noiseStop = new QLineEdit(settings.acStop);
    m_noisePoints = spin(1, 1000, settings.acPointsPerDecade);
    f->addRow(tr("&Output net:"), m_noiseOutput);
    f->addRow(tr("&Input source:"), m_noiseSource);
    f->addRow(tr("&Start frequency (Hz):"), m_noiseStart);
    f->addRow(tr("St&op frequency (Hz):"), m_noiseStop);
    f->addRow(tr("&Points per decade:"), m_noisePoints);
    layout->addWidget(m_pages);

    // sweeps
    auto* sweepBox = new QGroupBox(tr("Repeat the analysis"));
    auto* sweepLayout = new QVBoxLayout(sweepBox);
    m_sweep = new QComboBox;
    m_sweep->addItems({tr("Once"), tr("Temperature sweep"), tr("Parameter sweep"), tr("Monte Carlo")});
    sweepLayout->addWidget(m_sweep);
    m_sweepPages = new QStackedWidget;
    auto sweepPage = [&](QFormLayout*& form) {
        auto* w = new QWidget;
        form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        m_sweepPages->addWidget(w);
    };
    sweepPage(f);
    sweepPage(f);
    m_tempStart = new QLineEdit(settings.tempStart);
    m_tempStop = new QLineEdit(settings.tempStop);
    m_tempStep = new QLineEdit(settings.tempStep);
    f->addRow(tr("From (°C):"), m_tempStart);
    f->addRow(tr("To (°C):"), m_tempStop);
    f->addRow(tr("Step (°C):"), m_tempStep);
    sweepPage(f);
    m_paramDevice = editableCombo(devices, settings.paramDevice);
    m_paramStart = new QLineEdit(settings.paramStart);
    m_paramStop = new QLineEdit(settings.paramStop);
    m_paramStep = new QLineEdit(settings.paramStep);
    f->addRow(tr("De&vice (its value):"), m_paramDevice);
    f->addRow(tr("From:"), m_paramStart);
    f->addRow(tr("To:"), m_paramStop);
    f->addRow(tr("Step:"), m_paramStep);
    sweepPage(f);
    m_mcRuns = spin(2, 200, settings.mcRuns);
    m_mcTolerance = new QLineEdit(settings.mcTolerance);
    f->addRow(tr("&Runs:"), m_mcRuns);
    f->addRow(tr("&Tolerance of R, C, L (%):"), m_mcTolerance);
    sweepLayout->addWidget(m_sweepPages);
    layout->addWidget(sweepBox);
    connect(m_sweep, &QComboBox::currentIndexChanged, m_sweepPages, &QStackedWidget::setCurrentIndex);
    m_sweep->setCurrentIndex(int(settings.sweep));
    m_sweepPages->setCurrentIndex(int(settings.sweep));

    auto* options = new QFormLayout;
    m_spiceOptions = new QLineEdit(settings.spiceOptions);
    m_spiceOptions->setPlaceholderText(QStringLiteral("METHOD=GEAR ITL4=100"));
    options->addRow(tr("SPICE &options:"), m_spiceOptions);
    layout->addLayout(options);

    auto* hint = new QLabel(tr("Values accept SPICE suffixes: 10m, 4.7k, 1Meg, 100u, 1n."));
    hint->setEnabled(false);
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_type, &QComboBox::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_type->setCurrentIndex(int(settings.type));
    m_pages->setCurrentIndex(int(settings.type));
}

AnalysisSettings AnalysisDialog::result() const
{
    using Type = AnalysisSettings::Type;
    AnalysisSettings a = m_settings;
    a.analogMode = true;
    a.type = Type(m_type->currentIndex());
    // pages share settings; the chosen analysis's page wins
    const bool fourier = a.type == Type::Fourier, noise = a.type == Type::Noise;
    a.tranStop = (fourier ? m_fourierStop : m_tranStop)->text().trimmed();
    a.tranStep = (fourier ? m_fourierStep : m_tranStep)->text().trimmed();
    a.acStart = (noise ? m_noiseStart : m_acStart)->text().trimmed();
    a.acStop = (noise ? m_noiseStop : m_acStop)->text().trimmed();
    a.acPointsPerDecade = (noise ? m_noisePoints : m_acPoints)->value();
    if (a.type == Type::Transient) {
        a.tranStart = m_tranStart->text().trimmed().isEmpty() ? QStringLiteral("0") : m_tranStart->text().trimmed();
        a.tranMax = m_tranMax->text().trimmed();
    }
    if (a.type == Type::AC) a.acSweep = m_acSweep->currentData().toString();
    a.dcSource = m_dcSource->currentText();
    a.dcStart = m_dcStart->text().trimmed();
    a.dcStop = m_dcStop->text().trimmed();
    a.dcStep = m_dcStep->text().trimmed();
    a.fourierFreq = m_fourierFreq->text().trimmed();
    a.fourierHarmonics = m_harmonics->value();
    a.output = (noise ? m_noiseOutput : m_tfOutput)->currentText().trimmed();
    a.inputSource = (noise ? m_noiseSource : m_tfSource)->currentText().trimmed();
    a.sweep = AnalysisSettings::Sweep(m_sweep->currentIndex());
    a.tempStart = m_tempStart->text().trimmed();
    a.tempStop = m_tempStop->text().trimmed();
    a.tempStep = m_tempStep->text().trimmed();
    a.paramDevice = m_paramDevice->currentText().trimmed();
    a.paramStart = m_paramStart->text().trimmed();
    a.paramStop = m_paramStop->text().trimmed();
    a.paramStep = m_paramStep->text().trimmed();
    a.mcRuns = m_mcRuns->value();
    a.mcTolerance = m_mcTolerance->text().trimmed();
    a.spiceOptions = m_spiceOptions->text().trimmed();
    return a;
}

} // namespace cm
