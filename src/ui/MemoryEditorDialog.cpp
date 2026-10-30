#include "ui/MemoryEditorDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace cm {

namespace {

constexpr int kColumns = 16;

QString hex(int v, int digits) { return QStringLiteral("%1").arg(v, digits, 16, QChar(u'0')).toUpper(); }

} // namespace

QString MemoryEditorDialog::toText(const QVector<int>& words)
{
    int end = words.size();
    while (end > 0 && words[end - 1] == 0) --end;
    QStringList bytes;
    for (int i = 0; i < end; ++i) bytes << hex(words[i] & 0xff, 2);
    return bytes.join(u' ');
}

QVector<int> MemoryEditorDialog::fromText(const QString& text, int size)
{
    QVector<int> out;
    for (const QString& t : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        if (out.size() == size) break;
        out << (t.toInt(nullptr, 16) & 0xff);
    }
    out.resize(size);
    return out;
}

MemoryEditorDialog::MemoryEditorDialog(const QString& title, const QVector<int>& words, QWidget* parent)
    : QDialog(parent), m_words(words)
{
    setWindowTitle(title);
    auto* top = new QVBoxLayout(this);
    const int rows = (int(m_words.size()) + kColumns - 1) / kColumns;
    m_table = new QTableWidget(rows, kColumns);
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QStringList headers;
    for (int c = 0; c < kColumns; ++c) headers << hex(c, 1);
    m_table->setHorizontalHeaderLabels(headers);
    const int digits = std::max(2, int(QString::number(std::max<qsizetype>(1, m_words.size() - 1), 16).size()));
    QStringList addresses;
    for (int r = 0; r < rows; ++r) addresses << hex(r * kColumns, digits);
    m_table->setVerticalHeaderLabels(addresses);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setDefaultSectionSize(30);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->setMinimumWidth(kColumns * 30 + 90);
    m_table->setMinimumHeight(std::min(rows, 16) * 20 + 30);
    m_table->setToolTip(tr("Type each byte in hex"));
    top->addWidget(m_table, 1);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_updating) return;
        const int address = item->row() * kColumns + item->column();
        if (address >= m_words.size()) return;
        bool ok = false;
        const int v = item->text().trimmed().toInt(&ok, 16);
        setWord(address, ok && v >= 0 && v <= 0xff ? v : m_words[address]);
    });
    fillTable();

    auto* row = new QHBoxLayout;
    auto* clear = new QPushButton(tr("&Clear"));
    connect(clear, &QPushButton::clicked, this, [this] {
        m_words.fill(0);
        fillTable();
    });
    auto* load = new QPushButton(tr("&Load..."));
    connect(load, &QPushButton::clicked, this, &MemoryEditorDialog::load);
    auto* save = new QPushButton(tr("&Save..."));
    connect(save, &QPushButton::clicked, this, &MemoryEditorDialog::save);
    row->addWidget(clear);
    row->addWidget(load);
    row->addWidget(save);
    row->addStretch();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    row->addWidget(buttons);
    top->addLayout(row);
}

void MemoryEditorDialog::fillTable()
{
    m_updating = true;
    for (int a = 0; a < m_table->rowCount() * kColumns; ++a) {
        auto* item = new QTableWidgetItem(a < m_words.size() ? hex(m_words[a], 2) : QString());
        item->setTextAlignment(Qt::AlignCenter);
        if (a >= m_words.size()) item->setFlags(Qt::NoItemFlags);
        m_table->setItem(a / kColumns, a % kColumns, item);
    }
    m_updating = false;
}

void MemoryEditorDialog::setWord(int address, int value)
{
    m_words[address] = value;
    m_updating = true;
    m_table->item(address / kColumns, address % kColumns)->setText(hex(value, 2));
    m_updating = false;
}

// Text files hold hex bytes; anything else is read as raw binary.
void MemoryEditorDialog::load()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load memory contents"), {},
                                                      tr("Hex text (*.hex *.txt);;Binary (*.bin);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot open %1").arg(path));
        return;
    }
    const QByteArray data = f.readAll();
    if (QFileInfo(path).suffix().compare(u"bin", Qt::CaseInsensitive) == 0) {
        for (int i = 0; i < m_words.size(); ++i) m_words[i] = i < data.size() ? quint8(data[i]) : 0;
    } else {
        m_words = fromText(QString::fromLatin1(data), int(m_words.size()));
    }
    fillTable();
}

void MemoryEditorDialog::save()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save memory contents"), {},
                                                      tr("Hex text (*.hex *.txt);;Binary (*.bin)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1").arg(path));
        return;
    }
    if (QFileInfo(path).suffix().compare(u"bin", Qt::CaseInsensitive) == 0) {
        QByteArray data;
        for (int w : std::as_const(m_words)) data.append(char(w));
        f.write(data);
    } else {
        QStringList lines;
        for (int a = 0; a < m_words.size(); a += kColumns) {
            QStringList bytes;
            for (int i = a; i < std::min<int>(a + kColumns, m_words.size()); ++i) bytes << hex(m_words[i], 2);
            lines << bytes.join(u' ');
        }
        f.write((lines.join(u'\n') + u'\n').toLatin1());
    }
}

} // namespace cm
