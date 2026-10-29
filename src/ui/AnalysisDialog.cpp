#include "ui/AnalysisDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cm {

AnalysisDialog::AnalysisDialog(const AnalysisSettings& settings, const QStringList& sources, QWidget* parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(tr("Analog analysis"));
    auto* layout = new QVBoxLayout(this);
    auto* top = new QFormLayout;
    m_type = new QComboBox;
    m_type->addItems({tr("Operating point (DC bias)"), tr("Transient (time domain)"), tr("AC sweep (frequency response)"),
                      tr("DC sweep")});
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
    m_tranStop = new QLineEdit(settings.tranStop);
    m_tranStep = new QLineEdit(settings.tranStep);
    f->addRow(tr("Stop &time:"), m_tranStop);
    f->addRow(tr("Maximum &step:"), m_tranStep);

    page(f);
    m_acStart = new QLineEdit(settings.acStart);
    m_acStop = new QLineEdit(settings.acStop);
    m_acPoints = new QSpinBox;
    m_acPoints->setRange(1, 1000);
    m_acPoints->setValue(settings.acPointsPerDecade);
    f->addRow(tr("&Start frequency (Hz):"), m_acStart);
    f->addRow(tr("St&op frequency (Hz):"), m_acStop);
    f->addRow(tr("&Points per decade:"), m_acPoints);
    f->addRow(new QLabel(tr("Signal generators are the AC sources (1 V).")));

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
    layout->addWidget(m_pages);

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
    AnalysisSettings a = m_settings;
    a.analogMode = true;
    a.type = AnalysisSettings::Type(m_type->currentIndex());
    a.tranStop = m_tranStop->text().trimmed();
    a.tranStep = m_tranStep->text().trimmed();
    a.acStart = m_acStart->text().trimmed();
    a.acStop = m_acStop->text().trimmed();
    a.acPointsPerDecade = m_acPoints->value();
    a.dcSource = m_dcSource->currentText();
    a.dcStart = m_dcStart->text().trimmed();
    a.dcStop = m_dcStop->text().trimmed();
    a.dcStep = m_dcStep->text().trimmed();
    return a;
}

} // namespace cm
