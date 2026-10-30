#pragma once

#include "core/Circuit.h"
#include "core/Faults.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

namespace cm {

struct SymbolDef;

// CircuitMaker's Device Faults: pins stuck high/low, open or shorted, a
// wrong Label-Value and a hint for the student.
class DeviceFaultsDialog : public QDialog {
    Q_OBJECT
public:
    enum Kind { High, Low, Open, Short, KindCount };
    DeviceFaultsDialog(const Component& c, const SymbolDef& symbol, QWidget* parent = nullptr);
    faults::DeviceFaults result() const;
    // Assigns the selected free pins to a fault, or returns its pins to the list.
    void toggle(Kind kind);
    void selectPins(const QStringList& names);

private:
    void refresh();

    faults::DeviceFaults m_f;
    QStringList m_pins;                     // symbol pin names, in order
    QStringList m_assigned[KindCount];
    QListWidget* m_list;
    QLabel* m_labels[KindCount];
    QCheckBox* m_enabled;
    QLineEdit* m_value;
    QPlainTextEdit* m_hint;
};

// Circuit Fault Data: lock password, options the student loses while the
// circuit is locked, analog fault values, hint/replacement bookkeeping.
class CircuitFaultsDialog : public QDialog {
    Q_OBJECT
public:
    // Extra results besides Accepted/Rejected.
    enum Action { SelectFaulty = 10, SelectReplaced, SelectHinted };
    struct Counts { int faulty = 0, replaced = 0, hinted = 0; };
    CircuitFaultsDialog(const FaultSettings& settings, Counts counts, QWidget* parent = nullptr);
    FaultSettings result() const;
    bool clearMarks() const;

    static QList<std::pair<QString, QString>> options();   // key, caption

private:
    FaultSettings m_s;
    QLineEdit* m_password;
    QCheckBox* m_unlock;
    QHash<QString, QCheckBox*> m_options;
    QLineEdit *m_high, *m_low, *m_open, *m_short;
    QCheckBox* m_clear;
};

// What a student sees for a device while the faults are locked.
class AccessFaultsDialog : public QDialog {
    Q_OBJECT
public:
    enum Action { Unlocked = 10, ShowHint, Replace };
    AccessFaultsDialog(const QString& device, bool hintAllowed, bool replaceAllowed, const QString& passwordHash,
                       QWidget* parent = nullptr);
};

} // namespace cm
