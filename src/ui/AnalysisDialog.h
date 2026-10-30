#pragma once

#include "core/Circuit.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

namespace cm {

// Chooses the analog analysis and its parameters, and an optional sweep
// (temperature, parameter, Monte Carlo) that repeats it.
class AnalysisDialog : public QDialog {
    Q_OBJECT
public:
    // sources: voltage/current source designators (DC sweep, transfer function,
    // noise input); nets: named nets (outputs); devices: parts for the
    // parameter sweep.
    AnalysisDialog(const AnalysisSettings& settings, const QStringList& sources, const QStringList& nets,
                   const QStringList& devices, QWidget* parent = nullptr);
    AnalysisSettings result() const;

private:
    AnalysisSettings m_settings;
    QComboBox* m_type;
    QStackedWidget* m_pages;
    QLineEdit *m_tranStop, *m_tranStep, *m_acStart, *m_acStop, *m_dcStart, *m_dcStop, *m_dcStep;
    QSpinBox* m_acPoints;
    QComboBox* m_dcSource;
    QLineEdit *m_fourierStop, *m_fourierStep, *m_fourierFreq;
    QSpinBox* m_harmonics;
    QComboBox *m_tfOutput, *m_tfSource;
    QComboBox *m_noiseOutput, *m_noiseSource;
    QLineEdit *m_noiseStart, *m_noiseStop;
    QSpinBox* m_noisePoints;
    QComboBox* m_sweep;
    QStackedWidget* m_sweepPages;
    QLineEdit *m_tempStart, *m_tempStop, *m_tempStep;
    QComboBox* m_paramDevice;
    QLineEdit *m_paramStart, *m_paramStop, *m_paramStep, *m_mcTolerance;
    QSpinBox* m_mcRuns;
};

} // namespace cm
