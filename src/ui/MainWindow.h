#pragma once

#include "ui/SchematicScene.h"

#include "sim/analog/NgspiceEngine.h"
#include "sim/analog/SpiceNetlister.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QSpinBox;
class QUndoStack;
class QDockWidget;

namespace cm {

class Circuit;
class CmDeviceLib;
struct SymbolDef;
class LibraryBrowser;
class SchematicView;
class WaveformView;

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
    QAction* runAction() const { return m_run; }
    bool analogRunning() const { return m_analogWatcher.isRunning(); }

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
    void merge();
    void copyImage();
    // The schematic without grid and selection, at most 6000 px a side.
    QImage renderImage() const;
    void exportBom();
    void exportCkt();
    // A .ckt needs CircuitMaker 2000's Device.lib: asks for its folder the
    // first time. writeCkt warns about what CircuitMaker has no device for
    // and writes; false if cancelled or failed.
    bool loadCktLibrary(const QString& title, CmDeviceLib& lib);
    bool writeCkt(const QString& path, const QString& title);
    // the path written, empty if cancelled or failed
    QString writeTextFile(const QString& title, const QString& suffix, const QString& filter, const QString& text);
    void findAndSelect();
    void print();
    void printWaveforms();
    void revert();
    void about();
    void editPreferences();
    void setAnalogMode(bool analog);
    void editAnalysis();
    void runAnalog();
    void analogFinished();
    void showAnalogResults(const QVector<spice::Result>& results);
    void showDigitalWaveforms();
    void applyPreferences();
    // Macros menu
    void newMacro();
    void expandMacro();
    bool saveMacro();
    void editMacro();
    void deleteMacro();
    const SymbolDef* selectedMacro() const;
    void showContextMenu(QGraphicsItem* item, QPoint screenPos);
    void lockMacro();
    void editMacroSymbol();
    // fault simulation
    void deviceFaults(ItemId id);
    void circuitFaults();
    void applyRestrictions();

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

    QAction *m_new, *m_open, *m_import, *m_merge, *m_copyImage, *m_copyWaves, *m_save, *m_saveAs, *m_export, *m_print, *m_quit;
    QAction *m_close, *m_revert, *m_printWaves, *m_showStatusBar;
    QString m_openedPath;       // the file last opened, for File > Revert
    bool m_promptToSave = true;
    QAction *m_undoAct, *m_redoAct, *m_cut, *m_copy, *m_paste, *m_delete, *m_selectAll, *m_rotate, *m_mirror, *m_props;
    QAction *m_zoomIn, *m_zoomOut, *m_zoomFit, *m_grid;
    QAction *m_toolSelect, *m_toolWire, *m_toolBus, *m_toolNetLabel, *m_toolText, *m_toolDelete = nullptr,
            *m_toolZoom = nullptr;
    QAction *m_duplicate, *m_scale, *m_normalSize, *m_collapseTree, *m_refresh, *m_trace, *m_topicHelp;
    QAction *m_autoRoute, *m_groupItems, *m_labelPositions, *m_border, *m_straighten, *m_designations, *m_propDelays, *m_showNodeNames, *m_showPinDots, *m_showPropDelays, *m_exportSpice, *m_exportSubckt, *m_exportPcb, *m_exportCkt,
        *m_home;
    QPointF m_lastCursor;
    QAction *m_find, *m_titleBlock, *m_showTitleBlock, *m_cmDrawings, *m_bom;
    QActionGroup* m_tools;
    QAction *m_run, *m_step, *m_reset, *m_stop;
    QAction* m_about;
    QAction *m_newMacro, *m_expandMacro, *m_saveMacro, *m_editMacro, *m_deleteMacro, *m_lockMacro, *m_macroSymbol;
    QString m_macroName;     // set while the document is a macro's circuit
    QString m_macroLock;     // its Macro Lock digest, kept when it is saved again
    QAction* m_prefs;
    QAction* m_circuitFaults;
    bool m_faultsUnlocked = false;      // the fault lock password was entered this session
    QAction *m_digitalMode, *m_analogMode, *m_analysisSetup, *m_toolProbe;
    WaveformView* m_waves;
    QDockWidget* m_wavesDock;
    QFutureWatcher<QVector<spice::Result>> m_analogWatcher;
    QVector<spice::Run> m_runs;         // the decks being simulated (one per sweep point)
};

} // namespace cm
