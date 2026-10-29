#include "ui/MainWindow.h"

#include "core/Bom.h"
#include "core/PcbNetlist.h"
#include "core/Circuit.h"
#include "core/io/CktImporter.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "ui/LibraryBrowser.h"
#include "sim/digital/EventSimulator.h"
#include "ui/AnalysisDialog.h"
#include "ui/ComponentItem.h"
#include "ui/PreferencesDialog.h"
#include "ui/WaveformView.h"
#include "ui/SchematicView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
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
    connect(&m_analogWatcher, &QFutureWatcher<spice::Result>::finished, this, &MainWindow::analogFinished);

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

    m_save = new QAction(themeIcon("document-save", QStyle::SP_DialogSaveButton), tr("&Save"), this);
    m_save->setShortcut(QKeySequence::Save);
    connect(m_save, &QAction::triggered, this, &MainWindow::save);

    m_saveAs = new QAction(tr("Save &As..."), this);
    m_saveAs->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAs, &QAction::triggered, this, &MainWindow::saveAs);

    m_export = new QAction(tr("&Export Schematic (PNG, SVG, PDF)..."), this);
    connect(m_export, &QAction::triggered, this, &MainWindow::exportImage);

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
        const spice::Deck deck = spice::build(*m_circuit, LibraryRegistry::instance(), m_scene->netlist());
        writeTextFile(tr("Export SPICE netlist"), QStringLiteral("cir"), tr("SPICE netlist (*.cir *.sp)"), deck.text);
    });
    m_exportPcb = new QAction(tr("&PCB Netlist..."), this);
    connect(m_exportPcb, &QAction::triggered, this, [this] {
        m_scene->refreshNetlist();
        writeTextFile(tr("Export PCB netlist"), QStringLiteral("net"), tr("Protel netlist (*.net)"),
                      PcbNetlist::write(*m_circuit, LibraryRegistry::instance(), m_scene->netlist()));
    });
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

    m_prefs = new QAction(themeIcon("preferences-system", QStyle::SP_FileDialogDetailedView), tr("Pre&ferences..."), this);
    m_prefs->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::Key_Comma)});
    m_prefs->setMenuRole(QAction::PreferencesRole);
    connect(m_prefs, &QAction::triggered, this, &MainWindow::editPreferences);

    m_about = new QAction(tr("&About CirMaker Qt"), this);
    connect(m_about, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({m_new, m_open});
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addActions({m_import});
    file->addSeparator();
    file->addActions({m_save, m_saveAs});
    file->addSeparator();
    QMenu* exportMenu = file->addMenu(tr("E&xport"));
    exportMenu->addActions({m_export, m_exportSpice, m_exportPcb});
    file->addActions({m_bom, m_print});
    file->addSeparator();
    file->addAction(m_quit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({m_undoAct, m_redoAct});
    edit->addSeparator();
    edit->addActions({m_cut, m_copy, m_paste, m_delete, m_duplicate, m_selectAll, m_find});
    edit->addSeparator();
    edit->addActions({m_rotate, m_mirror, m_props, m_straighten, m_designations, m_propDelays});
    edit->addSeparator();
    edit->addAction(m_titleBlock);
    edit->addSeparator();
    edit->addAction(m_prefs);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName(QStringLiteral("viewMenu"));
    view->addActions({m_scale, m_normalSize, m_zoomFit, m_zoomIn, m_zoomOut, m_home, m_refresh, m_collapseTree});
    view->addSeparator();
    view->addActions({m_grid, m_showTitleBlock});
    view->addSeparator();

    QMenu* tools = menuBar()->addMenu(tr("&Options"));
    QMenu* cursorTools = tools->addMenu(tr("Cursor &Tools"));
    cursorTools->addActions(m_tools->actions());
    tools->addSeparator();
    tools->addActions({m_showNodeNames, m_showPinDots});
    tools->addSeparator();
    tools->addAction(m_prefs);

    QMenu* simulation = menuBar()->addMenu(tr("&Simulation"));
    simulation->addActions({m_digitalMode, m_analogMode});
    simulation->addSeparator();
    simulation->addActions({m_run, m_step, m_trace, m_reset, m_stop});
    simulation->addSeparator();
    simulation->addAction(m_analysisSetup);

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
    for (QAction* a : {m_new, m_open, m_import, m_cut, m_paste, m_delete, m_selectAll, m_props, m_find, m_titleBlock,
                       m_showTitleBlock})
        a->setEnabled(!simulating);
    m_undoAct->setEnabled(!simulating && m_undo->canUndo());
    m_redoAct->setEnabled(!simulating && m_undo->canRedo());
    m_undoAct->setText(m_undo->canUndo() ? tr("&Undo %1").arg(m_undo->undoText()) : tr("&Undo"));
    m_redoAct->setText(m_undo->canRedo() ? tr("&Redo %1").arg(m_undo->redoText()) : tr("&Redo"));
    for (QAction* a : {m_cut, m_copy, m_delete, m_rotate, m_mirror})
        a->setEnabled(!simulating && hasSel);
    m_props->setEnabled(!simulating && m_scene->selectedItems().size() == 1);
    m_tools->setEnabled(!simulating);
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
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
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
    updateTitle();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open circuit"), settings.value(QStringLiteral("dirs/open")).toString(),
        tr("Circuits (*.cmq *.ckt *.CKT);;CirMaker Qt (*.cmq);;CircuitMaker 2000 (*.ckt *.CKT);;All files (*)"));
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
        NativeFormat::deserialize(NativeFormat::serialize(imported), *m_circuit, nullptr);
        m_undo->clear();
        m_undo->resetClean();
        // imported circuits are saved as .cmq, never over the original
        m_path.clear();
        updateTitle();
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
    } else {
        Circuit loaded;
        if (!NativeFormat::deserialize(data, loaded, &error)) {
            return fail(tr("Open"), tr("Cannot open %1:\n%2").arg(QFileInfo(path).fileName(), error));
        }
        m_circuit->clear();
        NativeFormat::deserialize(data, *m_circuit, nullptr);
        m_undo->clear();
        m_path = path;
        updateTitle();
        addRecent(path);
    }
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

void MainWindow::writeTextFile(const QString& title, const QString& suffix, const QString& filter, const QString& text)
{
    const QString base = m_path.isEmpty() ? tr("circuit") : QFileInfo(m_path).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, title, base + u'.' + suffix, filter);
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || (f.write(text.toUtf8()), !f.commit())) {
        QMessageBox::critical(this, title, tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return;
    }
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
        const double scale = std::min(2.0, 6000.0 / std::max(src.width(), src.height()));
        QImage img((src.size() * scale).toSize(), QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        m_scene->render(&p, {}, src);
        p.end();
        ok = img.save(path);
    }
    m_scene->setExporting(false);
    return ok;
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
}

void MainWindow::showDigitalWaveforms()
{
    const auto* sim = m_scene->simulator();
    if (!sim || m_scene->waveformLanes().isEmpty())
        return;
    QVector<WaveformView::Lane> lanes;
    for (const SchematicScene::Lane& l : m_scene->waveformLanes()) {
        WaveformView::Lane lane{l.name, {}};
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
    AnalysisDialog dlg(m_circuit->analysis(), sources, this);
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
    m_runningDeck = spice::build(*m_circuit, LibraryRegistry::instance(), m_scene->netlist());
    if (!m_runningDeck.ok) {
        QMessageBox::warning(this, tr("Analog simulation"), m_runningDeck.warnings.join(u'\n'));
        return;
    }
    if (!m_runningDeck.warnings.isEmpty())
        statusBar()->showMessage(m_runningDeck.warnings.join(QStringLiteral("; ")), 10000);
    const QString text = m_runningDeck.text;
    m_analogWatcher.setFuture(QtConcurrent::run([text] { return spice::NgspiceEngine::instance().run(text); }));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    updateActions();
}

void MainWindow::analogFinished()
{
    QApplication::restoreOverrideCursor();
    const spice::Result r = m_analogWatcher.result();
    updateActions();
    if (!r.ok) {
        QMessageBox box(QMessageBox::Critical, tr("Analog simulation"), tr("SPICE could not simulate the circuit."),
                        QMessageBox::Ok, this);
        box.setInformativeText(r.error);
        box.setDetailedText(tr("Netlist:\n%1\nSPICE output:\n%2").arg(m_runningDeck.text, r.log));
        box.exec();
        return;
    }
    showAnalogResults(r);
}

void MainWindow::showAnalogResults(const spice::Result& r)
{
    const spice::Deck& deck = m_runningDeck;
    const Netlist& nl = m_scene->netlist();
    auto voltage = [&](const QString& node) -> double {
        if (node == u"0") return 0;
        return r.last(node).value_or(qQNaN());
    };
    auto netVoltage = [&](ItemId id, int pin) { return voltage(deck.nodeOfNet.value(nl.netOf(id, pin), QStringLiteral("0"))); };

    // meters and displays read the operating point / final values
    SchematicScene::AnalogOverlay overlay;
    for (auto it = deck.meters.cbegin(); it != deck.meters.cend(); ++it) {
        const spice::Meter& m = it.value();
        const double v = m.amps ? r.last(m.current).value_or(qQNaN()) : voltage(m.plus) - voltage(m.minus);
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
        for (int n = 0; n < nl.nets.size(); ++n) {
            const double v = voltage(deck.nodeOfNet.value(n));
            if (std::isfinite(v)) overlay.netVoltage.insert(n, v);
        }
        m_scene->setAnalogOverlay(overlay);
        statusBar()->showMessage(tr("Operating point: node voltages are shown on the wires"), 8000);
        return;
    }
    m_scene->setAnalogOverlay(overlay);

    const spice::Vector* scale = r.find(r.scale);
    if (!scale) return;
    const bool ac = r.plotType == u"ac";
    QVector<double> x;
    for (int i = 0; i < scale->size(); ++i)
        x << (scale->isComplex() ? scale->complex[i].real() : scale->real[i]);
    if (plotted.isEmpty()) {
        for (auto it = r.vectors.cbegin(); it != r.vectors.cend() && plotted.size() < 6; ++it)
            if (it.key() != r.scale && !it.key().contains(u'#'))
                plotted << std::pair{it.key(), it.key()};
    }
    QVector<WaveformView::Trace> traces;
    for (int i = 0; i < plotted.size(); ++i) {
        const spice::Vector* v = plotted[i].second == u"0" ? nullptr : r.find(plotted[i].second);
        WaveformView::Trace t{plotted[i].first, {}, WaveformView::traceColor(probeNets.isEmpty() ? i : i)};
        for (int k = 0; k < x.size(); ++k) {
            if (!v) t.y << 0;
            else if (ac) t.y << 20 * std::log10(std::max(std::abs(v->complex.value(k)), 1e-30));
            else t.y << v->real.value(k);
        }
        traces << t;
    }
    if (r.plotType == u"tran")
        m_waves->setData(tr("Transient"), tr("Time"), QStringLiteral("s"), QStringLiteral("V"), x, traces, false);
    else if (ac)
        m_waves->setData(tr("AC sweep (magnitude)"), tr("Frequency"), QStringLiteral("Hz"), QStringLiteral("dB"), x, traces, true);
    else
        m_waves->setData(tr("DC sweep"), m_circuit->analysis().dcSource, QStringLiteral("V"), QStringLiteral("V"), x, traces, false);
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
