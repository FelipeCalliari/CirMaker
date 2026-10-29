#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace cm {

class LibraryRegistry;

// Category tree with search and a preview of the selected symbol.
class LibraryBrowser : public QWidget {
    Q_OBJECT
public:
    explicit LibraryBrowser(const LibraryRegistry& library, QWidget* parent = nullptr);
    void reload();

signals:
    void placeRequested(const QString& symbolId);

private:
    void filter(const QString& text);
    void showSelected();
    QString selectedId() const;

    const LibraryRegistry& m_library;
    QLineEdit* m_search;
    QTreeWidget* m_tree;
    QLabel* m_preview;
    QLabel* m_info;
    QPushButton* m_place;
};

} // namespace cm
