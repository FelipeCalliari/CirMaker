#pragma once

#include "core/Circuit.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

namespace cm {

// Chooses the analog analysis and its parameters.
class AnalysisDialog : public QDialog {
    Q_OBJECT
public:
    // sources: designators of voltage/current sources, for the DC sweep.
    AnalysisDialog(const AnalysisSettings& settings, const QStringList& sources, QWidget* parent = nullptr);
    AnalysisSettings result() const;

private:
    AnalysisSettings m_settings;
    QComboBox* m_type;
    QStackedWidget* m_pages;
    QLineEdit *m_tranStop, *m_tranStep, *m_acStart, *m_acStop, *m_dcStart, *m_dcStop, *m_dcStep;
    QSpinBox* m_acPoints;
    QComboBox* m_dcSource;
};

} // namespace cm
