#pragma once

#include "core/Circuit.h"

#include <QDialog>
#include <QHash>

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPlainTextEdit;

namespace cm {

struct SymbolDef;

// Designator, label and the model parameters of one component.
class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    PropertiesDialog(const Component& c, const SymbolDef& symbol, QWidget* parent = nullptr);
    Component result() const;

private:
    Component m_c;
    QLineEdit* m_designator;
    QLineEdit* m_label;
    QHash<QString, QSpinBox*> m_intProps;
    QHash<QString, QLineEdit*> m_textProps;
    QHash<QString, QComboBox*> m_choiceProps;
    QCheckBox *m_showDesignator, *m_showLabel;
    bool m_designatorDefault, m_labelDefault;   // the symbol's visibility
    QCheckBox *m_excludePcb = nullptr, *m_excludeBom = nullptr;
    QLineEdit* m_package = nullptr;
    QString m_pins;
    QLineEdit* m_spiceLine = nullptr;
    QPlainTextEdit* m_spiceModels = nullptr;                     // "pins" property, edited in its own table
    void editPins(const SymbolDef& symbol);
};

// Title, author, date and revision of the drawing.
class TitleBlockDialog : public QDialog {
    Q_OBJECT
public:
    TitleBlockDialog(const TitleBlock& tb, QWidget* parent = nullptr);
    TitleBlock result() const;

private:
    TitleBlock m_tb;
    QLineEdit* m_title;
    QLineEdit* m_author;
    QLineEdit* m_date;
    QLineEdit* m_revision;
};

} // namespace cm
