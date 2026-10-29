#include "ui/MainWindow.h"

#include "core/Bom.h"
#include "core/Circuit.h"
#include "core/io/CktImporter.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "ui/LibraryBrowser.h"
#include "ui/SchematicView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
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

    createActions();
    createMenus();
    createToolBars();
    menuBar()->findChild<QMenu*>(QStringLiteral("viewMenu"))->addAction(dock->toggleViewAction());

    m_coords = new QLabel;
    m_simStatus = new QLabel;
    statusBar()->addPermanentWidget(m_simStatus);
    statusBar()->addPermanentWidget(m_coords);

    connect(m_scene, &SchematicScene::cursorMoved, this,
            [this](QPointF p) { m_coords->setText(QStringLiteral("x %1  y %2").arg(p.x()).arg(p.y())); });
    connect(m_scene, &SchematicScene::statusMessage, this, [this](const QString& t) { statusBar()->showMessage(t, 8000); });
    connect(m_scene, &SchematicScene::simulationChanged, this, &MainWindow::updateActions);
    connect(m_scene, &SchematicScene::simTimeChanged, this,
            [this](quint64 t) { m_simStatus->setText(tr("t = %1 ticks").arg(t)); });
    connect(m_scene, &SchematicScene::toolChanged, this, [this](SchematicScene::Tool t) {
        m_toolSelect->setChecked(t == SchematicScene::Tool::Select || t == SchematicScene::Tool::Place);
        m_toolWire->setChecked(t == SchematicScene::Tool::Wire);
        m_toolBus->setChecked(t == SchematicScene::Tool::Bus);
        m_toolNetLabel->setChecked(t == SchematicScene::Tool::NetLabel);
        m_toolText->setChecked(t == SchematicScene::Tool::Text);
    });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updateActions);
    connect(m_undo, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    rebuildRecentMenu();
    updateActions();
    updateTitle();
    m_view->centerOn(200, 150);
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
    m_quit->setShortcut(QKeySequence::Quit);
    connect(m_quit, &QAction::triggered, this, &QWidget::close);

    m_undoAct = m_undo->createUndoAction(this, tr("&Undo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_undoAct->setIcon(themeIcon("edit-undo", QStyle::SP_ArrowBack));
    m_redoAct = m_undo->createRedoAction(this, tr("&Redo"));
    m_redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    m_redoAct->setIcon(themeIcon("edit-redo", QStyle::SP_ArrowForward));

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
    m_zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_view->zoomBy(1.25); });
    m_zoomOut = new QAction(themeIcon("zoom-out", QStyle::SP_ArrowDown), tr("Zoom &Out"), this);
    m_zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOut, &QAction::triggered, this, [this] { m_view->zoomBy(0.8); });
    m_zoomFit = new QAction(themeIcon("zoom-fit-best", QStyle::SP_DesktopIcon), tr("&Fit to Window"), this);
    m_zoomFit->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(m_zoomFit, &QAction::triggered, m_view, &SchematicView::zoomToFit);
    m_grid = new QAction(tr("Show &Grid"), this);
    m_grid->setCheckable(true);
    m_grid->setChecked(true);
    connect(m_grid, &QAction::toggled, m_scene, &SchematicScene::setGridVisible);

    m_tools = new QActionGroup(this);
    m_toolSelect = new QAction(drawnIcon([](QPainter& p) {
                                   p.setBrush(p.pen().color());
                                   p.drawPolygon(QPolygonF({QPointF(8, 4), QPointF(8, 24), QPointF(13, 19), QPointF(17, 28),
                                                            QPointF(20, 27), QPointF(16, 18), QPointF(23, 18)}));
                               }),
                               tr("&Arrow"), m_tools);
    m_toolSelect->setShortcut(Qt::Key_A);
    m_toolWire = new QAction(drawnIcon([](QPainter& p) {
                                 p.drawPolyline(QPolygonF({QPointF(4, 8), QPointF(16, 8), QPointF(16, 24), QPointF(28, 24)}));
                                 p.setBrush(p.pen().color());
                                 p.drawEllipse(QPointF(16, 8), 2.5, 2.5);
                             }),
                             tr("&Wire"), m_tools);
    m_toolWire->setShortcut(Qt::Key_W);
    m_toolBus = new QAction(drawnIcon([](QPainter& p) {
                                QPen pen = p.pen();
                                pen.setWidthF(5);
                                p.setPen(pen);
                                p.drawLine(QPointF(4, 16), QPointF(28, 16));
                            }),
                            tr("&Bus"), m_tools);
    m_toolBus->setShortcut(Qt::Key_B);
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
    m_toolNetLabel->setShortcut(Qt::Key_N);
    m_toolNetLabel->setToolTip(tr("Net label: points with the same name are connected"));
    m_toolText = new QAction(drawnIcon([](QPainter& p) {
                                 QFont f = p.font();
                                 f.setPixelSize(24);
                                 f.setBold(true);
                                 p.setFont(f);
                                 p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, QStringLiteral("T"));
                             }),
                             tr("&Text"), m_tools);
    m_toolText->setShortcut(Qt::Key_T);
    for (QAction* a : m_tools->actions())
        a->setCheckable(true);
    m_toolSelect->setChecked(true);
    connect(m_toolSelect, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Select); });
    connect(m_toolWire, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Wire); });
    connect(m_toolText, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Text); });
    connect(m_toolBus, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::Bus); });
    connect(m_toolNetLabel, &QAction::triggered, this, [this] { m_scene->setTool(SchematicScene::Tool::NetLabel); });

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
    m_run->setShortcut(Qt::Key_F9);
    connect(m_run, &QAction::triggered, this, [this] { m_scene->running() ? m_scene->pause() : m_scene->run(); });
    m_step = new QAction(themeIcon("media-skip-forward", QStyle::SP_MediaSkipForward), tr("S&tep"), this);
    m_step->setShortcut(Qt::Key_F10);
    m_step->setToolTip(tr("Advance to the next logic change"));
    connect(m_step, &QAction::triggered, m_scene, &SchematicScene::stepSimulation);
    m_reset = new QAction(themeIcon("view-refresh", QStyle::SP_BrowserReload), tr("R&eset"), this);
    m_reset->setShortcut(Qt::CTRL | Qt::Key_F9);
    connect(m_reset, &QAction::triggered, m_scene, &SchematicScene::resetSimulation);
    m_stop = new QAction(themeIcon("media-playback-stop", QStyle::SP_MediaStop), tr("&Stop (back to editing)"), this);
    m_stop->setShortcut(Qt::SHIFT | Qt::Key_F9);
    connect(m_stop, &QAction::triggered, m_scene, &SchematicScene::stopSimulation);

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
    file->addActions({m_export, m_bom, m_print});
    file->addSeparator();
    file->addAction(m_quit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({m_undoAct, m_redoAct});
    edit->addSeparator();
    edit->addActions({m_cut, m_copy, m_paste, m_delete, m_selectAll, m_find});
    edit->addSeparator();
    edit->addActions({m_rotate, m_mirror, m_props});
    edit->addSeparator();
    edit->addAction(m_titleBlock);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName(QStringLiteral("viewMenu"));
    view->addActions({m_zoomIn, m_zoomOut, m_zoomFit});
    view->addSeparator();
    view->addActions({m_grid, m_showTitleBlock});
    view->addSeparator();

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addActions(m_tools->actions());

    QMenu* simulation = menuBar()->addMenu(tr("&Simulation"));
    simulation->addActions({m_run, m_step, m_reset, m_stop});

    QMenu* help = menuBar()->addMenu(tr("&Help"));
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
    sim->addActions({m_run, m_step, m_reset, m_stop});
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
    for (QAction* a : {m_cut, m_copy, m_delete, m_rotate, m_mirror})
        a->setEnabled(!simulating && hasSel);
    m_props->setEnabled(!simulating && m_scene->selectedItems().size() == 1);
    m_tools->setEnabled(!simulating);
    m_step->setEnabled(!m_scene->running());
    m_reset->setEnabled(simulating);
    m_stop->setEnabled(simulating);
    const bool running = m_scene->running();
    m_run->setText(running ? tr("&Pause") : tr("&Run"));
    m_run->setIcon(running ? themeIcon("media-playback-pause", QStyle::SP_MediaPause)
                           : themeIcon("media-playback-start", QStyle::SP_MediaPlay));
    if (!simulating)
        m_simStatus->setText(tr("Editing"));
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
