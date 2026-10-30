#pragma once

#include "core/Circuit.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;

namespace cm {

struct SymbolDef;

// CircuitMaker's multifunction signal generator editor: one page per wave
// (sine, square, triangle, pulse, AM, FM, exponential, piecewise linear),
// volts or amps, and the AC analysis stimulus.
class SignalGenDialog : public QDialog {
    Q_OBJECT
public:
    SignalGenDialog(const Component& c, const SymbolDef& symbol, QWidget* parent = nullptr);
    Component result() const;

    static QStringList waves();
    // Points of a CircuitMaker .PWL file ("+ t v t v ..." lines, "*" comments),
    // optionally scaled to a peak-to-peak amplitude around zero plus offset
    // and stretched to a total time (0 = keep).
    static QString parsePwlFile(const QString& text, double maxAmp = 0, double offset = 0, double maxTime = 0);

private:
    void loadPwl();

    Component m_c;
    QLineEdit *m_designator, *m_label;
    QComboBox *m_wave, *m_unit;
    QStackedWidget* m_pages;
    QHash<QString, QLineEdit*> m_fields[8];     // per page: property -> field
    QPlainTextEdit* m_pwl;
    QCheckBox* m_ac;
    QLineEdit *m_acMag, *m_acPhase;
};

} // namespace cm
