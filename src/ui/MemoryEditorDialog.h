#pragma once

#include <QDialog>
#include <QVector>

class QTableWidget;

namespace cm {

// CircuitMaker's "Edit PROM/RAM": the memory as a hex grid, 16 bytes a row.
class MemoryEditorDialog : public QDialog {
    Q_OBJECT
public:
    MemoryEditorDialog(const QString& title, const QVector<int>& words, QWidget* parent = nullptr);
    QVector<int> words() const { return m_words; }

    // The "data" property: hex bytes separated by spaces, trailing zeros dropped.
    static QString toText(const QVector<int>& words);
    // Words of a "data" property or a text file, padded/truncated to `size`.
    static QVector<int> fromText(const QString& text, int size);

private:
    void fillTable();
    void setWord(int address, int value);
    void load();
    void save();

    QVector<int> m_words;
    bool m_updating = false;
    QTableWidget* m_table;
};

} // namespace cm
