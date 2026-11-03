#pragma once

#include "ui/Theme.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPushButton;
class QSpinBox;

namespace cm {

struct Preferences {
    enum class GridStyle { Dots, Lines };

    QString language;           // "" = system, "pt_BR", "en"
    bool gridVisible = true;
    GridStyle gridStyle = GridStyle::Lines;
    int zoomPercent = 60;       // initial zoom and upper limit of "fit to window"
    int ticksPerFrame = 2;
    int stepTicks = 0;          // Step: this many ticks (CircuitMaker's step size); 0 = to the next event
    bool conflictsUnknown = false;  // outputs that disagree make the node unknown (CircuitMaker: the last device wins)
    Theme theme;                // colours and fonts

    static Preferences load();
    void save() const;
};

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const Preferences& prefs, QWidget* parent = nullptr);
    Preferences result() const;

private:
    QComboBox* m_language;
    QCheckBox* m_grid;
    QComboBox* m_gridStyle;
    QSpinBox* m_zoom;
    QSpinBox* m_ticks;
    QCheckBox* m_conflicts;
    QSpinBox* m_step;
    QString m_initialLanguage;
    Theme m_theme;
    QHash<QPushButton*, QColor Theme::*> m_colorButtons;
    QFontComboBox *m_deviceFont, *m_textFont;
    void showColors();
};

} // namespace cm
