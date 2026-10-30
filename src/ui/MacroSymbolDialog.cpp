#include "ui/MacroSymbolDialog.h"

#include "library/LibraryRegistry.h"
#include "library/Macros.h"
#include "ui/SymbolPainter.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace cm {

namespace {

const char* const kSides[4] = {"left", "right", "top", "bottom"};
const QString kGap = QStringLiteral("—");

} // namespace

MacroSymbolDialog::MacroSymbolDialog(const SymbolDef& macro, QWidget* parent)
    : QDialog(parent), m_name(macro.name), m_circuit(macro.model.value(u"circuit").toObject())
{
    setWindowTitle(tr("Macro symbol: %1").arg(macro.name));
    auto* top = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    auto* sidesBox = new QGroupBox(tr("Pins (drag between the sides)"));
    auto* grid = new QGridLayout(sidesBox);
    const QString captions[4] = {tr("Left"), tr("Right"), tr("Top"), tr("Bottom")};
    for (int k = 0; k < 4; ++k) {
        m_sides[k] = new QListWidget;
        m_sides[k]->setDragDropMode(QAbstractItemView::DragDrop);
        m_sides[k]->setDefaultDropAction(Qt::MoveAction);
        m_sides[k]->setSelectionMode(QAbstractItemView::SingleSelection);
        m_sides[k]->setMinimumWidth(110);
        connect(m_sides[k]->model(), &QAbstractItemModel::rowsInserted, this, &MacroSymbolDialog::updatePreview,
                Qt::QueuedConnection);
        connect(m_sides[k]->model(), &QAbstractItemModel::rowsRemoved, this, &MacroSymbolDialog::updatePreview,
                Qt::QueuedConnection);
        grid->addWidget(new QLabel(captions[k]), 0, k);
        grid->addWidget(m_sides[k], 1, k);
    }
    // current sides: the stored layout, else where the pins are now
    const QJsonObject stored = macros::symbolLayout(macro);
    if (!stored.isEmpty()) {
        for (int k = 0; k < 4; ++k)
            for (const QJsonValue& v : stored.value(QLatin1String(kSides[k])).toArray())
                m_sides[k]->addItem(v.toString().isEmpty() ? kGap : v.toString());
    }
    double minX = 1e9, maxX = -1e9;
    for (const SymbolPin& p : macro.pins) minX = std::min(minX, p.pos.x()), maxX = std::max(maxX, p.pos.x());
    QVector<std::pair<double, QString>> found[4];
    for (const SymbolPin& p : macro.pins) {
        bool listed = false;
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < m_sides[k]->count(); ++i) listed |= m_sides[k]->item(i)->text() == p.name;
        if (listed) continue;
        const int side = std::abs(p.pos.x() - minX) < 0.5 ? 0 : std::abs(p.pos.x() - maxX) < 0.5 ? 1 : p.pos.y() < 0 ? 2 : 3;
        found[side] << std::pair{side < 2 ? p.pos.y() : p.pos.x(), p.name};
    }
    for (int k = 0; k < 4; ++k) {
        std::sort(found[k].begin(), found[k].end());
        for (const auto& [at, name] : std::as_const(found[k])) m_sides[k]->addItem(name);
    }

    auto* buttons = new QHBoxLayout;
    auto* gap = new QPushButton(tr("Add &gap"));
    gap->setToolTip(tr("An empty pin position after the selected pin"));
    connect(gap, &QPushButton::clicked, this, [this] {
        for (QListWidget* list : m_sides) {
            if (!list->hasFocus() && list->currentRow() < 0) continue;
            list->insertItem(list->currentRow() + 1, kGap);
            break;
        }
        updatePreview();
    });
    auto* removeGap = new QPushButton(tr("&Remove gap"));
    connect(removeGap, &QPushButton::clicked, this, [this] {
        for (QListWidget* list : m_sides)
            if (QListWidgetItem* it = list->currentItem(); it && it->text() == kGap) delete it;
        updatePreview();
    });
    buttons->addWidget(gap);
    buttons->addWidget(removeGap);
    buttons->addStretch();
    grid->addLayout(buttons, 2, 0, 1, 4);
    row->addWidget(sidesBox, 1);

    auto* right = new QVBoxLayout;
    auto* form = new QFormLayout;
    m_width = new QSpinBox;
    m_width->setRange(0, 1000);
    m_width->setSingleStep(20);
    m_width->setSpecialValueText(tr("automatic"));
    m_width->setValue(stored.value(u"width").toInt());
    m_title = new QLineEdit(stored.value(u"title").toString(macro.name));
    form->addRow(tr("&Width:"), m_width);
    form->addRow(tr("&Title:"), m_title);
    right->addLayout(form);
    m_preview = new QLabel;
    m_preview->setMinimumSize(260, 220);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::StyledPanel);
    right->addWidget(m_preview, 1);
    row->addLayout(right);
    top->addLayout(row);
    connect(m_width, &QSpinBox::valueChanged, this, &MacroSymbolDialog::updatePreview);
    connect(m_title, &QLineEdit::textChanged, this, &MacroSymbolDialog::updatePreview);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        // inputs left, outputs right, as generated
        QJsonObject plain = m_circuit;
        plain.remove(u"symbol");
        SymbolDef s;
        if (!LibraryRegistry::parseSymbol(macros::symbolJson(m_name, plain), s, nullptr)) return;
        for (QListWidget* list : m_sides) list->clear();
        for (const SymbolPin& p : s.pins) m_sides[p.pos.x() < 0 ? 0 : 1]->addItem(p.name);
        m_width->setValue(0);
        m_title->setText(m_name);
        updatePreview();
    });
    top->addWidget(box);
    updatePreview();
}

void MacroSymbolDialog::movePin(const QString& name, int side)
{
    for (QListWidget* list : m_sides)
        for (int i = list->count() - 1; i >= 0; --i)
            if (list->item(i)->text() == name) delete list->takeItem(i);
    m_sides[std::clamp(side, 0, 3)]->addItem(name);
    updatePreview();
}

QJsonObject MacroSymbolDialog::layout() const
{
    QJsonObject o;
    for (int k = 0; k < 4; ++k) {
        QJsonArray a;
        for (int i = 0; i < m_sides[k]->count(); ++i) {
            const QString t = m_sides[k]->item(i)->text();
            a.append(t == kGap ? QString() : t);
        }
        o.insert(QLatin1String(kSides[k]), a);
    }
    if (m_width->value() > 0) o.insert(u"width", m_width->value());
    if (m_title->text() != m_name) o.insert(u"title", m_title->text());
    return o;
}

void MacroSymbolDialog::updatePreview()
{
    QJsonObject doc = m_circuit;
    doc.insert(u"symbol", layout());
    SymbolDef s;
    if (!LibraryRegistry::parseSymbol(macros::symbolJson(m_name, doc), s, nullptr)) return;
    const QRectF b = s.bounds.adjusted(-10, -10, 10, 10);
    const QSize size = m_preview->size().expandedTo(QSize(260, 220));
    QPixmap pix(size);
    pix.fill(Qt::white);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const double scale = std::min({size.width() / b.width(), size.height() / b.height(), 2.0});
    p.translate(size.width() / 2.0, size.height() / 2.0);
    p.scale(scale, scale);
    p.translate(-b.center());
    SymbolPainter::paint(&p, s, Qt::black, {});
    for (const SymbolPin& pin : s.pins)
        SymbolPainter::uprightText(&p, pin.pos + QPointF(0, -6), pin.name, 7, 0, QColor(0x00, 0x60, 0xc0));
    p.end();
    m_preview->setPixmap(pix);
}

} // namespace cm
