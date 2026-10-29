#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace cm {

struct Preferences {
    enum class GridStyle { Dots, Lines };

    QString language;           // "" = system, "pt_BR", "en"
    bool gridVisible = true;
    GridStyle gridStyle = GridStyle::Dots;
    int zoomPercent = 60;       // initial zoom and upper limit of "fit to window"
    int ticksPerFrame = 2;

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
    QString m_initialLanguage;
};

} // namespace cm
