#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace cm {

struct SymbolDef;

// Edit Macro Symbol: which side of the box each port pin is on, their order
// and gaps, the box width and its title, with a preview.
class MacroSymbolDialog : public QDialog {
    Q_OBJECT
public:
    explicit MacroSymbolDialog(const SymbolDef& macro, QWidget* parent = nullptr);
    QJsonObject layout() const;
    // Moves a pin to a side (0 left, 1 right, 2 top, 3 bottom) at the end.
    void movePin(const QString& name, int side);

private:
    void updatePreview();

    QString m_name;
    QJsonObject m_circuit;
    QListWidget* m_sides[4];
    QSpinBox* m_width;
    QLineEdit* m_title;
    QLabel* m_preview;
};

} // namespace cm
