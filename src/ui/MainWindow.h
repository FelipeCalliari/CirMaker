#pragma once

#include "ui/SchematicScene.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QSpinBox;
class QUndoStack;

namespace cm {

class Circuit;
class LibraryBrowser;
class SchematicView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    // interactive = false reports problems on stderr instead of dialogs.
    bool openFile(const QString& path, bool interactive = true);
    // Renders the schematic to PNG, SVG or PDF (by extension).
    bool exportTo(const QString& path);
    // Replaces the document with one instance of every library symbol.
    void showLibrarySheet();
    SchematicScene* scene() const { return m_scene; }
    QAction* undoAction() const { return m_undoAct; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void updateActions();
    void updateTitle();

    bool maybeSave();
    void newCircuit();
    void open();
    void importCkt();
    bool save();
    bool saveAs();
    void exportImage();
    void exportBom();
    void findAndSelect();
    void print();
    void about();
    void editPreferences();
    void applyPreferences();

    void addRecent(const QString& path);
    void rebuildRecentMenu();

    Circuit* m_circuit;
    QUndoStack* m_undo;
    SchematicScene* m_scene;
    SchematicView* m_view;
    LibraryBrowser* m_browser;
    QString m_path;
    QString m_lastFind;

    QLabel* m_coords;
    QLabel* m_simStatus;
    QSpinBox* m_speed;
    QMenu* m_recentMenu = nullptr;

    QAction *m_new, *m_open, *m_import, *m_save, *m_saveAs, *m_export, *m_print, *m_quit;
    QAction *m_undoAct, *m_redoAct, *m_cut, *m_copy, *m_paste, *m_delete, *m_selectAll, *m_rotate, *m_mirror, *m_props;
    QAction *m_zoomIn, *m_zoomOut, *m_zoomFit, *m_grid;
    QAction *m_toolSelect, *m_toolWire, *m_toolBus, *m_toolNetLabel, *m_toolText;
    QAction *m_find, *m_titleBlock, *m_showTitleBlock, *m_bom;
    QActionGroup* m_tools;
    QAction *m_run, *m_step, *m_reset, *m_stop;
    QAction* m_about;
    QAction* m_prefs;
};

} // namespace cm
