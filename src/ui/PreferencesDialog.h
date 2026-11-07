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
    // CircuitMaker's Options > Schematic
    bool autoRepeat = false;        // a placed device stays on the cursor for another
    bool promptToSave = true;       // ask before dropping changes
    bool showBusLabels = true;      // a bus's name in its box
    int designationStart = 1;       // the first designator number of each prefix
    bool snapToGrid = true;         // CircuitMaker's Grid > Snap To
    int connectionArea = 3;         // CircuitMaker's connection area size (its units)
    bool showConnectionArea = true; // drawn around a connection being made
    bool showNewDesignators = true; // new devices show their designator
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
    QCheckBox *m_autoRepeat, *m_promptToSave, *m_busLabels, *m_showNewDesignators;
    QSpinBox *m_designationStart, *m_connectionArea;
    QCheckBox *m_snap, *m_showArea;
    QString m_initialLanguage;
    Theme m_theme;
    QHash<QPushButton*, QColor Theme::*> m_colorButtons;
    QFontComboBox *m_deviceFont, *m_textFont;
    void showColors();
};

} // namespace cm
