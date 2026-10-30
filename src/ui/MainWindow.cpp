#include "ui/MainWindow.h"

#include "sim/digital/Models.h"

#include "core/Bom.h"
#include "core/PcbNetlist.h"
#include "core/Circuit.h"
#include "core/Faults.h"
#include "core/UndoCommands.h"
#include "core/io/CktExporter.h"
#include "core/io/CktImporter.h"
#include "core/io/CmDeviceLib.h"
#include "core/io/SpiceImporter.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "library/Macros.h"
#include "ui/FaultDialogs.h"
#include "ui/LibraryBrowser.h"
#include "ui/MacroSymbolDialog.h"
#include "sim/digital/EventSimulator.h"
#include "ui/AnalysisDialog.h"
#include "ui/ComponentItem.h"
#include "ui/PreferencesDialog.h"
#include "ui/WaveformView.h"
#include "ui/SchematicView.h"
#include "ui/TextNoteItem.h"
#include "ui/TitleBlockItem.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QImage>
#include <QInputDialog>
#include <QSaveFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QSvgGenerator>
#include <QToolBar>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace cm {

namespace {

QIcon themeIcon(const char* name, QStyle::StandardPixmap fallback)
{
    return QIcon::fromTheme(QString::fromLatin1(name), qApp->style()->standardIcon(fallback));
}

// Small drawn icons for actions without a freedesktop theme name.
QIcon drawnIcon(const std::function<void(QPainter&)>& draw)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(qApp->palette().text().color(), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw(p);
    return QIcon(pm);
}

} // namespace

MainWindow::MainWindow()
{
    m_circuit = new Circuit(this);
    m_undo = new QUndoStack(this);
    m_scene = new SchematicScene(m_circuit, m_undo, LibraryRegistry::instance(), this);
    m_view = new SchematicView(m_scene);
    setCentralWidget(m_view);

    m_browser = new LibraryBrowser(LibraryRegistry::instance());
    auto* dock = new QDockWidget(tr("Devices"), this);
    dock->setObjectName(QStringLiteral("devicesDock"));
    dock->setWidget(m_browser);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    connect(m_browser, &LibraryBrowser::placeRequested, this, [this](const QString& id) {
        m_scene->beginPlace(id);
        m_view->setFocus();
    });

    m_waves = new WaveformView;
    m_wavesDock = new QDockWidget(tr("Waveforms"), this);
    m_wavesDock->setObjectName(QStringLiteral("waveformsDock"));
    m_wavesDock->setWidget(m_waves);
    addDockWidget(Qt::BottomDockWidgetArea, m_wavesDock);
    m_wavesDock->hide();
    connect(m_waves, &WaveformView::breakChanged, this, [this](int lane, int condition) {
        if (lane < m_scene->waveformLanes().size())
            m_scene->setBreakCondition(m_scene->waveformLanes()[lane].component, condition);
    });
    connect(&m_analogWatcher, &QFutureWatcher<QVector<spice::Result>>::finished, this, &MainWindow::analogFinished);

    createActions();
    createMenus();
    createToolBars();
    menuBar()->findChild<QMenu*>(QStringLiteral("viewMenu"))->addAction(dock->toggleViewAction());
    menuBar()->findChild<QMenu*>(QStringLiteral("viewMenu"))->addAction(m_wavesDock->toggleViewAction());

    m_coords = new QLabel;
    m_simStatus = new QLabel;
    statusBar()->addPermanentWidget(m_simStatus);
    statusBar()->addPermanentWidget(m_coords);

    connect(m_scene, &SchematicScene::cursorMoved, this,
            [this](QPointF p) { m_coords->setText(QStringLiteral("x %1  y %2").arg(p.x()).arg(p.y())); });
    connect(m_scene, &SchematicScene::statusMessage, this, [this](const QString& t) { statusBar()->showMessage(t, 8000); });
    connect(m_scene, &SchematicScene::simulationChanged, this, &MainWindow::updateActions);
    connect(m_scene, &SchematicScene::simTimeChanged, this, [this](quint64 t) {
        m_simStatus->setText(tr("t = %1 ticks").arg(t));
        showDigitalWaveforms();
    });
    connect(m_scene, &SchematicScene::toolChanged, this, [this](SchematicScene::Tool t) {
        m_toolSelect->setChecked(t == SchematicScene::Tool::Select || t == SchematicScene::Tool::Place);
        m_toolWire->setChecked(t == SchematicScene::Tool::Wire);
        m_toolBus->setChecked(t == SchematicScene::Tool::Bus);
        m_toolNetLabel->setChecked(t == SchematicScene::Tool::NetLabel);
        m_toolProbe->setChecked(t == SchematicScene::Tool::Probe);
        if (m_toolDelete) m_toolDelete->setChecked(t == SchematicScene::Tool::Delete);
        if (m_toolZoom) m_toolZoom->setChecked(t == SchematicScene::Tool::Zoom);
        m_toolText->setChecked(t == SchematicScene::Tool::Text);
    });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updateActions);
    connect(m_undo, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    rebuildRecentMenu();
    applyPreferences();
    updateActions();
    updateTitle();
    m_view->resetZoom();
    m_view->centerOn(200, 150);
    m_view->setFocus();             // device hotkeys go to the schematic
}

MainWindow::~MainWindow()
{
    // children (undo stack, scene) emit signals while being destroyed; this
    // window is no longer a MainWindow by then
    m_analogWatcher.waitForFinished();
    m_undo->disconnect(this);
    m_scene->disconnect(this);
    m_circuit->disconnect(this);
    m_scene->stopSimulation();
}

void MainWindow::createActions()
{
    m_new = new QAction(themeIcon("document-new", QStyle::SP_FileIcon), tr("&New"), this);
    m_new->setShortcut(QKeySequence::New);
    connect(m_new, &QAction::triggered, this, &MainWindow::newCircuit);

    m_open = new QAction(themeIcon("document-open", QStyle::SP_DialogOpenButton), tr("&Open..."), this);
    m_open->setShortcut(QKeySequence::Open);
    connect(m_open, &QAction::triggered, this, &MainWindow::open);

    m_import = new QAction(tr("&Import CircuitMaker 2000 (.ckt)..."), this);
    m_import->setShortcut(Qt::CTRL | Qt::Key_I);
    connect(m_import, &QAction::triggered, this, &MainWindow::importCkt);

    m_merge = new QAction(tr("&Merge..."), this);
    m_merge->setStatusTip(tr("Insert another circuit below this one"));
    connect(m_merge, &QAction::triggered, this, &MainWindow::merge);

    m_save = new QAction(themeIcon("document-save", QStyle::SP_DialogSaveButton), tr("&Save"), this);
    m_save->setShortcut(QKeySequence::Save);
    connect(m_save, &QAction::triggered, this, &MainWindow::save);

    m_saveAs = new QAction(tr("Save &As..."), this);
    m_saveAs->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAs, &QAction::triggered, this, &MainWindow::saveAs);

    m_export = new QAction(tr("&Export Schematic (PNG, SVG, PDF)..."), this);
    connect(m_export, &QAction::triggered, this, &MainWindow::exportImage);

    m_copyImage = new QAction(tr("Copy Schematic as &Image"), this);
    m_copyImage->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
    connect(m_copyImage, &QAction::triggered, this, &MainWindow::copyImage);

    m_copyWaves = new QAction(tr("Copy &Waveforms as Image"), this);
    connect(m_copyWaves, &QAction::triggered, this, [this] {
        if (!m_wavesDock->isVisible()) {
            statusBar()->showMessage(tr("Open the Waveforms window first"), 4000);
            return;
        }
        QApplication::clipboard()->setImage(m_waves->grab().toImage());
        statusBar()->showMessage(tr("Waveforms copied to the clipboard"), 4000);
    });

    m_print = new QAction(themeIcon("document-print", QStyle::SP_FileIcon), tr("&Print Schematic..."), this);
    m_print->setShortcut(QKeySequence::Print);
    connect(m_print, &QAction::triggered, this, &MainWindow::print);

    m_quit = new QAction(tr("E&xit"), this);
    m_quit->setShortcut(Qt::ALT | Qt::Key_F4);
    connect(m_quit, &QAction::triggered, this, &QWidget::close);

    // own actions instead of QUndoStack's, which re-enable themselves even
    // while a simulation holds on to the circuit
    m_undoAct = new QAction(themeIcon("edit-undo", QStyle::SP_ArrowBack), tr("&Undo"), this);
    m_undoAct->setShortcut(QKeySequence::Undo);
    connect(m_undoAct, &QAction::triggered, this, [this] {
        if (!m_scene->simulating())
            m_undo->undo();
    });
    m_redoAct = new QAction(themeIcon("edit-redo", QStyle::SP_ArrowForward), tr("&Redo"), this);
    m_redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(m_redoAct, &QAction::triggered, this, [this] {
        if (!m_scene->simulating())
            m_undo->redo();
    });
    connect(m_undo, &QUndoStack::indexChanged, this, &MainWindow::updateActions);

    m_cut = new QAction(themeIcon("edit-cut", QStyle::SP_TrashIcon), tr("Cu&t"), this);
    m_cut->setShortcut(QKeySequence::Cut);
    connect(m_cut, &QAction::triggered, m_scene, &SchematicScene::cutSelection);
    m_copy = new QAction(themeIcon("edit-copy", QStyle::SP_FileIcon), tr("&Copy"), this);
    m_copy->setShortcut(QKeySequence::Copy);
    connect(m_copy, &QAction::triggered, m_scene, &SchematicScene::copySelection);
    m_paste = new QAction(themeIcon("edit-paste", QStyle::SP_FileIcon), tr("&Paste"), this);
    m_paste->setShortcut(QKeySequence::Paste);
    connect(m_paste, &QAction::triggered, m_scene, &SchematicScene::paste);
    m_delete = new QAction(themeIcon("edit-delete", QStyle::SP_TrashIcon), tr("&Delete"), this);
    m_delete->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    connect(m_delete, &QAction::triggered, m_scene, &SchematicScene::deleteSelection);
    m_selectAll = new QAction(tr("Select &All"), this);
    m_selectAll->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAll, &QAction::triggered, m_scene, &SchematicScene::selectAll);

    m_rotate = new QAction(drawnIcon([](QPainter& p) {
                               p.drawArc(QRectF(7, 7, 18, 18), 90 * 16, 270 * 16);
                               p.drawLine(QPointF(16, 7), QPointF(21, 3));
                               p.drawLine(QPointF(16, 7), QPointF(21, 11));
                           }),
                           tr("&Rotate 90°"), this);
    m_rotate->setShortcut(Qt::CTRL | Qt::Key_R);
    connect(m_rotate, &QAction::triggered, m_scene, &SchematicScene::rotateSelection);
    m_mirror = new QAction(drawnIcon([](QPainter& p) {
                               p.drawLine(QPointF(16, 4), QPointF(16, 28));
                               p.drawPolygon(QPolygonF({QPointF(12, 8), QPointF(12, 24), QPointF(4, 24)}));
                               p.drawPolygon(QPolygonF({QPointF(20, 8), QPointF(20, 24), QPointF(28, 24)}));
                           }),
                           tr("&Mirror"), this);
    m_mirror->setShortcut(Qt::CTRL | Qt::Key_M);
    connect(m_mirror, &QAction::triggered, m_scene, &SchematicScene::mirrorSelection);
    m_props = new QAction(tr("&Properties..."), this);
    m_props->setShortcut(Qt::ALT | Qt::Key_Return);
    connect(m_props, &QAction::triggered, this, [this] {
        const auto sel = m_scene->selectedItems();
        if (sel.size() == 1)
            m_scene->editItem(sel.first());
    });

    m_zoomIn = new QAction(themeIcon("zoom-in", QStyle::SP_ArrowUp), tr("Zoom &In"), this);
    m_zoomIn->setShortcuts({QKeySequence(Qt::Key_PageUp), QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_view->zoomBy(1.25); });
    m_zoomOut = new QAction(themeIcon("zoom-out", QStyle::SP_ArrowDown), tr("Zoom &Out"), this);
    m_zoomOut->setShortcuts({QKeySequence(Qt::Key_PageDown), QKeySequence::ZoomOut});
    connect(m_zoomOut, &QAction::triggered, this, [this] { m_view->zoomBy(0.8); });
    m_zoomFit = new QAction(themeIcon("zoom-fit-best", QStyle::SP_DesktopIcon), tr("&Fit to Window"), this);
    m_zoomFit->setShortcuts({QKeySequence(Qt::Key_F4), QKeySequence(Qt::CTRL | Qt::Key_0)});
    connect(m_zoomFit, &QAction::triggered, m_view, &SchematicView::zoomToFit);
    m_grid = new QAction(tr("Show &Grid"), this);
    m_grid->setCheckable(true);
    m_grid->setChecked(true);
    connect(m_grid, &QAction::toggled, this, [this](bool on) {
        m_scene->setGridVisible(on);
        Preferences p = Preferences::load();
        p.gridVisible = on;
        p.save();
    });

    m_tools = new QActionGroup(this);
    m_toolSelect = new QAction(drawnIcon([](QPainter& p) {
                                   p.setBrush(p.pen().color());
                                   p.drawPolygon(QPolygonF({QPointF(8, 4), QPointF(8, 24), QPointF(13, 19), QPointF(17, 28),
                                                            QPointF(20, 27), QPointF(16, 18), QPointF(23, 18)}));
                               }),
                               tr("&Arrow"), m_tools);
    m_toolSelect->setShortcut(Qt::ALT | Qt::Key_A);
    m_toolWire = new QAction(drawnIcon([](QPainter& p) {
                                 p.drawPolyline(QPolygonF({QPointF(4, 8), QPointF(16, 8), QPointF(16, 24), QPointF(28, 24)}));
                                 p.setBrush(p.pen().color());
                                 p.drawEllipse(QPointF(16, 8), 2.5, 2.5);
                             }),
                             tr("&Wire"), m_tools);
    m_toolWire->setShortcut(Qt::ALT | Qt::Key_W);
    m_toolBus = new QAction(drawnIcon([](QPainter& p) {
                                QPen pen = p.pen();
                                pen.setWidthF(5);
                                p.setPen(pen);
                                p.drawLine(QPointF(4, 16), QPointF(28, 16));
                            }),
                            tr("&Bus"), m_tools);
    m_toolBus->setShortcut(Qt::ALT | Qt::Key_B);
    m_toolBus->setToolTip(tr("Bus: wires ending on it are joined by entry name"));
    m_toolNetLabel = new QAction(drawnIcon([](QPainter& p) {
                                     QFont f = p.font();
                                     f.setPixelSize(13);
                                     f.setBold(true);
                                     p.setFont(f);
                                     p.drawLine(QPointF(4, 24), QPointF(28, 24));
                                     p.drawText(QRectF(0, 2, 32, 20), Qt::AlignCenter, QStringLiteral("N1"));
                                 }),
                                 tr("&Net Label"), m_tools);
    m_toolNetLabel->setShortcut(Qt::ALT | Qt::Key_N);
    m_toolNetLabel->setToolTip(tr("Net label: points with the same name are connected"));
    m_toolProbe = new QAction(drawnIcon([](QPainter& p) {
                                  p.setBrush(WaveformView::traceColor(0));
                                  p.drawPolygon(QPolygonF({QPointF(6, 28), QPointF(10, 14), QPointF(26, 14), QPointF(26, 4),
                                                           QPointF(6, 4)}));
                              }),
                              tr("&Probe"), m_tools);
    m_toolProbe->setShortcut(Qt::ALT | Qt::Key_P);
    m_toolProbe->setToolTip(tr("Probe: click a wire to plot its voltage in analog mode; click a probe again to remove it"));
    m_toolText = new QAction(drawnIcon([](QPainter& p) {
                                 QFont f = p.font();
                                 f.setPixelSize(24);
                                 f.setBold(true);
                                 p.setFont(f);
                                 p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, QStringLiteral("T"));
                             }),
                             tr("&Text"), m_tools);
    m_toolText->setShortcut(Qt::ALT | Qt::Key_T);
    for (QAction* a : m_tools->actions())
        a->setCheckable(true);
    m_toolSelect->setChecked(true);
    connect(m_toolSelect, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Select); });
    connect(m_toolWire, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Wire); });
    connect(m_toolText, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Text); });
    connect(m_toolBus, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Bus); });
    connect(m_toolNetLabel, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::NetLabel); });
    connect(m_toolProbe, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Probe); });
    m_toolDelete = new QAction(themeIcon("edit-delete", QStyle::SP_TrashIcon), tr("&Delete Tool"), m_tools);
    m_toolDelete->setShortcut(Qt::ALT | Qt::Key_D);
    m_toolDelete->setToolTip(tr("Delete tool: click an item to delete it (hold Shift to snip one wire segment)"));
    m_toolDelete->setCheckable(true);
    connect(m_toolDelete, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Delete); });
    m_toolZoom = new QAction(themeIcon("zoom-in", QStyle::SP_ArrowUp), tr("&Zoom Tool"), m_tools);
    m_toolZoom->setShortcut(Qt::ALT | Qt::Key_Z);
    m_toolZoom->setToolTip(tr("Zoom tool: click to zoom in (hold Shift to zoom out)"));
    m_toolZoom->setCheckable(true);
    connect(m_toolZoom, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Zoom); });
    connect(m_scene, &SchematicScene::zoomRequested, this, [this](QPointF at, double factor) {
        m_view->zoomBy(factor);
        m_view->centerOn(at);
    });

    connect(m_scene, &SchematicScene::contextMenuRequested, this, &MainWindow::showContextMenu);

    m_duplicate = new QAction(tr("Dup&licate"), this);
    m_duplicate->setShortcut(Qt::CTRL | Qt::Key_D);
    connect(m_duplicate, &QAction::triggered, m_scene, &SchematicScene::duplicateSelection);
    m_scale = new QAction(tr("&Display Scale..."), this);
    m_scale->setShortcut(Qt::Key_F2);
    connect(m_scale, &QAction::triggered, this, [this] {
        bool ok = false;
        const int percent = QInputDialog::getInt(this, tr("Display scale"), tr("Zoom (%):"), qRound(m_view->zoom() * 100), 10,
                                                 2000, 10, &ok);
        if (ok)
            m_view->zoomBy(percent / 100.0 / m_view->zoom());
    });
    m_normalSize = new QAction(tr("&Normal Size"), this);
    m_normalSize->setShortcut(Qt::Key_F3);
    connect(m_normalSize, &QAction::triggered, m_view, &SchematicView::normalSize);
    m_collapseTree = new QAction(tr("Collapse Device Tree"), this);
    m_collapseTree->setShortcut(Qt::Key_F6);
    connect(m_collapseTree, &QAction::triggered, m_browser, &LibraryBrowser::collapse);
    m_refresh = new QAction(tr("&Refresh Screen"), this);
    m_refresh->setShortcuts({QKeySequence(Qt::Key_F7), QKeySequence(Qt::Key_End)});
    connect(m_refresh, &QAction::triggered, this, [this] { m_scene->update(); m_view->viewport()->update(); });
    m_autoRoute = new QAction(tr("&Auto Wire Routing"), this);
    m_autoRoute->setCheckable(true);
    m_autoRoute->setStatusTip(tr("A wire from a pin to another pin or wire goes around the devices"));
    m_autoRoute->setChecked(QSettings().value(QStringLiteral("prefs/autoRoute"), true).toBool());
    m_scene->setAutoRoute(m_autoRoute->isChecked());
    connect(m_autoRoute, &QAction::toggled, this, [this](bool on) {
        m_scene->setAutoRoute(on);
        QSettings().setValue(QStringLiteral("prefs/autoRoute"), on);
    });
    m_groupItems = new QAction(tr("&Group Items"), this);
    m_groupItems->setStatusTip(tr("Puts the selected gates in the same package (U1A, U1B...)"));
    connect(m_groupItems, &QAction::triggered, this, [this] {
        QString error;
        if (!m_scene->groupSelection(&error) && !error.isEmpty())
            QMessageBox::information(this, tr("Group items"), error);
    });
    m_labelPositions = new QAction(tr("Set &Label Positions"), this);
    m_labelPositions->setStatusTip(tr("Puts the designators and labels of the selected devices back in their standard places"));
    connect(m_labelPositions, &QAction::triggered, m_scene, &SchematicScene::resetLabelPositions);
    m_border = new QAction(tr("&Border"), this);
    m_border->setCheckable(true);
    m_border->setStatusTip(tr("Map border with lettered and numbered zones around the schematic (also printed and exported)"));
    m_border->setChecked(QSettings().value(QStringLiteral("prefs/border"), false).toBool());
    m_scene->setBorderVisible(m_border->isChecked());
    connect(m_border, &QAction::toggled, this, [this](bool on) {
        m_scene->setBorderVisible(on);
        QSettings().setValue(QStringLiteral("prefs/border"), on);
    });
    m_straighten = new QAction(tr("Straighten &Wires"), this);
    connect(m_straighten, &QAction::triggered, m_scene, &SchematicScene::straightenWires);
    m_designations = new QAction(tr("Set Desi&gnations..."), this);
    connect(m_designations, &QAction::triggered, this, [this] {
        QStringList prefixes{tr("(all)")};
        for (const Component& c : m_circuit->components())
            if (const SymbolDef* s = LibraryRegistry::instance().find(c.symbolId); s && !s->prefix.isEmpty() && !s->netLabel
                && !prefixes.contains(s->prefix))
                prefixes << s->prefix;
        bool ok = false;
        const QString prefix = QInputDialog::getItem(this, tr("Set designations"),
                                                     tr("Renumber the selected devices (or all) with prefix:"), prefixes, 0, false, &ok);
        if (!ok) return;
        const int first = QInputDialog::getInt(this, tr("Set designations"), tr("Starting number:"), 1, 0, 99999, 1, &ok);
        if (ok) m_scene->setDesignations(prefix == prefixes.first() ? QString() : prefix, first);
    });
    m_propDelays = new QAction(tr("Set Prop Dela&ys..."), this);
    connect(m_propDelays, &QAction::triggered, this, [this] {
        bool ok = false;
        const int ticks = QInputDialog::getInt(this, tr("Propagation delay"),
                                               tr("Delay of the selected digital devices (ticks):"), 1, 1, 14, 1, &ok);
        if (ok) m_scene->setPropDelays(ticks);
    });
    m_showNodeNames = new QAction(tr("Show &Node Names"), this);
    m_showNodeNames->setCheckable(true);
    m_showNodeNames->setChecked(QSettings().value(QStringLiteral("prefs/showNodeNames"), false).toBool());
    m_scene->setShowNodeNames(m_showNodeNames->isChecked());
    connect(m_showNodeNames, &QAction::toggled, this, [this](bool on) {
        m_scene->setShowNodeNames(on);
        QSettings().setValue(QStringLiteral("prefs/showNodeNames"), on);
    });
    m_showPinDots = new QAction(tr("Show &Pin Dots"), this);
    m_showPinDots->setCheckable(true);
    m_showPinDots->setChecked(QSettings().value(QStringLiteral("prefs/showPinDots"), true).toBool());
    m_scene->setShowPinDots(m_showPinDots->isChecked());
    connect(m_showPinDots, &QAction::toggled, this, [this](bool on) {
        m_scene->setShowPinDots(on);
        QSettings().setValue(QStringLiteral("prefs/showPinDots"), on);
    });
    m_exportSpice = new QAction(tr("&SPICE Netlist..."), this);
    connect(m_exportSpice, &QAction::triggered, this, [this] {
        m_scene->refreshNetlist();
        const spice::Deck deck = spice::build(m_scene->simCircuit(), LibraryRegistry::instance(), m_scene->netlist());
        writeTextFile(tr("Export SPICE netlist"), QStringLiteral("cir"), tr("SPICE netlist (*.cir *.sp)"), deck.text);
    });
    m_exportPcb = new QAction(tr("&PCB Netlist..."), this);
    connect(m_exportPcb, &QAction::triggered, this, [this] {
        m_scene->refreshNetlist();
        // CircuitMaker's formats; the choice is remembered
        const QStringList formats{tr("Protel"), tr("Tango"), tr("PADS"), tr("OrCAD PCB II"), tr("CadNetix"), tr("Calay"),
                                  tr("Calay 90")};
        QSettings settings;
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Export PCB netlist"), tr("Format:"), formats,
                                                     settings.value(QStringLiteral("export/pcbFormat"), 0).toInt(), false, &ok);
        if (!ok) return;
        const int format = int(formats.indexOf(chosen));
        settings.setValue(QStringLiteral("export/pcbFormat"), format);
        const PcbNetlist::Format f = PcbNetlist::Format(format);
        const QString path = writeTextFile(tr("Export PCB netlist"), format == 2 ? QStringLiteral("asc") : QStringLiteral("net"),
                                           tr("PCB netlist (*.net *.asc);;All files (*)"),
                                           PcbNetlist::write(m_scene->simCircuit(), LibraryRegistry::instance(), m_scene->netlist(), f));
        // Calay keeps the parts in a .cmp file beside the netlist
        if (!path.isEmpty() && (f == PcbNetlist::Format::Calay || f == PcbNetlist::Format::Calay90)) {
            QFile cmp(QFileInfo(path).path() + u'/' + QFileInfo(path).completeBaseName() + QStringLiteral(".cmp"));
            if (cmp.open(QIODevice::WriteOnly | QIODevice::Text))
                cmp.write(PcbNetlist::calayComponents(m_scene->simCircuit(), LibraryRegistry::instance(), m_scene->netlist()).toUtf8());
        }
    });
    m_exportSubckt = new QAction(tr("SPICE &Subcircuit..."), this);
    m_exportSubckt->setStatusTip(tr("The circuit as a .subckt whose pins are its Input/Output ports"));
    connect(m_exportSubckt, &QAction::triggered, this, [this] {
        m_scene->refreshNetlist();
        const QString name = m_macroName.isEmpty() ? (m_path.isEmpty() ? tr("circuit") : QFileInfo(m_path).completeBaseName())
                                                   : m_macroName;
        QString error;
        const QString text = spice::subcircuit(m_scene->simCircuit(), LibraryRegistry::instance(), m_scene->netlist(), name, &error);
        if (text.isEmpty()) {
            QMessageBox::warning(this, tr("Export SPICE subcircuit"), error);
            return;
        }
        writeTextFile(tr("Export SPICE subcircuit"), QStringLiteral("sub"), tr("SPICE subcircuit (*.sub *.lib *.ckt)"), text);
    });
    m_exportCkt = new QAction(tr("&CircuitMaker 2000 (.ckt)..."), this);
    m_exportCkt->setStatusTip(tr("A schematic CircuitMaker 2000 opens, built from the Device.lib of its installation"));
    connect(m_exportCkt, &QAction::triggered, this, &MainWindow::exportCkt);
    m_home = new QAction(tr("Center on Cursor"), this);
    m_home->setShortcut(Qt::Key_Home);
    connect(m_home, &QAction::triggered, this, [this] { m_view->centerOn(m_lastCursor); });
    connect(m_scene, &SchematicScene::cursorMoved, this, [this](QPointF p) { m_lastCursor = p; });

    m_trace = new QAction(tr("&Trace"), this);
    m_trace->setShortcut(Qt::Key_F11);
    m_trace->setCheckable(true);
    m_trace->setChecked(true);
    m_trace->setToolTip(tr("Colour wires by logic level while simulating"));
    connect(m_trace, &QAction::toggled, m_scene, &SchematicScene::setTrace);
    m_topicHelp = new QAction(tr("&Topic for Selected Item"), this);
    m_topicHelp->setShortcut(Qt::Key_F1);
    connect(m_topicHelp, &QAction::triggered, this, [this] {
        for (QGraphicsItem* it : m_scene->selectedItems())
            if (auto* c = dynamic_cast<ComponentItem*>(it)) {
                QMessageBox::information(this, c->symbol()->name,
                                         c->symbol()->description.isEmpty() ? c->symbol()->name : c->symbol()->description);
                return;
            }
        statusBar()->showMessage(tr("Select a device to see its description"), 4000);
    });

    m_find = new QAction(themeIcon("edit-find", QStyle::SP_FileDialogContentsView), tr("&Find and Select..."), this);
    m_find->setShortcut(QKeySequence::Find);
    connect(m_find, &QAction::triggered, this, &MainWindow::findAndSelect);
    m_titleBlock = new QAction(tr("Title &Block..."), this);
    connect(m_titleBlock, &QAction::triggered, m_scene, &SchematicScene::editTitleBlock);
    m_showTitleBlock = new QAction(tr("Show &Title Block"), this);
    m_showTitleBlock->setCheckable(true);
    connect(m_showTitleBlock, &QAction::toggled, m_scene, &SchematicScene::setTitleBlockVisible);
    connect(m_circuit, &Circuit::titleBlockChanged, this, [this] {
        const QSignalBlocker block(m_showTitleBlock);
        m_showTitleBlock->setChecked(m_circuit->titleBlock().visible);
    });
    m_bom = new QAction(tr("&Bill of Materials (CSV)..."), this);
    connect(m_bom, &QAction::triggered, this, &MainWindow::exportBom);

    m_run = new QAction(themeIcon("media-playback-start", QStyle::SP_MediaPlay), tr("&Run"), this);
    m_run->setShortcut(Qt::Key_F10);
    connect(m_run, &QAction::triggered, this, [this] {
        if (m_circuit->analysis().analogMode)
            runAnalog();
        else
            m_scene->running() ? m_scene->pause() : m_scene->run();
    });

    auto* modes = new QActionGroup(this);
    m_digitalMode = new QAction(tr("&Digital Mode"), modes);
    m_analogMode = new QAction(tr("&Analog Mode (SPICE)"), modes);
    for (QAction* a : {m_digitalMode, m_analogMode})
        a->setCheckable(true);
    m_digitalMode->setChecked(true);
    connect(m_digitalMode, &QAction::triggered, this, [this] { setAnalogMode(false); });
    connect(m_analogMode, &QAction::triggered, this, [this] { setAnalogMode(true); });
    connect(m_circuit, &Circuit::analysisChanged, this, &MainWindow::updateActions);
    m_analysisSetup = new QAction(tr("Analog A&nalysis..."), this);
    m_analysisSetup->setShortcut(Qt::Key_F8);
    connect(m_analysisSetup, &QAction::triggered, this, &MainWindow::editAnalysis);
    m_step = new QAction(themeIcon("media-skip-forward", QStyle::SP_MediaSkipForward), tr("S&tep"), this);
    m_step->setShortcut(Qt::Key_F9);
    m_step->setToolTip(tr("Advance to the next logic change"));
    connect(m_step, &QAction::triggered, m_scene, &SchematicScene::stepSimulation);
    m_reset = new QAction(themeIcon("view-refresh", QStyle::SP_BrowserReload), tr("R&eset"), this);
    m_reset->setShortcut(Qt::CTRL | Qt::Key_Q);
    connect(m_reset, &QAction::triggered, m_scene, &SchematicScene::resetSimulation);
    m_stop = new QAction(themeIcon("media-playback-stop", QStyle::SP_MediaStop), tr("&Stop (back to editing)"), this);
    m_stop->setShortcut(Qt::SHIFT | Qt::Key_F10);
    connect(m_stop, &QAction::triggered, m_scene, &SchematicScene::stopSimulation);

    m_circuitFaults = new QAction(tr("Circuit &Fault Data..."), this);
    m_circuitFaults->setStatusTip(tr("Fault lock password, options disabled for students, analog fault values"));
    connect(m_circuitFaults, &QAction::triggered, this, &MainWindow::circuitFaults);
    connect(m_circuit, &Circuit::faultSettingsChanged, this, &MainWindow::applyRestrictions);

    m_prefs = new QAction(themeIcon("preferences-system", QStyle::SP_FileDialogDetailedView), tr("Pre&ferences..."), this);
    m_prefs->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::Key_Comma)});
    m_prefs->setMenuRole(QAction::PreferencesRole);
    connect(m_prefs, &QAction::triggered, this, &MainWindow::editPreferences);

    m_newMacro = new QAction(tr("&New Macro..."), this);
    m_newMacro->setShortcut(Qt::CTRL | Qt::Key_H);
    m_newMacro->setStatusTip(tr("Turn the circuit into a device; its Input and Output ports become the pins"));
    connect(m_newMacro, &QAction::triggered, this, &MainWindow::newMacro);
    m_expandMacro = new QAction(tr("&Expand Macro"), this);
    m_expandMacro->setShortcut(Qt::CTRL | Qt::Key_E);
    m_expandMacro->setStatusTip(tr("Open the circuit inside the selected macro device"));
    connect(m_expandMacro, &QAction::triggered, this, &MainWindow::expandMacro);
    m_saveMacro = new QAction(tr("&Save Macro"), this);
    m_saveMacro->setStatusTip(tr("Save the work area as the macro being edited"));
    connect(m_saveMacro, &QAction::triggered, this, &MainWindow::saveMacro);
    m_editMacro = new QAction(tr("Edit &Macro..."), this);
    m_editMacro->setStatusTip(tr("Rename the macro being edited; a new name saves a new macro"));
    connect(m_editMacro, &QAction::triggered, this, &MainWindow::editMacro);
    m_macroSymbol = new QAction(tr("Edit Macro S&ymbol..."), this);
    m_macroSymbol->setStatusTip(tr("Arrange the pins of the selected macro's symbol"));
    connect(m_macroSymbol, &QAction::triggered, this, &MainWindow::editMacroSymbol);
    m_lockMacro = new QAction(tr("Macro &Lock..."), this);
    m_lockMacro->setStatusTip(tr("Protects the selected macro with a password: it cannot be expanded without it"));
    connect(m_lockMacro, &QAction::triggered, this, &MainWindow::lockMacro);
    m_deleteMacro = new QAction(tr("&Delete Macro..."), this);
    connect(m_deleteMacro, &QAction::triggered, this, &MainWindow::deleteMacro);

    m_about = new QAction(tr("&About CirMaker Qt"), this);
    connect(m_about, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({m_new, m_open});
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addActions({m_import, m_merge});
    file->addSeparator();
    file->addActions({m_save, m_saveAs});
    file->addSeparator();
    QMenu* exportMenu = file->addMenu(tr("E&xport"));
    exportMenu->addActions({m_export, m_exportSpice, m_exportSubckt, m_exportPcb, m_exportCkt});
    file->addActions({m_bom, m_print});
    file->addSeparator();
    file->addAction(m_quit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({m_undoAct, m_redoAct});
    edit->addSeparator();
    edit->addActions({m_cut, m_copy, m_copyImage, m_copyWaves, m_paste, m_delete, m_duplicate, m_selectAll, m_find});
    edit->addSeparator();
    edit->addActions({m_rotate, m_mirror, m_props, m_groupItems, m_labelPositions, m_straighten, m_designations, m_propDelays});
    edit->addSeparator();
    edit->addAction(m_titleBlock);
    edit->addSeparator();
    edit->addAction(m_prefs);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName(QStringLiteral("viewMenu"));
    view->addActions({m_scale, m_normalSize, m_zoomFit, m_zoomIn, m_zoomOut, m_home, m_refresh, m_collapseTree});
    view->addSeparator();
    view->addActions({m_grid, m_border, m_showTitleBlock});
    view->addSeparator();

    QMenu* tools = menuBar()->addMenu(tr("&Options"));
    QMenu* cursorTools = tools->addMenu(tr("Cursor &Tools"));
    cursorTools->addActions(m_tools->actions());
    tools->addSeparator();
    tools->addActions({m_showNodeNames, m_showPinDots});
    tools->addSeparator();
    tools->addAction(m_autoRoute);
    tools->addAction(m_circuitFaults);
    tools->addAction(m_prefs);

    QMenu* simulation = menuBar()->addMenu(tr("&Simulation"));
    simulation->addActions({m_digitalMode, m_analogMode});
    simulation->addSeparator();
    simulation->addActions({m_run, m_step, m_trace, m_reset, m_stop});
    // CircuitMaker Digital Options: how the SCOPE breakpoint boxes combine
    QMenu* breakMenu = simulation->addMenu(tr("&Breakpoint Type"));
    auto* breakGroup = new QActionGroup(breakMenu);
    const std::pair<QString, std::pair<bool, bool>> types[] = {
        {tr("Level-&And"), {false, true}}, {tr("Level-&Or"), {false, false}},
        {tr("&Edge-And"), {true, true}}, {tr("Edge-O&r"), {true, false}}};
    for (const auto& [name, mode] : types) {
        QAction* a = breakMenu->addAction(name);
        a->setCheckable(true);
        breakGroup->addAction(a);
        const auto [edge, all] = mode;
        auto sync = [this, a, edge, all] {
            a->setChecked(m_circuit->analysis().breakEdge == edge && m_circuit->analysis().breakAll == all);
        };
        sync();
        connect(m_circuit, &Circuit::analysisChanged, a, sync);
        connect(a, &QAction::triggered, this, [this, edge, all] {
            AnalysisSettings s = m_circuit->analysis();
            s.breakEdge = edge;
            s.breakAll = all;
            m_undo->push(new SetAnalysisCommand(m_circuit, s, tr("Breakpoint type")));
        });
    }
    breakMenu->setToolTipsVisible(true);
    breakMenu->menuAction()->setStatusTip(
        tr("Breakpoints are set with the boxes beside the SCOPE names in the Waveforms window"));
    simulation->addSeparator();
    simulation->addAction(m_analysisSetup);

    QMenu* macrosMenu = menuBar()->addMenu(tr("&Macros"));
    macrosMenu->addActions({m_newMacro, m_editMacro, m_saveMacro, m_expandMacro, m_macroSymbol, m_lockMacro});
    macrosMenu->addSeparator();
    macrosMenu->addAction(m_deleteMacro);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_topicHelp);
    help->addAction(m_about);
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createToolBars()
{
    QToolBar* main = addToolBar(tr("Main"));
    main->setObjectName(QStringLiteral("mainToolBar"));
    main->addActions({m_new, m_open, m_save});
    main->addSeparator();
    main->addActions({m_undoAct, m_redoAct});
    main->addSeparator();
    main->addActions(m_tools->actions());
    main->addActions({m_rotate, m_mirror});
    main->addSeparator();
    main->addActions({m_zoomIn, m_zoomOut, m_zoomFit});

    QToolBar* sim = addToolBar(tr("Simulation"));
    sim->setObjectName(QStringLiteral("simToolBar"));
    auto* modeBox = new QComboBox;
    modeBox->addItems({tr("Digital"), tr("Analog")});
    modeBox->setToolTip(tr("Simulation mode"));
    connect(modeBox, &QComboBox::activated, this, [this](int i) { setAnalogMode(i == 1); });
    connect(m_circuit, &Circuit::analysisChanged, modeBox, [this, modeBox] {
        modeBox->setCurrentIndex(m_circuit->analysis().analogMode ? 1 : 0);
    });
    sim->addWidget(modeBox);
    sim->addActions({m_run, m_step, m_reset, m_stop, m_analysisSetup});
    sim->addWidget(new QLabel(tr(" Speed: ")));
    m_speed = new QSpinBox;
    m_speed->setRange(1, 1000);
    m_speed->setValue(m_scene->ticksPerFrame());
    m_speed->setSuffix(tr(" ticks/frame"));
    m_speed->setToolTip(tr("Simulation ticks advanced every 30 ms while running"));
    connect(m_speed, &QSpinBox::valueChanged, m_scene, &SchematicScene::setTicksPerFrame);
    sim->addWidget(m_speed);
}

void MainWindow::updateActions()
{
    const bool simulating = m_scene->simulating();
    const bool hasSel = m_scene->hasSelection();
    for (QAction* a : {m_new, m_open, m_import, m_merge, m_cut, m_paste, m_delete, m_selectAll, m_props, m_find, m_titleBlock,
                       m_showTitleBlock})
        a->setEnabled(!simulating);
    m_undoAct->setEnabled(!simulating && m_undo->canUndo());
    m_redoAct->setEnabled(!simulating && m_undo->canRedo());
    m_undoAct->setText(m_undo->canUndo() ? tr("&Undo %1").arg(m_undo->undoText()) : tr("&Undo"));
    m_redoAct->setText(m_undo->canRedo() ? tr("&Redo %1").arg(m_undo->redoText()) : tr("&Redo"));
    for (QAction* a : {m_cut, m_copy, m_delete, m_rotate, m_mirror})
        a->setEnabled(!simulating && hasSel);
    m_props->setEnabled(!simulating && m_scene->selectedItems().size() == 1);
    for (QAction* a : {m_newMacro, m_saveMacro, m_editMacro, m_deleteMacro})
        a->setEnabled(!simulating);
    m_editMacro->setEnabled(!simulating && !m_macroName.isEmpty());
    m_expandMacro->setEnabled(!simulating && selectedMacro());
    m_lockMacro->setEnabled(!simulating && selectedMacro());
    m_macroSymbol->setEnabled(!simulating && selectedMacro());
    for (QAction* a : m_tools->actions())
        a->setEnabled(!simulating || a == m_toolSelect || a == m_toolProbe);
    m_step->setEnabled(!m_scene->running());
    m_reset->setEnabled(simulating);
    m_stop->setEnabled(simulating);
    const bool analog = m_circuit->analysis().analogMode;
    m_digitalMode->setChecked(!analog);
    m_analogMode->setChecked(analog);
    m_digitalMode->setEnabled(!simulating);
    m_analogMode->setEnabled(!simulating);
    m_analysisSetup->setEnabled(!simulating);
    if (analog) {
        m_step->setEnabled(false);
        m_reset->setEnabled(false);
        m_speed->setEnabled(false);
    } else {
        m_speed->setEnabled(true);
    }
    const bool running = m_scene->running() || m_analogWatcher.isRunning();
    m_run->setText(running ? tr("&Pause") : tr("&Run"));
    m_run->setIcon(running ? themeIcon("media-playback-pause", QStyle::SP_MediaPause)
                           : themeIcon("media-playback-start", QStyle::SP_MediaPlay));
    if (m_analogWatcher.isRunning())
        m_simStatus->setText(tr("SPICE running..."));
    else if (!simulating)
        m_simStatus->setText(analog ? tr("Analog mode") : tr("Editing"));
    else
        m_simStatus->setText(tr("t = %1 ticks").arg(m_scene->simTime()));

    // fault lock: the circuit options the instructor disabled
    auto off = [this](const char* option) { return m_scene->restricted(QString::fromLatin1(option)); };
    if (off("edit"))
        for (QAction* a : {m_cut, m_copy, m_paste, m_duplicate, m_rotate, m_mirror, m_merge, m_straighten, m_designations,
                           m_labelPositions, m_groupItems,
                           m_propDelays})
            a->setEnabled(false);
    if (off("delete")) {
        m_delete->setEnabled(false);
        if (m_toolDelete) m_toolDelete->setEnabled(false);
    }
    if (off("wire"))
        for (QAction* a : {m_toolWire, m_toolBus, m_toolNetLabel}) a->setEnabled(false);
    if (off("properties"))
        m_props->setEnabled(false);
    m_browser->setEnabled(!simulating && !off("library"));
    m_trace->setEnabled(!off("trace"));
    m_showNodeNames->setEnabled(!off("nodeNames"));
}

void MainWindow::applyRestrictions()
{
    const FaultSettings& f = m_circuit->faultSettings();
    m_scene->setRestrictions(f.locked() && !m_faultsUnlocked ? f.disabled : QStringList());
    if (m_scene->restricted(QStringLiteral("trace")) && m_trace->isChecked()) m_trace->setChecked(false);
    if (m_scene->restricted(QStringLiteral("nodeNames")) && m_showNodeNames->isChecked()) m_showNodeNames->setChecked(false);
    updateActions();
}

void MainWindow::deviceFaults(ItemId id)
{
    const Component* c = m_circuit->component(id);
    const SymbolDef* s = c ? LibraryRegistry::instance().find(c->symbolId) : nullptr;
    if (!c || !s || m_scene->simulating())
        return;
    const FaultSettings& settings = m_circuit->faultSettings();
    const QString name = c->designator.isEmpty() ? s->name : c->designator;
    faults::DeviceFaults f = faults::read(*c);
    if (settings.locked() && !m_faultsUnlocked) {
        AccessFaultsDialog access(name, !settings.disables(QStringLiteral("hints")),
                                  !settings.disables(QStringLiteral("replace")), settings.password, this);
        const int r = access.exec();
        if (r == AccessFaultsDialog::Unlocked) {
            m_faultsUnlocked = true;
            applyRestrictions();
        } else if (r == AccessFaultsDialog::ShowHint) {
            QMessageBox::information(this, tr("Hint: %1").arg(name), f.hint.isEmpty() ? tr("No hint for this device.") : f.hint);
            if (f.hint.isEmpty() || f.hinted) return;
            f.hinted = true;
        } else if (r == AccessFaultsDialog::Replace) {
            if (f.replaced) {
                QMessageBox::information(this, tr("Replace device"), tr("%1 was already replaced.").arg(name));
                return;
            }
            if (!settings.disables(QStringLiteral("replaceStatus")))
                QMessageBox::information(this, tr("Replace device"),
                                         f.active() ? tr("%1 was faulty and has been replaced.").arg(name)
                                                    : tr("%1 was not faulty; it has been replaced anyway.").arg(name));
            f.replaced = true;
        } else {
            return;
        }
        if (r != AccessFaultsDialog::Unlocked) {
            Component after = *c;
            faults::write(after, f);
            Selection b, a;
            b.components << *c;
            a.components << after;
            m_undo->push(new ModifyItemsCommand(m_circuit, b, a,
                                                r == AccessFaultsDialog::Replace ? tr("Replace %1").arg(name) : tr("Hint")));
            return;
        }
    }
    DeviceFaultsDialog dlg(*c, *s, this);
    if (dlg.exec() != QDialog::Accepted || dlg.result() == f)
        return;
    Component after = *c;
    faults::write(after, dlg.result());
    Selection b, a;
    b.components << *c;
    a.components << after;
    m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Faults of %1").arg(name)));
}

void MainWindow::circuitFaults()
{
    const FaultSettings& settings = m_circuit->faultSettings();
    if (settings.locked() && !m_faultsUnlocked) {
        bool ok = false;
        const QString pw = QInputDialog::getText(this, tr("Circuit fault data"), tr("Fault lock password:"),
                                                 QLineEdit::Password, {}, &ok);
        if (!ok)
            return;
        if (faults::hashPassword(pw) != settings.password) {
            QMessageBox::warning(this, tr("Circuit fault data"), tr("Wrong password."));
            return;
        }
        m_faultsUnlocked = true;
        applyRestrictions();
    }
    CircuitFaultsDialog::Counts counts;
    for (const Component& c : m_circuit->components()) {
        const faults::DeviceFaults f = faults::read(c);
        counts.faulty += f.hasData();
        counts.replaced += f.replaced;
        counts.hinted += f.hinted;
    }
    CircuitFaultsDialog dlg(settings, counts, this);
    const int r = dlg.exec();
    if (r >= CircuitFaultsDialog::SelectFaulty) {
        m_scene->clearSelection();
        for (QGraphicsItem* it : m_scene->items()) {
            auto* item = qgraphicsitem_cast<ComponentItem*>(it);
            if (!item) continue;
            const faults::DeviceFaults f = faults::read(item->component());
            item->setSelected(r == CircuitFaultsDialog::SelectFaulty ? f.hasData()
                              : r == CircuitFaultsDialog::SelectReplaced ? f.replaced : f.hinted);
        }
        return;
    }
    if (r != QDialog::Accepted)
        return;
    m_undo->beginMacro(tr("Circuit fault data"));
    if (dlg.clearMarks()) {
        Selection b, a;
        for (const Component& c : m_circuit->components()) {
            faults::DeviceFaults f = faults::read(c);
            if (!f.replaced && !f.hinted) continue;
            f.replaced = f.hinted = false;
            Component after = c;
            faults::write(after, f);
            b.components << c;
            a.components << after;
        }
        if (!a.isEmpty())
            m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Clear hints/replacements")));
    }
    if (dlg.result() != settings) {
        // whoever set a new password knows it
        m_faultsUnlocked = true;
        m_undo->push(new SetFaultSettingsCommand(m_circuit, dlg.result(), tr("Circuit fault data")));
    }
    m_undo->endMacro();
}

void MainWindow::updateTitle()
{
    const QString name = !m_macroName.isEmpty() ? tr("Macro %1").arg(m_macroName)
                       : m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1[*] — CirMaker Qt").arg(name));
    setWindowModified(!m_undo->isClean());
}

bool MainWindow::maybeSave()
{
    m_scene->stopSimulation();
    if (m_undo->isClean())
        return true;
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
                                             tr("The circuit has been modified. Save the changes?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::newCircuit()
{
    if (!maybeSave())
        return;
    m_circuit->clear();
    m_undo->clear();
    m_path.clear();
    m_macroName.clear();
    m_macroLock.clear();
    m_faultsUnlocked = false;
    applyRestrictions();
    updateTitle();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open circuit"), settings.value(QStringLiteral("dirs/open")).toString(),
        tr("Circuits (*.cmq *.ckt *.CKT *.cir *.sp *.spi);;CirMaker Qt (*.cmq);;CircuitMaker 2000 (*.ckt *.CKT);;"
           "SPICE netlist (*.cir *.sp *.spi *.net);;All files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString& path, bool interactive)
{
    auto fail = [&](const QString& title, const QString& text) {
        if (interactive)
            QMessageBox::critical(this, title, text);
        else
            qWarning("%s", qPrintable(text));
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return fail(tr("Open"), tr("Cannot open %1:\n%2").arg(path, f.errorString()));
    }
    const QByteArray data = f.readAll();
    QSettings().setValue(QStringLiteral("dirs/open"), QFileInfo(path).absolutePath());
    QString error;

    if (data.startsWith("CircuitMaker")) {
        Circuit imported;
        CktImportReport report;
        if (!CktImporter::importData(data, imported, LibraryRegistry::instance(), &report, &error)) {
            return fail(tr("Import"), tr("Cannot import %1:\n%2").arg(QFileInfo(path).fileName(), error));
        }
        m_circuit->clear();
        m_macroName.clear();
        m_macroLock.clear();
        NativeFormat::deserialize(NativeFormat::serialize(imported), *m_circuit, nullptr);
        m_undo->clear();
        m_undo->resetClean();
        // imported circuits are saved as .cmq, never over the original
        m_path.clear();
        updateTitle();
        addRecent(path);
        setWindowModified(true);
        if (interactive && (!report.unmapped.isEmpty() || !report.warnings.isEmpty())) {
            QStringList lines;
            for (auto it = report.unmapped.cbegin(); it != report.unmapped.cend(); ++it)
                lines << tr("%1 (%n×)", nullptr, it.value()).arg(it.key());
            QMessageBox box(QMessageBox::Information, tr("Import report"),
                            tr("Imported %1 (CircuitMaker %2): %3 devices, %4 wires.")
                                .arg(QFileInfo(path).fileName())
                                .arg(report.version)
                                .arg(report.components)
                                .arg(report.wires),
                            QMessageBox::Ok, this);
            if (!lines.isEmpty())
                box.setInformativeText(tr("Devices without an equivalent in the library were replaced by placeholders:"));
            box.setDetailedText((lines + report.warnings).join(u'\n'));
            box.exec();
        }
        statusBar()->showMessage(tr("Imported %1").arg(path), 5000);
    } else if (!data.trimmed().startsWith('{')) {
        // anything that is not a .cmq document is read as a SPICE netlist
        Circuit imported;
        SpiceImportReport report;
        if (!SpiceImporter::importText(QString::fromLocal8Bit(data), imported, LibraryRegistry::instance(), &report, &error))
            return fail(tr("Import"), tr("Cannot import %1:\n%2").arg(QFileInfo(path).fileName(), error));
        m_circuit->clear();
        m_macroName.clear();
        m_macroLock.clear();
        NativeFormat::deserialize(NativeFormat::serialize(imported), *m_circuit, nullptr);
        m_undo->clear();
        m_undo->resetClean();
        m_path.clear();
        updateTitle();
        addRecent(path);
        setWindowModified(true);
        if (interactive && !report.skipped.isEmpty()) {
            QMessageBox box(QMessageBox::Information, tr("Import report"),
                            tr("Imported %1: %2 parts.").arg(QFileInfo(path).fileName()).arg(report.components),
                            QMessageBox::Ok, this);
            box.setInformativeText(tr("Lines without an equivalent here were left out:"));
            box.setDetailedText(report.skipped.join(u'\n'));
            box.exec();
        }
        statusBar()->showMessage(tr("Imported %1").arg(path), 5000);
    } else {
        Circuit loaded;
        if (!NativeFormat::deserialize(data, loaded, &error)) {
            return fail(tr("Open"), tr("Cannot open %1:\n%2").arg(QFileInfo(path).fileName(), error));
        }
        m_circuit->clear();
        m_macroName.clear();
        m_macroLock.clear();
        NativeFormat::deserialize(data, *m_circuit, nullptr);
        m_undo->clear();
        m_path = path;
        updateTitle();
        addRecent(path);
    }
    m_faultsUnlocked = false;
    applyRestrictions();
    m_view->zoomToFit();
    return true;
}

void MainWindow::importCkt()
{
    if (!maybeSave())
        return;
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, tr("Import CircuitMaker 2000 circuit"),
                                                      settings.value(QStringLiteral("dirs/open")).toString(),
                                                      tr("CircuitMaker 2000 (*.ckt *.CKT);;All files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    m_scene->stopSimulation();
    QString error;
    if (!NativeFormat::save(m_path, *m_circuit, &error)) {
        QMessageBox::critical(this, tr("Save"), tr("Cannot save %1:\n%2").arg(m_path, error));
        return false;
    }
    m_undo->setClean();
    updateTitle();
    addRecent(m_path);
    statusBar()->showMessage(tr("Saved %1").arg(m_path), 4000);
    return true;
}

bool MainWindow::saveAs()
{
    QSettings settings;
    QString path = QFileDialog::getSaveFileName(this, tr("Save circuit"), settings.value(QStringLiteral("dirs/open")).toString(),
                                                tr("CirMaker Qt (*.cmq)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".cmq"), Qt::CaseInsensitive))
        path += QStringLiteral(".cmq");
    m_path = path;
    return save();
}

// ---------------------------------------------------------------- macros

const SymbolDef* MainWindow::selectedMacro() const
{
    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1)
        return nullptr;
    const auto* item = dynamic_cast<const ComponentItem*>(sel.first());
    const SymbolDef* s = item ? LibraryRegistry::instance().find(item->component().symbolId) : nullptr;
    return s && macros::isMacro(*s) ? s : nullptr;
}

void MainWindow::newMacro()
{
    m_scene->stopSimulation();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Macro"), tr("Macro name:"), QLineEdit::Normal,
                                               m_macroName.isEmpty() ? tr("Macro1") : m_macroName, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (const SymbolDef* existing = LibraryRegistry::instance().find(macros::idFor(name));
        existing && !macros::lockOf(*existing).isEmpty()) {
        QMessageBox::warning(this, tr("New Macro"), tr("%1 is locked; unlock it with Macro Lock before replacing it.").arg(name));
        return;
    }
    m_macroLock.clear();
    if (LibraryRegistry::instance().find(macros::idFor(name))
        && QMessageBox::question(this, tr("New Macro"), tr("A macro named %1 already exists. Replace it?").arg(name))
               != QMessageBox::Yes)
        return;
    if (!m_circuit->isEmpty()
        && QMessageBox::question(this, tr("New Macro"), tr("Include the circuit in the work area inside the macro?"))
               == QMessageBox::Yes) {
        const QString previous = m_macroName;
        m_macroName = name;
        if (!saveMacro())
            m_macroName = previous;
        updateTitle();
        return;
    }
    if (!maybeSave())
        return;
    m_circuit->clear();
    m_undo->clear();
    m_path.clear();
    m_macroName = name;
    updateTitle();
    statusBar()->showMessage(tr("Draw the macro circuit with Input and Output ports, then choose Macros > Save Macro"), 10000);
}

bool MainWindow::saveMacro()
{
    if (m_macroName.isEmpty()) {
        newMacro();
        return !m_macroName.isEmpty();
    }
    m_scene->stopSimulation();
    QString error;
    const QString id = macros::define(m_macroName, *m_circuit, &error, m_macroLock);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Save Macro"),
                             tr("Cannot save macro %1: %2.\n\nPlace Input and Output devices "
                                "(General category) where the macro's pins connect.").arg(m_macroName, error));
        return false;
    }
    m_scene->refreshSymbols();
    m_browser->reload();
    if (m_path.isEmpty())
        m_undo->setClean();     // the macro library holds the work
    updateTitle();
    statusBar()->showMessage(tr("Saved macro %1 (Macros category of the device library)").arg(m_macroName), 5000);
    return true;
}

// CircuitMaker's right-click menu: the item's own editors, then the edit commands.
void MainWindow::showContextMenu(QGraphicsItem* item, QPoint screenPos)
{
    updateActions();
    QMenu menu(this);
    auto* comp = qgraphicsitem_cast<ComponentItem*>(item);
    const bool memory = comp && SchematicScene::isMemory(*comp->symbol());
    if (m_scene->simulating()) {
        if (!memory)
            return;
        menu.addAction(tr("Edit PROM/&RAM..."), this, [this, id = comp->id()] { m_scene->editMemory(id); });
        menu.exec(screenPos);
        return;
    }
    if (comp) {
        const QString kind = comp->symbol()->model.value(u"kind").toString();
        if (memory)
            menu.addAction(tr("Edit PROM/&RAM..."), this, [this, id = comp->id()] { m_scene->editMemory(id); });
        const QString edit = kind == u"dataseq" ? tr("Edit &Data Sequencer...")
                           : kind == u"pulser"  ? tr("Edit P&ulser...")
                           : kind == u"siggen"  ? tr("Edit &Signal Generator...")
                           : comp->symbol()->netLabel ? tr("Edit &Terminal Name...")
                           : tr("Device &Properties...");
        QAction* props = menu.addAction(edit, this, [this, item] { m_scene->editItem(item); });
        menu.setDefaultAction(memory ? menu.actions().first() : props);
        if (!comp->symbol()->netLabel)
            menu.addAction(tr("Device &Faults..."), this, [this, id = comp->id()] { deviceFaults(id); });
        if (selectedMacro())
            menu.addAction(m_expandMacro);
        menu.addAction(m_topicHelp);
        menu.addSeparator();
    } else if (item && item->type() == TextNoteItem::Type) {
        menu.setDefaultAction(menu.addAction(tr("Edit &Text..."), this, [this, item] { m_scene->editItem(item); }));
        menu.addSeparator();
    } else if (item && item->type() == TitleBlockItem::Type) {
        menu.setDefaultAction(menu.addAction(tr("Edit Title &Block..."), this, [this, item] { m_scene->editItem(item); }));
        menu.addSeparator();
    }
    if (m_scene->hasSelection()) {
        menu.addActions({m_cut, m_copy, m_delete, m_duplicate});
        if (!m_scene->selection().components.isEmpty()) {
            menu.addSeparator();
            menu.addActions({m_rotate, m_mirror});
        }
    } else {
        menu.addActions({m_paste, m_selectAll});
    }
    menu.exec(screenPos);
}

void MainWindow::editMacroSymbol()
{
    const SymbolDef* s = selectedMacro();
    if (!s) {
        statusBar()->showMessage(tr("Select one macro device"), 4000);
        return;
    }
    if (const QString lock = macros::lockOf(*s); !lock.isEmpty()) {
        bool ok = false;
        const QString pw = QInputDialog::getText(this, tr("Edit Macro Symbol"), tr("%1 is locked. Password:").arg(s->name),
                                                 QLineEdit::Password, {}, &ok);
        if (!ok) return;
        if (faults::hashPassword(pw) != lock) {
            QMessageBox::warning(this, tr("Edit Macro Symbol"), tr("Wrong password."));
            return;
        }
    }
    MacroSymbolDialog dlg(*s, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!macros::setSymbolLayout(s->id, dlg.layout(), &error)) {
        QMessageBox::warning(this, tr("Edit Macro Symbol"), error);
        return;
    }
    m_scene->refreshSymbols();
    m_browser->reload();
    setWindowModified(true);
}

// CircuitMaker's Macro Lock: set a password, or remove it with the password
void MainWindow::lockMacro()
{
    const SymbolDef* s = selectedMacro();
    if (!s) {
        statusBar()->showMessage(tr("Select one macro device to lock"), 4000);
        return;
    }
    const QString id = s->id, name = s->name, lock = macros::lockOf(*s);
    bool ok = false;
    QString digest;
    if (lock.isEmpty()) {
        const QString pw = QInputDialog::getText(this, tr("Macro Lock"), tr("Password to lock %1:").arg(name),
                                                 QLineEdit::Password, {}, &ok);
        if (!ok || pw.isEmpty()) return;
        const QString again = QInputDialog::getText(this, tr("Macro Lock"), tr("Type it again:"), QLineEdit::Password, {}, &ok);
        if (!ok) return;
        if (again != pw) {
            QMessageBox::warning(this, tr("Macro Lock"), tr("The passwords differ."));
            return;
        }
        digest = faults::hashPassword(pw);
    } else {
        const QString pw = QInputDialog::getText(this, tr("Macro Lock"), tr("%1 is locked. Password to unlock it:").arg(name),
                                                 QLineEdit::Password, {}, &ok);
        if (!ok) return;
        if (faults::hashPassword(pw) != lock) {
            QMessageBox::warning(this, tr("Macro Lock"), tr("Wrong password."));
            return;
        }
    }
    QString error;
    if (!macros::setLock(id, digest, &error)) {
        QMessageBox::warning(this, tr("Macro Lock"), error);
        return;
    }
    m_scene->refreshSymbols();
    setWindowModified(true);        // the document embeds the definition
    statusBar()->showMessage(digest.isEmpty() ? tr("%1 unlocked").arg(name) : tr("%1 locked").arg(name), 5000);
}

void MainWindow::expandMacro()
{
    const SymbolDef* s = selectedMacro();
    if (!s) {
        statusBar()->showMessage(tr("Select one macro device to expand"), 4000);
        return;
    }
    const QString name = s->name;
    const QString lock = macros::lockOf(*s);
    if (!lock.isEmpty()) {
        bool ok = false;
        const QString pw = QInputDialog::getText(this, tr("Expand Macro"), tr("%1 is locked. Password:").arg(name),
                                                 QLineEdit::Password, {}, &ok);
        if (!ok) return;
        if (faults::hashPassword(pw) != lock) {
            QMessageBox::warning(this, tr("Expand Macro"), tr("Wrong password."));
            return;
        }
    }
    Circuit inner;
    QString error;
    if (!macros::innerCircuit(*s, inner, &error)) {
        QMessageBox::warning(this, tr("Expand Macro"), error);
        return;
    }
    if (!maybeSave())
        return;
    m_circuit->clear();
    NativeFormat::deserialize(NativeFormat::serialize(inner), *m_circuit, nullptr);
    m_undo->clear();
    m_path.clear();
    m_macroName = name;
    m_macroLock = lock;
    updateTitle();
    m_view->zoomToFit();
    statusBar()->showMessage(tr("Editing macro %1; choose Macros > Save Macro when done").arg(name), 8000);
}

void MainWindow::editMacro()
{
    if (m_macroName.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Edit Macro"),
                                               tr("Macro name (a new name saves a new macro):"),
                                               QLineEdit::Normal, m_macroName, &ok).trimmed();
    if (!ok || name.isEmpty() || name == m_macroName)
        return;
    if (const SymbolDef* existing = LibraryRegistry::instance().find(macros::idFor(name));
        existing && !macros::lockOf(*existing).isEmpty()) {
        QMessageBox::warning(this, tr("Edit Macro"), tr("%1 is locked; unlock it with Macro Lock before replacing it.").arg(name));
        return;
    }
    const QString previous = m_macroName;
    m_macroName = name;
    if (!saveMacro())
        m_macroName = previous;
    updateTitle();
}

void MainWindow::deleteMacro()
{
    QStringList names;
    for (const SymbolDef* s : LibraryRegistry::instance().all())
        if (macros::isMacro(*s))
            names << s->name;
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Delete Macro"), tr("The library has no macros."));
        return;
    }
    names.sort(Qt::CaseInsensitive);
    bool ok = false;
    const QString name = QInputDialog::getItem(this, tr("Delete Macro"), tr("Macro:"), names, 0, false, &ok);
    if (!ok)
        return;
    const QString id = macros::idFor(name);
    for (const Component& c : m_circuit->components())
        if (c.symbolId == id) {
            QMessageBox::warning(this, tr("Delete Macro"), tr("%1 is used in this circuit (%2).").arg(name, c.designator));
            return;
        }
    if (const SymbolDef* s = LibraryRegistry::instance().find(id); s && !macros::lockOf(*s).isEmpty()) {
        QMessageBox::warning(this, tr("Delete Macro"), tr("%1 is locked; unlock it with Macro Lock first.").arg(name));
        return;
    }
    macros::forget(id);
    m_browser->reload();
    statusBar()->showMessage(tr("Deleted macro %1").arg(name), 4000);
}

void MainWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export schematic"), {},
                                                      tr("PNG image (*.png);;SVG drawing (*.svg);;PDF document (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!exportTo(path))
        QMessageBox::critical(this, tr("Export"), tr("Cannot write %1").arg(path));
    else
        statusBar()->showMessage(tr("Exported %1").arg(path), 4000);
}

QString MainWindow::writeTextFile(const QString& title, const QString& suffix, const QString& filter, const QString& text)
{
    const QString base = m_path.isEmpty() ? tr("circuit") : QFileInfo(m_path).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, title, base + u'.' + suffix, filter);
    if (path.isEmpty())
        return {};
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || (f.write(text.toUtf8()), !f.commit())) {
        QMessageBox::critical(this, title, tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return {};
    }
    statusBar()->showMessage(tr("Written %1").arg(path), 5000);
    return path;
}

// CircuitMaker draws devices from its own library, so writing its format
// needs an installation (normally under Wine); its folder is remembered.
void MainWindow::exportCkt()
{
    const QString title = tr("Export to CircuitMaker 2000");
    QSettings settings;
    QString libPath = CmDeviceLib::locate(settings.value(QStringLiteral("cm/folder")).toString());
    if (libPath.isEmpty()) {
        QMessageBox::information(this, title,
                                 tr("Writing .ckt files needs CircuitMaker 2000's Device.lib, which is not distributed "
                                    "with this program. Choose the folder where CircuitMaker 2000 is installed."));
        const QString folder = QFileDialog::getExistingDirectory(this, title);
        if (folder.isEmpty()) return;
        libPath = CmDeviceLib::locate(folder);
        if (libPath.isEmpty()) {
            QMessageBox::warning(this, title, tr("There is no Device.lib in %1.").arg(folder));
            return;
        }
        settings.setValue(QStringLiteral("cm/folder"), QFileInfo(libPath).absolutePath());
    }
    CmDeviceLib lib;
    QString error;
    if (!lib.load(libPath, &error)) {
        QMessageBox::warning(this, title, tr("Cannot read %1:\n%2").arg(libPath, error));
        return;
    }
    const QString base = m_path.isEmpty() ? tr("circuit") : QFileInfo(m_path).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, title, base + QStringLiteral(".ckt"),
                                                      tr("CircuitMaker 2000 schematic (*.ckt)"));
    if (path.isEmpty()) return;
    CktExportReport report;
    if (!CktExporter::save(path, *m_circuit, LibraryRegistry::instance(), lib, &report, &error)) {
        QMessageBox::critical(this, title, tr("Cannot write %1:\n%2").arg(path, error));
        return;
    }
    if (!report.skipped.isEmpty())
        QMessageBox::warning(this, title,
                             tr("Written %1, without these items CircuitMaker 2000 has no equivalent for:\n%2")
                                 .arg(path, report.skipped.join(QStringLiteral(", "))));
    else
        statusBar()->showMessage(tr("Written %1").arg(path), 5000);
}

void MainWindow::exportBom()
{
    const QVector<BomLine> lines = Bom::build(*m_circuit, LibraryRegistry::instance());
    if (lines.isEmpty()) {
        QMessageBox::information(this, tr("Bill of Materials"), tr("The circuit has no parts."));
        return;
    }
    const QString base = m_path.isEmpty() ? tr("circuit") : QFileInfo(m_path).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, tr("Bill of Materials"), base + QStringLiteral(".csv"),
                                                      tr("CSV file (*.csv)"));
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Bill of Materials"), tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    f.write(Bom::toCsv(lines).toUtf8());
    if (!f.commit()) {
        QMessageBox::critical(this, tr("Bill of Materials"), tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Bill of materials written to %1 (%n line(s))", nullptr, int(lines.size())).arg(path), 5000);
}

void MainWindow::findAndSelect()
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Find and Select"),
                                               tr("Designator, label or device name contains:"), QLineEdit::Normal,
                                               m_lastFind, &ok);
    if (!ok)
        return;
    m_lastFind = text;
    const int count = m_scene->findAndSelect(text);
    if (count == 0) {
        statusBar()->showMessage(tr("Nothing matches \"%1\"").arg(text), 5000);
        return;
    }
    QRectF r;
    for (QGraphicsItem* it : m_scene->selectedItems())
        r = r.united(it->sceneBoundingRect());
    if (!m_view->mapToScene(m_view->viewport()->rect()).boundingRect().contains(r))
        m_view->fitInView(r.adjusted(-200, -150, 200, 150), Qt::KeepAspectRatio);
    statusBar()->showMessage(tr("%n item(s) selected", nullptr, count), 5000);
}

bool MainWindow::exportTo(const QString& path)
{
    const QRectF src = m_scene->contentsRect();
    m_scene->clearSelection();
    m_scene->setExporting(true);
    bool ok = true;
    if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
        QSvgGenerator gen;
        gen.setFileName(path);
        gen.setSize(src.size().toSize());
        gen.setViewBox(QRectF(QPointF(0, 0), src.size()));
        gen.setTitle(windowTitle());
        QPainter p(&gen);
        m_scene->render(&p, QRectF(QPointF(0, 0), src.size()), src);
    } else if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        QPdfWriter pdf(path);
        pdf.setPageOrientation(src.width() > src.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        QPainter p(&pdf);
        m_scene->render(&p, {}, src);
    } else {
        ok = renderImage().save(path);
    }
    m_scene->setExporting(false);
    return ok;
}

QImage MainWindow::renderImage() const
{
    const QRectF src = m_scene->contentsRect();
    const double scale = std::min(2.0, 6000.0 / std::max(src.width(), src.height()));
    QImage img((src.size() * scale).toSize(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    for (QGraphicsItem* it : selected) it->setSelected(false);
    m_scene->setExporting(true);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&p, {}, src);
    p.end();
    m_scene->setExporting(false);
    for (QGraphicsItem* it : selected) it->setSelected(true);
    return img;
}

void MainWindow::copyImage()
{
    QApplication::clipboard()->setImage(renderImage());
    statusBar()->showMessage(tr("Schematic copied to the clipboard"), 4000);
}

void MainWindow::merge()
{
    if (m_scene->simulating())
        return;
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Merge circuit"), settings.value(QStringLiteral("dirs/open")).toString(),
        tr("Circuits (*.cmq *.ckt *.CKT *.cir *.sp *.spi);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Merge"), tr("Cannot open %1:\n%2").arg(path, f.errorString()));
        return;
    }
    const QByteArray data = f.readAll();
    Circuit other;
    QString error;
    const bool ok = data.startsWith("CircuitMaker")
                        ? CktImporter::importData(data, other, LibraryRegistry::instance(), nullptr, &error)
                        : !data.trimmed().startsWith('{')
                              ? SpiceImporter::importText(QString::fromLocal8Bit(data), other, LibraryRegistry::instance(), nullptr, &error)
                              : NativeFormat::deserialize(data, other, &error);
    if (!ok) {
        QMessageBox::critical(this, tr("Merge"), tr("Cannot open %1:\n%2").arg(QFileInfo(path).fileName(), error));
        return;
    }
    m_scene->mergeCircuit(other);
    statusBar()->showMessage(tr("Merged %1").arg(path), 4000);
}

void MainWindow::showLibrarySheet()
{
    m_circuit->clear();
    m_undo->clear();
    const QList<const SymbolDef*> symbols = LibraryRegistry::instance().all();
    QString category;
    double x = 0, y = 0, rowHeight = 0;
    constexpr double kWidth = 2400;
    for (const SymbolDef* s : symbols) {
        if (s->category != category) {
            category = s->category;
            x = 0;
            y += rowHeight + (rowHeight > 0 ? 80 : 0);
            rowHeight = 0;
            m_circuit->addText({0, {0, y}, category, 18});
            y += 60;
        }
        const QRectF b = s->bounds;
        if (x + b.width() + 120 > kWidth) {
            x = 0;
            y += rowHeight + 60;
            rowHeight = 0;
        }
        Component c;
        c.symbolId = s->id;
        c.designator = s->name;
        c.label = s->defaultLabel;
        c.pos = SchematicScene::snap(QPointF(x - b.left(), y - b.top()));
        m_circuit->addComponent(c);
        x += b.width() + 160;
        rowHeight = std::max(rowHeight, b.height() + 20);
    }
    m_undo->resetClean();
    m_path.clear();
    updateTitle();
    m_view->zoomToFit();
}

void MainWindow::print()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QRectF src = m_scene->contentsRect();
    m_scene->clearSelection();
    m_scene->setExporting(true);
    QPainter p(&printer);
    m_scene->render(&p, {}, src);
    p.end();
    m_scene->setExporting(false);
}

void MainWindow::editPreferences()
{
    PreferencesDialog dlg(Preferences::load(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    dlg.result().save();
    applyPreferences();
}

void MainWindow::applyPreferences()
{
    const Preferences p = Preferences::load();
    m_scene->setGridLines(p.gridStyle == Preferences::GridStyle::Lines);
    m_grid->setChecked(p.gridVisible);              // drives the scene through toggled()
    m_scene->setGridVisible(p.gridVisible);
    m_view->setDefaultZoom(p.zoomPercent / 100.0);
    m_speed->setValue(p.ticksPerFrame);
    m_scene->setStepTicks(p.stepTicks);
    m_scene->applyTheme();
}

void MainWindow::showDigitalWaveforms()
{
    const auto* sim = m_scene->simulator();
    if (!sim || m_scene->waveformLanes().isEmpty())
        return;
    QVector<WaveformView::Lane> lanes;
    for (const SchematicScene::Lane& l : m_scene->waveformLanes()) {
        WaveformView::Lane lane{l.name, {}, l.component ? m_scene->breakCondition(l.component) : -1};
        for (const auto& [t, v] : sim->history().value(l.net))
            lane.changes.append(std::pair<quint64, int>(t, int(v)));
        lanes << lane;
    }
    m_waves->setDigital(lanes, sim->time());
    if (!m_wavesDock->isVisible())
        m_wavesDock->show();
}

void MainWindow::setAnalogMode(bool analog)
{
    if (m_scene->simulating() || m_circuit->analysis().analogMode == analog)
        return updateActions();
    AnalysisSettings a = m_circuit->analysis();
    a.analogMode = analog;
    m_undo->push(new SetAnalysisCommand(m_circuit, a, analog ? tr("Analog mode") : tr("Digital mode")));
    if (analog) {
        QString why;
        if (!spice::NgspiceEngine::instance().available(&why))
            QMessageBox::warning(this, tr("Analog mode"), tr("The SPICE engine is not available:\n%1").arg(why));
    }
}

void MainWindow::editAnalysis()
{
    QStringList sources;
    for (const Component& c : m_circuit->components()) {
        const SymbolDef* s = LibraryRegistry::instance().find(c.symbolId);
        const QString line = s ? s->spice.value(u"line").toString() : QString();
        if (line.startsWith(u'V') || line.startsWith(u'I') || (s && s->model.value(u"kind").toString() == u"siggen"))
            sources << c.designator;
    }
    sources.sort();
    QStringList devices, nets;
    for (const Component& c : m_circuit->components()) {
        const SymbolDef* s = LibraryRegistry::instance().find(c.symbolId);
        if (s && !s->netLabel && !c.designator.isEmpty() && !s->spice.isEmpty()) devices << c.designator;
    }
    devices.sort();
    m_scene->refreshNetlist();
    const Netlist& nl = m_scene->netlist();
    for (int n = 0; n < nl.nets.size(); ++n)
        if (nl.nets[n].name != QStringLiteral("N%1").arg(n + 1) && !nets.contains(nl.nets[n].name)) nets << nl.nets[n].name;
    nets.sort();
    AnalysisDialog dlg(m_circuit->analysis(), sources, nets, devices, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_undo->push(new SetAnalysisCommand(m_circuit, dlg.result(), tr("Analysis settings")));
}

void MainWindow::runAnalog()
{
    if (m_analogWatcher.isRunning())
        return;
    QString why;
    if (!spice::NgspiceEngine::instance().available(&why)) {
        QMessageBox::critical(this, tr("Analog simulation"), tr("The SPICE engine is not available:\n%1").arg(why));
        return;
    }
    m_scene->refreshNetlist();
    const SchematicScene::AnalogInput input = m_scene->analogInput();
    QString error;
    m_runs = spice::buildRuns(*input.circuit, LibraryRegistry::instance(), input.netlist, &error);
    QStringList warnings = error.isEmpty() ? QStringList() : QStringList{error};
    bool ok = !m_runs.isEmpty();
    for (const spice::Run& run : std::as_const(m_runs)) {
        ok &= run.deck.ok;
        for (const QString& w : run.deck.warnings)
            if (!warnings.contains(w)) warnings << w;
    }
    if (!ok) {
        QMessageBox::warning(this, tr("Analog simulation"), warnings.join(u'\n'));
        return;
    }
    if (!warnings.isEmpty())
        statusBar()->showMessage(warnings.join(QStringLiteral("; ")), 10000);
    QStringList texts;
    for (const spice::Run& run : std::as_const(m_runs)) texts << run.deck.text;
    m_analogWatcher.setFuture(QtConcurrent::run([texts] {
        QVector<spice::Result> results;
        for (const QString& text : texts) {
            results << spice::NgspiceEngine::instance().run(text);
            if (!results.last().ok) break;
        }
        return results;
    }));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    updateActions();
}

void MainWindow::analogFinished()
{
    QApplication::restoreOverrideCursor();
    const QVector<spice::Result> results = m_analogWatcher.result();
    updateActions();
    if (results.isEmpty() || !results.last().ok) {
        const int failed = int(results.size()) - 1;
        const spice::Result r = results.value(failed);
        QMessageBox box(QMessageBox::Critical, tr("Analog simulation"), tr("SPICE could not simulate the circuit."),
                        QMessageBox::Ok, this);
        box.setInformativeText(m_runs.value(failed).label.isEmpty() ? r.error
                                                                    : m_runs.value(failed).label + u": " + r.error);
        box.setDetailedText(tr("Netlist:\n%1\nSPICE output:\n%2").arg(m_runs.value(failed).deck.text, r.log));
        box.exec();
        return;
    }
    showAnalogResults(results);
}

void MainWindow::showAnalogResults(const QVector<spice::Result>& results)
{
    const spice::Result& r = results.first();
    const spice::Deck& deck = m_runs.first().deck;
    const Netlist& nl = m_scene->netlist();
    const AnalysisSettings& analysis = m_circuit->analysis();
    const bool sweep = results.size() > 1;
    auto voltage = [&](const spice::Result& res, const QString& node) -> double {
        if (node == u"0") return 0;
        return res.last(node).value_or(qQNaN());
    };
    auto netVoltage = [&](ItemId id, int pin) { return voltage(r, deck.nodeOfNet.value(nl.netOf(id, pin), QStringLiteral("0"))); };

    // transfer function: three numbers per run
    if (r.plotType == u"tf") {
        QStringList lines;
        for (int i = 0; i < results.size(); ++i) {
            const spice::Result& res = results[i];
            QString tf, zin, zout;
            for (auto it = res.vectors.cbegin(); it != res.vectors.cend(); ++it) {
                const double v = it->real.value(0, it->isComplex() ? it->complex[0].real() : qQNaN());
                if (it.key().contains(u"transfer")) tf = QString::number(v, 'g', 6);
                else if (it.key().contains(u"input_impedance")) zin = WaveformView::siFormat(v, QStringLiteral("Ω"), 5);
                else if (it.key().contains(u"output_impedance")) zout = WaveformView::siFormat(v, QStringLiteral("Ω"), 5);
            }
            if (sweep) lines << QStringLiteral("<b>%1</b>").arg(m_runs[i].label);
            lines << tr("Transfer function V(%1)/%2: %3").arg(analysis.output, analysis.inputSource, tf)
                  << tr("Input impedance: %1").arg(zin) << tr("Output impedance: %1").arg(zout);
        }
        QMessageBox::information(this, tr("Transfer function"), lines.join(QStringLiteral("<br>")));
        return;
    }

    // meters and displays read the operating point / final values
    SchematicScene::AnalogOverlay overlay;
    for (auto it = deck.meters.cbegin(); it != deck.meters.cend(); ++it) {
        const spice::Meter& m = it.value();
        const double v = m.amps ? r.last(m.current).value_or(qQNaN()) : voltage(r, m.plus) - voltage(r, m.minus);
        overlay.meterText.insert(it.key(), WaveformView::siFormat(v, m.amps ? QStringLiteral("A") : QStringLiteral("V"), 4));
    }
    for (const Component& c : m_circuit->components()) {
        const SymbolDef* s = LibraryRegistry::instance().find(c.symbolId);
        if (!s || s->display.isEmpty()) continue;
        const QString& d = s->display;
        if (d == u"led") {
            overlay.displayState.insert(c.id, netVoltage(c.id, s->pinIndex(QStringLiteral("A")))
                                                      - netVoltage(c.id, s->pinIndex(QStringLiteral("K"))) > 1.5);
        } else if (d == u"bulb") {
            overlay.displayState.insert(c.id, std::abs(netVoltage(c.id, 0) - netVoltage(c.id, 1)) > 0.5);
        } else if (d == u"stepper") {
            // the rotor follows the energized coils through the whole run
            auto series = [&](const QString& pin) -> const QVector<double>* {
                const QString node = deck.nodeOfNet.value(nl.netOf(c.id, s->pinIndex(pin)), QStringLiteral("0"));
                const spice::Vector* v = node == u"0" ? nullptr : r.find(node);
                return v ? &v->real : nullptr;
            };
            auto at = [](const QVector<double>* v, int k) { return v && !v->isEmpty() ? v->value(k, v->last()) : 0.0; };
            const QVector<double>* coil[4] = {series(QStringLiteral("1")), series(QStringLiteral("2")),
                                              series(QStringLiteral("3")), series(QStringLiteral("4"))};
            const QVector<double>* com[2] = {series(QStringLiteral("COMA")), series(QStringLiteral("COMB"))};
            int samples = 1;
            for (const QVector<double>* v : coil) if (v) samples = std::max(samples, int(v->size()));
            int half = 0;
            for (int k = 0; k < samples; ++k) {
                bool on[4];
                for (int n = 0; n < 4; ++n) on[n] = std::abs(at(coil[n], k) - at(com[n / 2], k)) > 1.0;
                half = sim::stepperAdvance(half, on);
            }
            overlay.displayState.insert(c.id, half);
        } else if (d == u"lamp" || d == u"hex" || d == u"ascii" || d == u"stoplight") {
            int value = 0;
            for (int i = 0; i < s->pins.size(); ++i) {
                const QString pn = s->pins[i].name;
                if (pn.startsWith(u'D') && netVoltage(c.id, i) > 2.5)
                    value |= 1 << pn.mid(1).toInt();
            }
            overlay.displayState.insert(c.id, value);
        }
    }

    // nets to plot: probes, else named nets, else the first few nodes
    QVector<std::pair<QString, QString>> plotted;      // (label, node)
    const QVector<int> probeNets = m_scene->probeNets();
    for (int i = 0; i < probeNets.size(); ++i)
        if (probeNets[i] >= 0)
            plotted << std::pair{QStringLiteral("%1: %2").arg(i + 1).arg(nl.nets[probeNets[i]].name), deck.nodeOfNet.value(probeNets[i])};
    if (plotted.isEmpty()) {
        for (int n = 0; n < nl.nets.size() && plotted.size() < 8; ++n) {
            const QString node = deck.nodeOfNet.value(n);
            if (node != u"0" && nl.nets[n].name != QStringLiteral("N%1").arg(n + 1))
                plotted << std::pair{nl.nets[n].name, node};
        }
    }

    if (r.plotType == u"op") {
        if (sweep) {
            // one line per run: the plotted nodes' voltages
            QStringList rows{QStringLiteral("<tr><th></th>")};
            for (const auto& [label, node] : std::as_const(plotted)) rows.first() += QStringLiteral("<th>%1</th>").arg(label);
            for (int i = 0; i < results.size(); ++i) {
                QString row = QStringLiteral("<tr><td>%1</td>").arg(m_runs[i].label);
                for (const auto& [label, node] : std::as_const(plotted))
                    row += QStringLiteral("<td>%1</td>").arg(WaveformView::siFormat(voltage(results[i], node), QStringLiteral("V"), 4));
                rows << row + QStringLiteral("</tr>");
            }
            QMessageBox::information(this, tr("Operating point sweep"),
                                     QStringLiteral("<table cellspacing=6>%1</table>").arg(rows.join(QString())));
        }
        for (int n = 0; n < nl.nets.size(); ++n) {
            const double v = voltage(r, deck.nodeOfNet.value(n));
            if (std::isfinite(v)) overlay.netVoltage.insert(n, v);
        }
        m_scene->setAnalogOverlay(overlay);
        statusBar()->showMessage(tr("Operating point: node voltages are shown on the wires"), 8000);
        return;
    }
    m_scene->setAnalogOverlay(overlay);

    const bool noise = r.plotType == u"noise";
    if (noise)
        plotted = {{tr("output noise"), QStringLiteral("onoise_spectrum")}, {tr("input noise"), QStringLiteral("inoise_spectrum")}};
    const spice::Vector* scale = r.find(r.scale);
    if (!scale) return;
    const bool ac = r.plotType == u"ac";
    auto xOf = [](const spice::Vector& v) {
        QVector<double> x;
        for (int i = 0; i < v.size(); ++i) x << (v.isComplex() ? v.complex[i].real() : v.real[i]);
        return x;
    };
    const QVector<double> x = xOf(*scale);
    // device probes: current, power and impedance (CircuitMaker's test points)
    struct DeviceProbe { QString label, mode, current, a, b; };
    QVector<DeviceProbe> devices;
    if (!noise) {
        for (const Component& c : m_circuit->components()) {
            const QString mode = c.props.value(QStringLiteral("probe")).toString();
            const QString current = deck.currentOf.value(c.id);
            if (mode.isEmpty() || current.isEmpty()) continue;
            const QString label = mode == u"I" ? QStringLiteral("I(%1)").arg(c.designator)
                                : mode == u"P" ? QStringLiteral("P(%1)").arg(c.designator)
                                : ac ? QStringLiteral("|Z|(%1) dBΩ").arg(c.designator) : QStringLiteral("Z(%1)").arg(c.designator);
            devices << DeviceProbe{label, mode, current, deck.nodeOfNet.value(nl.netOf(c.id, 0), QStringLiteral("0")),
                                   deck.nodeOfNet.value(nl.netOf(c.id, 1), QStringLiteral("0"))};
        }
    }
    if (plotted.isEmpty() && devices.isEmpty()) {
        for (auto it = r.vectors.cbegin(); it != r.vectors.cend() && plotted.size() < 6; ++it)
            if (it.key() != r.scale && !it.key().contains(u'#'))
                plotted << std::pair{it.key(), it.key()};
    }
    auto sample = [](const spice::Result& res, const QString& name, int k) -> std::complex<double> {
        if (name == u"0") return 0;
        const spice::Vector* v = res.find(name);
        if (!v) return qQNaN();
        return v->isComplex() ? v->complex.value(k) : std::complex<double>(v->real.value(k));
    };
    auto deviceY = [&](const spice::Result& res, const DeviceProbe& d, int n) {
        QVector<double> y;
        for (int k = 0; k < n; ++k) {
            const std::complex<double> i = sample(res, d.current, k), v = sample(res, d.a, k) - sample(res, d.b, k);
            if (d.mode == u"I") y << (ac ? 20 * std::log10(std::max(std::abs(i), 1e-30)) : i.real());
            else if (d.mode == u"P") y << (ac ? std::abs(v) * std::abs(i) : v.real() * i.real());
            else y << (ac ? 20 * std::log10(std::max(std::abs(v) / std::max(std::abs(i), 1e-30), 1e-30))
                          : v.real() / i.real());
        }
        return y;
    };
    // y of one node in one run, on that run's own x
    auto yOf = [&](const spice::Result& res, const QString& node, int n) {
        const spice::Vector* v = node == u"0" ? nullptr : res.find(node);
        QVector<double> y;
        for (int k = 0; k < n; ++k) {
            if (!v) y << 0;
            else if (v->isComplex()) y << 20 * std::log10(std::max(std::abs(v->complex.value(k)), 1e-30));
            else y << v->real.value(k);
        }
        return y;
    };
    // another run's curve on the first run's x (time steps differ between runs)
    auto resample = [&](const QVector<double>& rx, const QVector<double>& ry) {
        QVector<double> y;
        int k = 0;
        for (double t : x) {
            while (k + 2 < rx.size() && rx[k + 1] < t) ++k;
            if (rx.size() < 2) { y << ry.value(0, qQNaN()); continue; }
            const double a = rx[k], b = rx[k + 1];
            y << (b > a ? ry[k] + (ry[k + 1] - ry[k]) * std::clamp((t - a) / (b - a), 0.0, 1.0) : ry[k]);
        }
        return y;
    };

    if (analysis.type == AnalysisSettings::Type::Fourier) {
        const double f0 = spice::parseNumber(analysis.fourierFreq).value_or(deck.generatorFrequency);
        if (f0 <= 0) {
            QMessageBox::warning(this, tr("Fourier analysis"), tr("Set the fundamental frequency (no signal generator found)."));
            return;
        }
        QVector<double> fx;
        QVector<WaveformView::Trace> traces;
        QStringList thd;
        for (int i = 0; i < results.size(); ++i) {
            const spice::Vector* t = results[i].find(QStringLiteral("time"));
            if (!t) continue;
            for (const auto& [label, node] : std::as_const(plotted)) {
                const spice::Fourier f = spice::fourier(xOf(*t), yOf(results[i], node, t->size()), f0, analysis.fourierHarmonics);
                if (f.magnitude.isEmpty()) continue;
                fx = f.frequency;
                const QString name = sweep ? label + u" @ " + m_runs[i].label : label;
                traces << WaveformView::Trace{name, f.magnitude, WaveformView::traceColor(int(traces.size()))};
                thd << tr("%1: THD %2%").arg(name, QString::number(f.thd, 'f', 2));
            }
        }
        if (traces.isEmpty()) {
            QMessageBox::warning(this, tr("Fourier analysis"), tr("The transient is shorter than one period of %1.")
                                                                   .arg(WaveformView::siFormat(f0, QStringLiteral("Hz"))));
            return;
        }
        m_waves->setData(tr("Fourier (harmonic peaks)"), tr("Frequency"), QStringLiteral("Hz"), QStringLiteral("V"), fx,
                         traces, false);
        statusBar()->showMessage(thd.join(QStringLiteral("   ")), 20000);
        m_wavesDock->show();
        m_wavesDock->raise();
        return;
    }

    QVector<WaveformView::Trace> traces;
    for (int i = 0; i < results.size(); ++i) {
        const spice::Vector* sc = results[i].find(results[i].scale);
        if (!sc) continue;
        const QVector<double> rx = xOf(*sc);
        for (const auto& [label, node] : std::as_const(plotted)) {
            const QVector<double> y = yOf(results[i], node, int(rx.size()));
            traces << WaveformView::Trace{sweep ? label + u" @ " + m_runs[i].label : label, i == 0 ? y : resample(rx, y),
                                          WaveformView::traceColor(int(traces.size()))};
        }
        for (const DeviceProbe& d : std::as_const(devices)) {
            const QVector<double> y = deviceY(results[i], d, int(rx.size()));
            traces << WaveformView::Trace{sweep ? d.label + u" @ " + m_runs[i].label : d.label, i == 0 ? y : resample(rx, y),
                                          WaveformView::traceColor(int(traces.size()))};
        }
    }
    if (r.plotType == u"tran")
        m_waves->setData(tr("Transient"), tr("Time"), QStringLiteral("s"), QStringLiteral("V"), x, traces, false);
    else if (ac)
        m_waves->setData(tr("AC sweep (magnitude)"), tr("Frequency"), QStringLiteral("Hz"), QStringLiteral("dB"), x, traces, true);
    else if (noise)
        m_waves->setData(tr("Noise spectral density"), tr("Frequency"), QStringLiteral("Hz"), QStringLiteral("V/√Hz"), x,
                         traces, true);
    else
        m_waves->setData(tr("DC sweep"), analysis.dcSource, QStringLiteral("V"), QStringLiteral("V"), x, traces, false);
    m_wavesDock->show();
    m_wavesDock->raise();
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About CirMaker Qt"),
                       tr("<h3>CirMaker Qt %1</h3>"
                          "<p>A schematic editor and digital logic simulator in the spirit of CircuitMaker 2000, "
                          "built with Qt %2.</p>"
                          "<p>Opens CircuitMaker 2000 text circuits (.ckt). The device library is an independent "
                          "reimplementation; no files from the original product are included.</p>")
                           .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::addRecent(const QString& path)
{
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recent")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 8)
        recent.removeLast();
    settings.setValue(QStringLiteral("recent"), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recent")).toStringList();
    for (const QString& path : recent) {
        QAction* a = m_recentMenu->addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path] {
            if (maybeSave())
                openFile(path);
        });
    }
    m_recentMenu->setEnabled(!recent.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    e->accept();
}

} // namespace cm
