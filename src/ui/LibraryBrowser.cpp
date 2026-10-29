#include "ui/LibraryBrowser.h"

#include "library/LibraryRegistry.h"
#include "ui/Hotkeys.h"
#include "ui/SymbolPainter.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace cm {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

LibraryBrowser::LibraryBrowser(const LibraryRegistry& library, QWidget* parent) : QWidget(parent), m_library(library)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search devices (name or CircuitMaker alias)"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(110);
    m_preview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_preview);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    m_place = new QPushButton(tr("&Place"));
    m_place->setEnabled(false);
    m_hotkey = new QPushButton(tr("Hot&key..."));
    m_hotkey->setEnabled(false);
    m_hotkey->setToolTip(tr("Assign a key (0-9, a-z, A-Z) that picks this device on the schematic"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_hotkey);
    buttons->addWidget(m_place);
    layout->addLayout(buttons);
    connect(m_hotkey, &QPushButton::clicked, this, &LibraryBrowser::assignHotkey);

    connect(m_search, &QLineEdit::textChanged, this, &LibraryBrowser::filter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &LibraryBrowser::showSelected);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (const QString id = item->data(0, kIdRole).toString(); !id.isEmpty())
            emit placeRequested(id);
    });
    connect(m_place, &QPushButton::clicked, this, [this] {
        if (const QString id = selectedId(); !id.isEmpty())
            emit placeRequested(id);
    });
    reload();
}

void LibraryBrowser::reload()
{
    m_tree->clear();
    QHash<QString, QTreeWidgetItem*> categories;
    for (const SymbolDef* s : m_library.all()) {
        if (s->hidden)
            continue;
        // "Digital/Gates" -> nested folders
        QTreeWidgetItem* parent = nullptr;
        QString path;
        for (const QString& part : s->category.split(u'/', Qt::SkipEmptyParts)) {
            path += u'/' + part;
            QTreeWidgetItem*& node = categories[path];
            if (!node) {
                node = parent ? new QTreeWidgetItem(parent, {part}) : new QTreeWidgetItem(m_tree, {part});
                QFont f = node->font(0);
                f.setBold(true);
                node->setFont(0, f);
            }
            parent = node;
        }
        const QChar key = Hotkeys::keyFor(s->id);
        const QString text = key.isNull() ? s->name : QStringLiteral("%1   [%2]").arg(s->name, key);
        auto* item = parent ? new QTreeWidgetItem(parent, {text}) : new QTreeWidgetItem(m_tree, {text});
        item->setData(0, kIdRole, s->id);
        item->setToolTip(0, s->aliases.isEmpty() ? s->name : tr("CircuitMaker: %1").arg(s->aliases.join(QStringLiteral(", "))));
    }
    m_tree->expandAll();
}

void LibraryBrowser::collapse()
{
    m_tree->collapseAll();
}

void LibraryBrowser::assignHotkey()
{
    const QString id = selectedId();
    if (id.isEmpty())
        return;
    QStringList keys{tr("(none)")};
    for (char c = '0'; c <= '9'; ++c) keys << QString(QChar(c));
    for (char c = 'a'; c <= 'z'; ++c) keys << QString(QChar(c));
    for (char c = 'A'; c <= 'Z'; ++c) keys << QString(QChar(c));
    const QHash<QChar, QString> current = Hotkeys::load();
    QStringList items;
    for (const QString& k : std::as_const(keys)) {
        const QString used = k.size() == 1 ? current.value(k[0]) : QString();
        const SymbolDef* s = used.isEmpty() ? nullptr : m_library.find(used);
        items << (s ? QStringLiteral("%1  — %2").arg(k, s->name) : k);
    }
    const QChar mine = Hotkeys::keyFor(id);
    const int index = mine.isNull() ? 0 : int(keys.indexOf(QString(mine)));
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("Hotkey"), tr("Key for %1:").arg(m_library.find(id)->name),
                                                 items, index, false, &ok);
    if (!ok)
        return;
    const int picked = int(items.indexOf(choice));
    if (picked <= 0) {
        if (!mine.isNull()) Hotkeys::assign(mine, {});
    } else {
        Hotkeys::assign(keys[picked][0], id);
    }
    reload();
    filter(m_search->text());
}

void LibraryBrowser::filter(const QString& text)
{
    const QString needle = text.trimmed();
    std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) {
        const QString id = item->data(0, kIdRole).toString();
        if (!id.isEmpty()) {
            const SymbolDef* s = m_library.find(id);
            bool match = needle.isEmpty() || s->name.contains(needle, Qt::CaseInsensitive)
                      || s->description.contains(needle, Qt::CaseInsensitive);
            for (const QString& a : s->aliases)
                match = match || a.contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            return match;
        }
        bool any = false;
        for (int i = 0; i < item->childCount(); ++i)
            any = apply(item->child(i)) || any;
        item->setHidden(!any);
        return any;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        apply(m_tree->topLevelItem(i));
}

QString LibraryBrowser::selectedId() const
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

void LibraryBrowser::showSelected()
{
    const SymbolDef* s = m_library.find(selectedId());
    m_place->setEnabled(s != nullptr);
    m_hotkey->setEnabled(s != nullptr);
    if (!s) {
        m_preview->clear();
        m_info->clear();
        return;
    }
    m_preview->setPixmap(SymbolPainter::preview(*s, QSize(180, 100), palette().text().color(), palette().base().color()));
    QString info = QStringLiteral("<b>%1</b>").arg(s->name.toHtmlEscaped());
    if (!s->description.isEmpty())
        info += QStringLiteral("<br>") + s->description.toHtmlEscaped();
    if (!s->aliases.isEmpty())
        info += QStringLiteral("<br><small>%1</small>").arg(tr("CircuitMaker: %1").arg(s->aliases.join(QStringLiteral(", "))).toHtmlEscaped());
    m_info->setText(info);
}

} // namespace cm
