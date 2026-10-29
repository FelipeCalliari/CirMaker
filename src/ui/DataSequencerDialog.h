#pragma once

#include "core/Circuit.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace cm {

struct SymbolDef;

// Data Sequencer properties: the word list (hex and bit columns), address
// range, clocking and analog levels.
class DataSequencerDialog : public QDialog {
    Q_OBJECT
public:
    DataSequencerDialog(const Component& c, const SymbolDef& symbol, QWidget* parent = nullptr);
    Component result() const;
    QVector<int> words() const { return m_words; }

private:
    void resizeWords(int lines);
    void fillTable();
    void setWord(int row, int value);
    void openPatternEditor();

    Component m_c;
    QVector<int> m_words;
    bool m_updating = false;
    QLineEdit *m_designator, *m_label;
    QTableWidget* m_table;
    QSpinBox *m_lines, *m_start, *m_stop, *m_tick;
    QCheckBox* m_external;
    QLineEdit *m_low, *m_high, *m_step;
};

// Fills rows and bit columns of a word list with a pattern (CircuitMaker's
// Pattern Editor).
class PatternEditor : public QDialog {
    Q_OBJECT
public:
    enum class Pattern { CountUp, CountDown, Shift1Left, Shift1Right, Shift0Left, Shift0Right, All0, All1, Random };
    PatternEditor(int lines, int firstRow, int lastRow, QWidget* parent = nullptr);
    // Applies the chosen pattern to `words`.
    void apply(QVector<int>& words) const;
    static void fill(QVector<int>& words, Pattern pattern, int first, int last, int increment, int columns,
                     quint32 seed = 1);

private:
    QComboBox* m_pattern;
    QSpinBox *m_first, *m_last, *m_increment;
    QVector<QCheckBox*> m_columns;
};

} // namespace cm
