#include "ui/DataSequencerDialog.h"

#include "library/SymbolDef.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace cm {

namespace {

constexpr int kMaxLines = 32767;
constexpr int kHexColumn = 8;   // columns 0..7 show bits 7..0

QVector<int> parseWords(const QString& text)
{
    QVector<int> out;
    for (const QString& t : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts))
        out << (t.toInt(nullptr, 16) & 0xff);
    return out;
}

QString hex(int v, int digits) { return QStringLiteral("%1").arg(v, digits, 16, QChar(u'0')).toUpper(); }

QSpinBox* hexSpin(int max)
{
    auto* s = new QSpinBox;
    s->setDisplayIntegerBase(16);
    s->setPrefix(QStringLiteral("0x"));
    s->setRange(0, max);
    return s;
}

} // namespace

DataSequencerDialog::DataSequencerDialog(const Component& c, const SymbolDef& symbol, QWidget* parent)
    : QDialog(parent), m_c(c)
{
    setWindowTitle(tr("%1 properties").arg(symbol.name));
    QVariantMap props = symbol.defaultProps;
    for (auto it = c.props.cbegin(); it != c.props.cend(); ++it)
        props.insert(it.key(), it.value());
    m_words = parseWords(props.value(QStringLiteral("data")).toString());
    const int lines = std::clamp(std::max<int>(props.value(QStringLiteral("lines"), 32).toInt(), int(m_words.size())), 1, kMaxLines);
    m_words.resize(lines);

    auto* top = new QHBoxLayout(this);
    m_table = new QTableWidget(0, 9);
    QStringList headers;
    for (int b = 7; b >= 0; --b) headers << QString::number(b);
    headers << tr("Hex");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->setMinimumWidth(340);
    m_table->setToolTip(tr("Click a bit to toggle it, or type the word in hex"));
    top->addWidget(m_table, 1);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col < kHexColumn) setWord(row, m_words[row] ^ (1 << (7 - col)));
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_updating || item->column() != kHexColumn) return;
        bool ok = false;
        const int v = item->text().trimmed().toInt(&ok, 16);
        setWord(item->row(), ok ? v & 0xff : m_words[item->row()]);
    });

    auto* side = new QVBoxLayout;
    top->addLayout(side);
    auto* form = new QFormLayout;
    side->addLayout(form);
    m_designator = new QLineEdit(c.designator);
    form->addRow(tr("&Designator:"), m_designator);
    m_label = new QLineEdit(c.label);
    form->addRow(tr("&Label:"), m_label);
    m_lines = new QSpinBox;
    m_lines->setRange(1, kMaxLines);
    m_lines->setValue(lines);
    m_lines->setToolTip(tr("Max. number of pattern lines; lines beyond it are lost when it shrinks"));
    form->addRow(tr("Pattern &lines:"), m_lines);
    m_start = hexSpin(lines - 1);
    m_start->setValue(props.value(QStringLiteral("start")).toInt());
    form->addRow(tr("&Start address:"), m_start);
    m_stop = hexSpin(lines - 1);
    m_stop->setValue(props.value(QStringLiteral("stop"), lines - 1).toInt());
    form->addRow(tr("Sto&p address:"), m_stop);

    auto* digital = new QGroupBox(tr("Digital"));
    auto* df = new QFormLayout(digital);
    m_tick = new QSpinBox;
    m_tick->setRange(1, 1000000);
    m_tick->setValue(props.value(QStringLiteral("tick"), 1).toInt());
    m_tick->setToolTip(tr("Ticks per address; keep it longer than the slowest propagation delay"));
    df->addRow(tr("&Tick increment:"), m_tick);
    m_external = new QCheckBox(tr("Use &external clock (CP1 rising, CP2 falling)"));
    m_external->setChecked(props.value(QStringLiteral("external")).toInt() != 0);
    df->addRow(m_external);
    connect(m_external, &QCheckBox::toggled, m_tick, &QWidget::setDisabled);
    m_tick->setDisabled(m_external->isChecked());
    side->addWidget(digital);

    auto* analog = new QGroupBox(tr("Analog"));
    auto* af = new QFormLayout(analog);
    m_low = new QLineEdit(props.value(QStringLiteral("low"), QStringLiteral("0")).toString());
    m_high = new QLineEdit(props.value(QStringLiteral("high"), QStringLiteral("5")).toString());
    m_step = new QLineEdit(props.value(QStringLiteral("step"), QStringLiteral("1m")).toString());
    af->addRow(tr("Low level (V):"), m_low);
    af->addRow(tr("High level (V):"), m_high);
    af->addRow(tr("Step time (s):"), m_step);
    side->addWidget(analog);

    auto* pattern = new QPushButton(tr("P&attern..."));
    connect(pattern, &QPushButton::clicked, this, &DataSequencerDialog::openPatternEditor);
    side->addWidget(pattern);
    side->addStretch();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    side->addWidget(buttons);

    connect(m_lines, &QSpinBox::valueChanged, this, &DataSequencerDialog::resizeWords);
    connect(m_start, &QSpinBox::valueChanged, this, [this](int v) { if (m_stop->value() < v) m_stop->setValue(v); });
    connect(m_stop, &QSpinBox::valueChanged, this, [this](int v) { if (m_start->value() > v) m_start->setValue(v); });
    fillTable();
}

void DataSequencerDialog::resizeWords(int lines)
{
    m_words.resize(lines);
    m_start->setMaximum(lines - 1);
    m_stop->setMaximum(lines - 1);
    fillTable();
}

void DataSequencerDialog::fillTable()
{
    m_updating = true;
    const int digits = m_words.size() > 0x1000 ? 4 : m_words.size() > 0x100 ? 3 : 2;
    m_table->setRowCount(int(m_words.size()));
    QStringList rows;
    for (int r = 0; r < m_words.size(); ++r) rows << hex(r, digits);
    m_table->setVerticalHeaderLabels(rows);
    for (int r = 0; r < m_words.size(); ++r) {
        for (int col = 0; col <= kHexColumn; ++col) {
            if (!m_table->item(r, col)) {
                auto* item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                if (col < kHexColumn) item->setFlags(Qt::ItemIsEnabled);
                m_table->setItem(r, col, item);
            }
        }
        m_updating = false;
        setWord(r, m_words[r]);
        m_updating = true;
    }
    m_updating = false;
}

void DataSequencerDialog::setWord(int row, int value)
{
    m_words[row] = value;
    const bool was = m_updating;
    m_updating = true;
    for (int col = 0; col < kHexColumn; ++col)
        m_table->item(row, col)->setText(value >> (7 - col) & 1 ? QStringLiteral("1") : QStringLiteral("0"));
    m_table->item(row, kHexColumn)->setText(hex(value, 2));
    m_updating = was;
}

void DataSequencerDialog::openPatternEditor()
{
    const QList<QTableWidgetSelectionRange> sel = m_table->selectedRanges();
    int first = 0, last = int(m_words.size()) - 1;
    if (!sel.isEmpty() && sel.first().rowCount() > 1) {
        first = sel.first().topRow();
        last = sel.first().bottomRow();
    }
    PatternEditor editor(int(m_words.size()), first, last, this);
    if (editor.exec() != QDialog::Accepted)
        return;
    editor.apply(m_words);
    fillTable();
}

Component DataSequencerDialog::result() const
{
    Component c = m_c;
    c.designator = m_designator->text().trimmed();
    c.label = m_label->text().trimmed();
    // trailing zero lines are implied by "lines"
    qsizetype used = m_words.size();
    while (used > 0 && m_words[used - 1] == 0) --used;
    QStringList bytes;
    for (qsizetype i = 0; i < used; ++i) bytes << hex(m_words[i], 2);
    c.props.insert(QStringLiteral("data"), bytes.join(u' '));
    c.props.insert(QStringLiteral("lines"), m_lines->value());
    c.props.insert(QStringLiteral("start"), m_start->value());
    c.props.insert(QStringLiteral("stop"), m_stop->value());
    c.props.insert(QStringLiteral("tick"), m_tick->value());
    c.props.insert(QStringLiteral("external"), m_external->isChecked() ? 1 : 0);
    c.props.insert(QStringLiteral("low"), m_low->text().trimmed());
    c.props.insert(QStringLiteral("high"), m_high->text().trimmed());
    c.props.insert(QStringLiteral("step"), m_step->text().trimmed());
    return c;
}

// ---------------------------------------------------------------- pattern editor

PatternEditor::PatternEditor(int lines, int firstRow, int lastRow, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Pattern Editor"));
    auto* form = new QFormLayout(this);
    m_pattern = new QComboBox;
    m_pattern->addItems({tr("Count Up"), tr("Count Down"), tr("Shift 1 Left"), tr("Shift 1 Right"), tr("Shift 0 Left"),
                         tr("Shift 0 Right"), tr("All 0"), tr("All 1"), tr("Random")});
    form->addRow(tr("&Pattern:"), m_pattern);
    m_first = hexSpin(lines - 1);
    m_first->setValue(firstRow);
    m_last = hexSpin(lines - 1);
    m_last->setValue(lastRow);
    form->addRow(tr("&First row:"), m_first);
    form->addRow(tr("&Last row:"), m_last);
    m_increment = new QSpinBox;
    m_increment->setRange(1, kMaxLines);
    m_increment->setToolTip(tr("Rows holding the same data before the pattern changes"));
    form->addRow(tr("&Increment:"), m_increment);
    auto* cols = new QHBoxLayout;
    for (int b = 7; b >= 0; --b) {
        auto* box = new QCheckBox(QString::number(b));
        box->setChecked(true);
        cols->addWidget(box);
        m_columns.prepend(box);         // m_columns[b] is bit b
    }
    form->addRow(tr("Columns:"), cols);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

void PatternEditor::apply(QVector<int>& words) const
{
    int columns = 0;
    for (int b = 0; b < 8; ++b)
        if (m_columns[b]->isChecked()) columns |= 1 << b;
    fill(words, Pattern(m_pattern->currentIndex()), m_first->value(), m_last->value(), m_increment->value(), columns,
         QRandomGenerator::global()->generate());
}

// The pattern is an n-bit value (n = selected columns) spread over the
// selected columns, lowest selected column = bit 0, so one column counting
// up alternates 0, 1, 0, 1...
void PatternEditor::fill(QVector<int>& words, Pattern pattern, int first, int last, int increment, int columns,
                         quint32 seed)
{
    QVector<int> bits;
    for (int b = 0; b < 8; ++b)
        if (columns >> b & 1) bits << b;
    const int n = int(bits.size());
    if (n == 0 || words.isEmpty())
        return;
    first = std::clamp(first, 0, int(words.size()) - 1);
    last = std::clamp(last, first, int(words.size()) - 1);
    increment = std::max(1, increment);
    const int full = (1 << n) - 1;
    QRandomGenerator random(seed);
    for (int row = first; row <= last; ++row) {
        const int k = (row - first) / increment;
        int v = 0;
        switch (pattern) {
        case Pattern::CountUp: v = k & full; break;
        case Pattern::CountDown: v = (full - k % (full + 1)) & full; break;
        case Pattern::Shift1Left: v = 1 << (k % n); break;
        case Pattern::Shift1Right: v = 1 << (n - 1 - k % n); break;
        case Pattern::Shift0Left: v = full & ~(1 << (k % n)); break;
        case Pattern::Shift0Right: v = full & ~(1 << (n - 1 - k % n)); break;
        case Pattern::All0: v = 0; break;
        case Pattern::All1: v = full; break;
        case Pattern::Random: v = int(random.bounded(full + 1)); break;
        }
        int word = words[row] & ~columns;
        for (int i = 0; i < n; ++i)
            if (v >> i & 1) word |= 1 << bits[i];
        words[row] = word;
    }
}

} // namespace cm
