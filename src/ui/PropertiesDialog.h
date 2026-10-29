#pragma once

#include "core/Circuit.h"

#include <QDialog>
#include <QHash>

class QLineEdit;
class QSpinBox;

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
