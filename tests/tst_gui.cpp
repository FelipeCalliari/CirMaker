// Widget-level tests on the offscreen platform: scene interaction paths that
// crashed or need the real event flow.

#include "TestUtil.h"
#include "core/io/CktImporter.h"
#include "core/WireGeometry.h"

#include "ui/ComponentItem.h"
#include "ui/MainWindow.h"
#include "ui/PropertiesDialog.h"
#include "ui/DataSequencerDialog.h"
#include "ui/MemoryEditorDialog.h"
#include "ui/MacroSymbolDialog.h"
#include "library/Macros.h"
#include "core/io/NativeFormat.h"
#include <QJsonArray>
#include <QJsonDocument>
#include "ui/SignalGenDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"

#include <QCheckBox>
#include <QComboBox>
#include "ui/SchematicScene.h"
#include "ui/WireItem.h"

#include <QContextMenuEvent>
#include <QGraphicsView>
#include <QPainter>

#include "sim/digital/EventSimulator.h"
#include <QUndoStack>

using namespace cm;
using namespace cm::test;

class TestGui : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { library(); }
    void choosingAnotherDeviceReplacesGhost();
    void windowClosesCleanly();
    void dragWireSegment();
    void dragWireEnd();
    void wiresFollowReshapedWire();
    void simulationStress();
    void buttonsAnimate_data();
    void buttonsAnimate();
    void circuitMakerShortcuts();
    void editMenuTools();
    void ledColourProperty_data();
    void ledColourProperty();
    void patternEditorFills();
    void dataSequencerDialogKeepsWords();
    void memoryEditorText();
    void contextMenuSelectsItem();
    void sceneGrowsWithItems();
    void breakpointStopsSimulation();
    void mergePlacesBelow();
    void probeToolSurvivesRun();
    void faultLockRestrictsScene();
    void simulationAppliesFaults();
    void labelPositionsAndBorder();
    void signalGenDialog();
    void groupItemsAndPlacement();
    void themeColours();
    void deviceProbeCycles();
    void analysisWindowTools();
    void propertiesVisibility();
    void stepSize();
    void wireAutoRoutes();
    void macroSymbolLayout();
    void hexKeyTakesKeyboard();
    void switchesClickFastAndKeyboard();
    void dragDesignator();
    void clickSelectsWire();
    void screenshot();
    void busEntriesFollowBus();
    void draggedBusKeepsConnections();
    void snapOffAndConnectionArea();
};

namespace {

int componentItems(QGraphicsScene& scene)
{
    int n = 0;
    for (QGraphicsItem* it : scene.items())
        n += it->type() == ComponentItem::Type;
    return n;
}

} // namespace

void TestGui::choosingAnotherDeviceReplacesGhost()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    scene.beginPlace(QStringLiteral("and2"));
    scene.beginPlace(QStringLiteral("or2"));
    scene.beginPlace(QStringLiteral("or2"));
    QCOMPARE(componentItems(scene), 1);              // only the current ghost
    scene.setTool(SchematicScene::Tool::Select);
    QCOMPARE(componentItems(scene), 0);
    // editing a stray item must never dereference a missing component
    ComponentItem stray(Component{}, library().find(QStringLiteral("and2")));
    scene.editItem(&stray);
}

void TestGui::windowClosesCleanly()
{
    auto* w = new MainWindow;
    w->show();
    QVERIFY(QTest::qWaitForWindowExposed(w));
    w->showLibrarySheet();
    delete w;                                        // used to abort in QUndoStack::clear
}

namespace {

struct ViewFixture {
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene{&circuit, &undo, library()};
    QGraphicsView view{&scene};
    ViewFixture()
    {
        view.resize(800, 600);
        view.setSceneRect(-400, -300, 800, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
    }
    QPoint at(QPointF scene) const { return view.mapFromScene(scene); }
    void drag(QPointF from, QPointF to)
    {
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, at(from));
        QTest::mouseMove(view.viewport(), at((from + to) / 2));
        QTest::mouseMove(view.viewport(), at(to));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, at(to));
    }
};

} // namespace

void TestGui::dragWireSegment()
{
    ViewFixture f;
    const ItemId id = f.circuit.addWire({0, {{-100, 0}, {0, 0}, {0, 100}, {100, 100}}});
    f.drag({0, 50}, {60, 50});                       // vertical middle segment slides right
    QCOMPARE(f.circuit.wire(id)->points, QVector<QPointF>({{-100, 0}, {60, 0}, {60, 100}, {100, 100}}));
    f.undo.undo();
    QCOMPARE(f.circuit.wire(id)->points.at(1), QPointF(0, 0));

    // as in CircuitMaker, free ends move with the segment
    const ItemId straight = f.circuit.addWire({0, {{-100, -200}, {100, -200}}});
    f.drag({0, -200}, {0, -150});
    QCOMPARE(f.circuit.wire(straight)->points, QVector<QPointF>({{-100, -150}, {100, -150}}));

    // an end on a part's pin stays there and its segment grows an elbow
    place(f.circuit, QStringLiteral("resistor"), {-160, -400}, QStringLiteral("R1"));     // pin 2 at (-100, -400)
    const ItemId pinned = f.circuit.addWire({0, {{-100, -400}, {100, -400}}});
    f.drag({0, -400}, {0, -350});
    QCOMPARE(f.circuit.wire(pinned)->points, QVector<QPointF>({{-100, -400}, {-100, -350}, {100, -350}}));
}

void TestGui::dragWireEnd()
{
    ViewFixture f;
    const ItemId id = f.circuit.addWire({0, {{-100, 0}, {100, 0}}});
    f.drag({100, 0}, {150, 80});
    QCOMPARE(f.circuit.wire(id)->points.first(), QPointF(-100, 0));
    QCOMPARE(f.circuit.wire(id)->points.last(), QPointF(150, 80));
    QCOMPARE(f.circuit.wire(id)->points.size(), 3);  // orthogonal elbow added
}

void TestGui::wiresFollowReshapedWire()
{
    ViewFixture f;
    const ItemId wire = f.circuit.addWire({0, {{-100, 0}, {0, 0}, {0, 100}, {100, 100}}});
    const ItemId tee = f.circuit.addWire({0, {{-100, 50}, {0, 50}}});
    const ItemId corner = f.circuit.addWire({0, {{0, -100}, {0, 0}}});
    const ItemId label = place(f.circuit, QStringLiteral("netlabel"), {0, 80}, {}, QStringLiteral("N"));
    const QPointF labelAt = f.circuit.component(label)->pos;
    f.drag({0, 30}, {60, 30});                       // slide the middle segment
    QCOMPARE(f.circuit.wire(wire)->points, QVector<QPointF>({{-100, 0}, {60, 0}, {60, 100}, {100, 100}}));
    QCOMPARE(f.circuit.wire(tee)->points.last(), QPointF(60, 50));
    QCOMPARE(f.circuit.wire(tee)->points.first(), QPointF(-100, 50));
    QCOMPARE(f.circuit.wire(corner)->points.last(), QPointF(60, 0));
    QCOMPARE(f.circuit.component(label)->pos, labelAt + QPointF(60, 0));
    f.undo.undo();                                   // one step
    QCOMPARE(f.circuit.wire(tee)->points.last(), QPointF(0, 50));
    QCOMPARE(f.circuit.component(label)->pos, labelAt);

    // a free end moves with the segment and the wire meeting it there
    // stretches; one along the segment slides
    const ItemId straight = f.circuit.addWire({0, {{-100, -200}, {100, -200}}});
    const ItemId atEnd = f.circuit.addWire({0, {{-100, -250}, {-100, -200}}});
    const ItemId along = f.circuit.addWire({0, {{0, -250}, {0, -200}}});
    f.drag({50, -200}, {50, -150});
    QCOMPARE(f.circuit.wire(straight)->points.first(), QPointF(-100, -150));
    QCOMPARE(f.circuit.wire(atEnd)->points.last(), QPointF(-100, -150));
    QCOMPARE(f.circuit.wire(along)->points.last(), QPointF(0, -150));

    // a dragged end carries the wire ends that meet it
    const ItemId a = f.circuit.addWire({0, {{200, 0}, {300, 0}}});
    const ItemId b = f.circuit.addWire({0, {{300, -100}, {300, 0}}});
    f.drag({300, 0}, {340, 60});
    const QPointF end = f.circuit.wire(a)->points.last();
    QCOMPARE(f.circuit.wire(b)->points.last(), end);
    QCOMPARE(end, QPointF(340, 60));

    // moved as a selection, wires ending on the moved wire follow it
    const ItemId moved = f.circuit.addWire({0, {{-300, 200}, {-100, 200}}});
    const ItemId hanging = f.circuit.addWire({0, {{-200, 250}, {-200, 200}}});
    const ItemId other = f.circuit.addWire({0, {{-300, 150}, {-250, 150}}});
    for (QGraphicsItem* it : f.scene.items())
        if (auto* w = qgraphicsitem_cast<WireItem*>(it); w && (w->id() == moved || w->id() == other)) w->setSelected(true);
    f.drag({-150, 200}, {-150, 180});
    QCOMPARE(f.circuit.wire(moved)->points.first(), QPointF(-300, 180));
    QCOMPARE(f.circuit.wire(hanging)->points.last(), QPointF(-200, 180));
    QCOMPARE(f.circuit.wire(hanging)->points.first(), QPointF(-200, 250));
}

void TestGui::simulationStress()
{
    const QString file = QDir::homePath()
                       + QStringLiteral("/Documents/github/_calliari_/tecdigi/CircuitMaker/FSM_0101_Mealy.CKT");
    if (!QFile::exists(file))
        QSKIP("course circuit not available");
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVERIFY(w.openFile(file, false));
    SchematicScene* scene = w.scene();
    const int components = int(scene->circuit()->components().size());
    for (int round = 0; round < 3; ++round) {
        scene->run();
        QTest::qWait(300);
        w.undoAction()->trigger();                   // must not touch the circuit while simulating
        QCOMPARE(int(scene->circuit()->components().size()), components);
        scene->pause();
        for (int i = 0; i < 20; ++i)
            scene->stepSimulation();
        scene->resetSimulation();
        scene->stopSimulation();
        w.undoAction()->trigger();                   // undoes the switch positions, if any
    }
}

void TestGui::buttonsAnimate_data()
{
    QTest::addColumn<QString>("symbol");
    QTest::addColumn<QString>("press");
    QTest::newRow("NO push button") << "pb_no" << "press";
    QTest::newRow("NC push button") << "pb_nc" << "press";
    QTest::newRow("SPDT push button") << "pb_spdt" << "press";
    QTest::newRow("SPST switch") << "spst" << "toggle";
}

void TestGui::buttonsAnimate()
{
    QFETCH(QString, symbol);
    QFETCH(QString, press);
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    const ItemId id = place(circuit, symbol, {0, 0}, QStringLiteral("S1"));
    auto render = [&] {
        QImage img(200, 120, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        scene.render(&p, {}, QRectF(-50, -30, 100, 60));
        return img;
    };
    scene.run();
    scene.pause();
    const QImage released = render();
    QVERIFY(scene.simulating());
    // drive the model the way a mouse press does
    scene.simulator()->interact(id, press);
    const QImage pressed = render();
    QVERIFY2(released != pressed, "the symbol looks the same pressed and released");
    scene.stopSimulation();
}

void TestGui::circuitMakerShortcuts()
{
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QWidget* view = w.findChild<QGraphicsView*>();
    QVERIFY(view);
    view->setFocus();
    SchematicScene* scene = w.scene();

    QTest::keyClick(view, Qt::Key_W, Qt::AltModifier);
    QCOMPARE(scene->tool(), SchematicScene::Tool::Wire);
    QTest::keyClick(view, Qt::Key_D, Qt::AltModifier);
    QCOMPARE(scene->tool(), SchematicScene::Tool::Delete);
    QTest::keyClick(view, Qt::Key_A, Qt::AltModifier);
    QCOMPARE(scene->tool(), SchematicScene::Tool::Select);

    // device hotkey: '4' picks the 2-input AND (default assignment)
    QTest::keyClick(view, Qt::Key_4);
    QCOMPARE(scene->tool(), SchematicScene::Tool::Place);
    bool ghostIsAnd = false;
    for (QGraphicsItem* it : scene->items())
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it))
            ghostIsAnd = c->id() == 0 && c->component().symbolId == u"and2";
    QVERIFY(ghostIsAnd);
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(scene->tool(), SchematicScene::Tool::Select);
}

void TestGui::editMenuTools()
{
    ViewFixture f;
    // Set Designations: reading order, per prefix
    const ItemId a = place(f.circuit, QStringLiteral("and2"), {300, 0}, QStringLiteral("U7"));
    const ItemId b = place(f.circuit, QStringLiteral("and2"), {0, 0}, QStringLiteral("U3"));
    const ItemId r = place(f.circuit, QStringLiteral("resistor"), {0, 300}, QStringLiteral("R9"));
    f.scene.setDesignations({}, 1);
    QCOMPARE(f.circuit.component(b)->designator, QStringLiteral("U1"));
    QCOMPARE(f.circuit.component(a)->designator, QStringLiteral("U2"));
    QCOMPARE(f.circuit.component(r)->designator, QStringLiteral("R1"));

    // Straighten Wires removes a short zig-zag
    const ItemId w = f.circuit.addWire({0, {{-300, -200}, {-200, -200}, {-200, -190}, {-100, -190}}});
    f.scene.straightenWires();
    QCOMPARE(f.circuit.wire(w)->points.size(), 2);

    // arrow keys nudge the selected device one grid step
    f.scene.clearSelection();
    for (QGraphicsItem* it : f.scene.items())
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it); c && c->id() == r)
            c->setSelected(true);
    f.view.setFocus();
    QTest::keyClick(f.view.viewport(), Qt::Key_Right);
    QCOMPARE(f.circuit.component(r)->pos, QPointF(SchematicScene::kGrid, 300));

    // Alt+click highlights the whole node, wires joined only by labels included
    const ItemId w1 = f.circuit.addWire({0, {{-300, 100}, {-200, 100}}});
    const ItemId w2 = f.circuit.addWire({0, {{100, 100}, {200, 100}}});
    place(f.circuit, QStringLiteral("netlabel"), {-200, 100}, {}, QStringLiteral("N"));
    place(f.circuit, QStringLiteral("netlabel"), {100, 100}, {}, QStringLiteral("N"));
    f.scene.refreshNetlist();
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, Qt::AltModifier, f.at({-250, 100}));
    QVERIFY(f.scene.highlightedNet() >= 0);
    QCOMPARE(f.scene.netlist().wireNet.value(w1), f.scene.highlightedNet());
    QCOMPARE(f.scene.netlist().wireNet.value(w2), f.scene.highlightedNet());
}

void TestGui::ledColourProperty_data()
{
    QTest::addColumn<QString>("symbol");
    QTest::newRow("LED") << "led";
    QTest::newRow("Logic Display") << "logicdisplay";
}

void TestGui::ledColourProperty()
{
    QFETCH(QString, symbol);
    Component led;
    led.symbolId = symbol;
    led.designator = QStringLiteral("D1");
    PropertiesDialog dlg(led, *library().find(symbol));
    auto* combo = dlg.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("red"));      // default
    combo->setCurrentIndex(combo->findData(QStringLiteral("green")));
    QCOMPARE(dlg.result().props.value(QStringLiteral("color")).toString(), QStringLiteral("green"));
}

// CM_SCREENSHOT=<png> [CM_SCREENSHOT_FILE=<circuit>] saves the main window.
void TestGui::screenshot()
{
    const QString out = qEnvironmentVariable("CM_SCREENSHOT");
    if (out.isEmpty())
        QSKIP("set CM_SCREENSHOT to capture the main window");
    MainWindow w;
    w.resize(1600, 1000);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    if (const QString file = qEnvironmentVariable("CM_SCREENSHOT_FILE"); !file.isEmpty())
        QVERIFY(w.openFile(file, false));
    QTest::qWait(200);
    if (qEnvironmentVariableIsSet("CM_SCREENSHOT_RUN")) {
        w.runAction()->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!w.analogRunning(), 20000);
        QTest::qWait(300);
    }
    QVERIFY(w.grab().save(out));
}

void TestGui::patternEditorFills()
{
    using P = PatternEditor::Pattern;
    QVector<int> w(8, 0);
    PatternEditor::fill(w, P::CountUp, 0, 7, 1, 0xff);
    QCOMPARE(w, (QVector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    // one column counting up alternates; other columns keep their bits
    w.fill(0x01);
    PatternEditor::fill(w, P::CountUp, 0, 3, 1, 0x08);
    QCOMPARE(w, (QVector<int>{0x01, 0x09, 0x01, 0x09, 0x01, 0x01, 0x01, 0x01}));
    w.fill(0);
    PatternEditor::fill(w, P::Shift1Left, 0, 7, 2, 0x0f);
    QCOMPARE(w, (QVector<int>{1, 1, 2, 2, 4, 4, 8, 8}));
    PatternEditor::fill(w, P::Shift0Right, 0, 3, 1, 0xf0);
    QCOMPARE(w.mid(0, 4), (QVector<int>{0x71, 0xb1, 0xd2, 0xe2}));
    PatternEditor::fill(w, P::CountDown, 0, 2, 1, 0xff);
    QCOMPARE(w.mid(0, 3), (QVector<int>{0xff, 0xfe, 0xfd}));
}

void TestGui::dataSequencerDialogKeepsWords()
{
    Component c;
    c.symbolId = QStringLiteral("dataseq");
    c.props = {{QStringLiteral("data"), QStringLiteral("01 02 04")}, {QStringLiteral("lines"), 16},
               {QStringLiteral("stop"), 2}};
    DataSequencerDialog dlg(c, *library().find(c.symbolId));
    QCOMPARE(dlg.words().size(), 16);
    const Component r = dlg.result();
    QCOMPARE(r.props.value(QStringLiteral("data")).toString(), QStringLiteral("01 02 04"));
    QCOMPARE(r.props.value(QStringLiteral("lines")).toInt(), 16);
    QCOMPARE(r.props.value(QStringLiteral("stop")).toInt(), 2);
}

void TestGui::memoryEditorText()
{
    const QVector<int> w = MemoryEditorDialog::fromText(QStringLiteral("1 ab,FF;100"), 8);
    QCOMPARE(w, (QVector<int>{0x01, 0xab, 0xff, 0x00, 0, 0, 0, 0}));
    QCOMPARE(MemoryEditorDialog::toText(w), QStringLiteral("01 AB FF"));
    QCOMPARE(MemoryEditorDialog::fromText(QStringLiteral("1 2 3"), 2).size(), 2);
    MemoryEditorDialog dlg(QStringLiteral("RAM"), w);
    QCOMPARE(dlg.words(), w);
}

void TestGui::contextMenuSelectsItem()
{
    ViewFixture f;
    const ItemId other = place(f.circuit, QStringLiteral("and2"), {-200, 0});
    const ItemId id = place(f.circuit, QStringLiteral("and2"), {100, 0});
    for (QGraphicsItem* it : f.scene.items())
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it); c && c->id() == other) c->setSelected(true);
    QList<QGraphicsItem*> hits;
    connect(&f.scene, &SchematicScene::contextMenuRequested, this, [&](QGraphicsItem* it, QPoint) { hits << it; });
    const QPoint p = f.at({100, 0});
    QContextMenuEvent e(QContextMenuEvent::Mouse, p, f.view.viewport()->mapToGlobal(p));
    QApplication::sendEvent(f.view.viewport(), &e);
    QCOMPARE(hits.size(), 1);
    auto* item = qgraphicsitem_cast<ComponentItem*>(hits.first());
    QVERIFY(item && item->id() == id);
    QCOMPARE(f.scene.selectedItems(), (QList<QGraphicsItem*>{item}));   // right click selects only it
}

void TestGui::sceneGrowsWithItems()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    place(circuit, QStringLiteral("and2"), {9000, 6000});
    scene.refreshNetlist();
    QVERIFY(scene.sceneRect().contains(QPointF(9500, 6500)));
}

namespace {

// Pulser (10 ticks high, 10 low) watched by a SCOPE.
ItemId clockWithScope(Circuit& c)
{
    const ItemId p = place(c, QStringLiteral("pulser"), {0, 0}, QStringLiteral("P1"));
    const ItemId scope = place(c, QStringLiteral("scope"), {400, 0}, QStringLiteral("T1"));
    label(c, p, QStringLiteral("Q"), QStringLiteral("CLK"));
    label(c, scope, library().find(QStringLiteral("scope"))->pins[0].name, QStringLiteral("CLK"));
    return scope;
}

} // namespace

void TestGui::breakpointStopsSimulation()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    const ItemId scope = clockWithScope(circuit);
    scene.setBreakCondition(scope, 2);                     // break on 1
    QCOMPARE(scene.breakCondition(scope), 2);
    scene.setTicksPerFrame(15);
    scene.run();
    QTRY_VERIFY(!scene.running());
    const int net = scene.waveformLanes().first().net;
    QCOMPARE(scene.simulator()->netValue(net), sim::Logic::L1);
    const quint64 first = scene.simTime();
    scene.run();
    QTRY_VERIFY(!scene.running());
    QCOMPARE(scene.simTime(), first + 20);                 // the next rising edge
    QCOMPARE(scene.simulator()->netValue(net), sim::Logic::L1);
    scene.stopSimulation();
    QCOMPARE(circuit.component(scope)->props.value(QStringLiteral("break")).toInt(), 2);

    // Level-Or on 0: stops when the level starts matching, not again while it holds
    AnalysisSettings a = circuit.analysis();
    a.breakEdge = false;
    circuit.setAnalysis(a);
    scene.setBreakCondition(scope, 1);
    scene.run();
    QTRY_VERIFY(!scene.running());
    QCOMPARE(scene.simulator()->netValue(net), sim::Logic::L0);
    const quint64 low = scene.simTime();
    scene.run();
    QTRY_VERIFY(!scene.running());
    QVERIFY(scene.simTime() > low + 10);                   // went high, then low again
    QCOMPARE(scene.simulator()->netValue(net), sim::Logic::L0);
    scene.stopSimulation();
}

void TestGui::mergePlacesBelow()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    place(circuit, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    Circuit other;
    place(other, QStringLiteral("or2"), {500, -300}, QStringLiteral("U1"));
    scene.mergeCircuit(other);
    QCOMPARE(circuit.components().size(), 2);
    const Component& merged = circuit.components().last();
    QCOMPARE(merged.designator, QStringLiteral("U2"));
    QVERIFY(merged.pos.y() > 40);
    QCOMPARE(scene.selection().components.size(), 1);
    undo.undo();
    QCOMPARE(circuit.components().size(), 1);
}

void TestGui::probeToolSurvivesRun()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    clockWithScope(circuit);
    scene.setTool(SchematicScene::Tool::Probe);
    scene.run();
    QCOMPARE(scene.tool(), SchematicScene::Tool::Probe);
    scene.stopSimulation();
}

void TestGui::faultLockRestrictsScene()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    place(circuit, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    scene.selectAll();
    scene.setRestrictions({QStringLiteral("delete"), QStringLiteral("wire"), QStringLiteral("edit")});
    scene.deleteSelection();
    scene.duplicateSelection();
    QCOMPARE(circuit.components().size(), 1);
    scene.setTool(SchematicScene::Tool::Wire);
    QCOMPARE(scene.tool(), SchematicScene::Tool::Select);
    scene.setRestrictions({});
    scene.deleteSelection();
    QCOMPARE(circuit.components().size(), 0);
}

void TestGui::simulationAppliesFaults()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    const ItemId g = place(circuit, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    Component c = *circuit.component(g);
    c.props = {{QStringLiteral("fault.on"), true}, {QStringLiteral("fault.low"), QStringLiteral("Y")}};
    circuit.updateComponent(c);
    scene.run();
    scene.pause();
    QCOMPARE(scene.simCircuit().components().size(), 3);      // the gate, LOW on its pin and on its node
    scene.stopSimulation();
    QCOMPARE(scene.simCircuit().components().size(), 1);
    QCOMPARE(scene.netlist().nets.size(), NetlistBuilder::build(circuit, library()).nets.size());
}

void TestGui::labelPositionsAndBorder()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    const ItemId g = place(circuit, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    Component c = *circuit.component(g);
    c.designatorAt = QPointF(200, 200);
    circuit.updateComponent(c);
    scene.selectAll();
    scene.resetLabelPositions();
    QVERIFY(!circuit.component(g)->designatorAt);
    undo.undo();
    QVERIFY(circuit.component(g)->designatorAt);

    const QRectF items = scene.contentsRect();
    scene.setBorderVisible(true);
    const QRectF border = scene.borderRect();
    QVERIFY(border.contains(items));
    QCOMPARE(std::fmod(border.width(), SchematicScene::kZone), 0.0);
    QVERIFY(border.width() >= 2 * SchematicScene::kZone);
    QVERIFY(scene.contentsRect().contains(border));            // exported with the drawing
}

void TestGui::signalGenDialog()
{
    const QString file = QStringLiteral("* noise\n+ 0 1 1m 3 dV\n+ 2m 2\nignored 9 9\n");
    QCOMPARE(SignalGenDialog::parsePwlFile(file), QStringLiteral("0 1\n0.001 3\n0.002 2"));
    // 4 V peak to peak around 1 V, stretched to 10 ms
    QCOMPARE(SignalGenDialog::parsePwlFile(file, 4, 1, 10e-3), QStringLiteral("0 -1\n0.005 3\n0.01 1"));

    Component c;
    c.symbolId = QStringLiteral("siggen");
    c.props = {{QStringLiteral("wave"), QStringLiteral("fm")}, {QStringLiteral("carrier"), QStringLiteral("20k")}};
    SignalGenDialog dlg(c, *library().find(c.symbolId));
    const Component r = dlg.result();
    QCOMPARE(r.props.value(QStringLiteral("wave")).toString(), QStringLiteral("fm"));
    QCOMPARE(r.props.value(QStringLiteral("carrier")).toString(), QStringLiteral("20k"));
    QCOMPARE(r.props.value(QStringLiteral("mdi")).toString(), QStringLiteral("1"));
    QVERIFY(!r.props.contains(QStringLiteral("current")));
}

void TestGui::groupItemsAndPlacement()
{
    ViewFixture f;
    const ItemId a = place(f.circuit, QStringLiteral("7400"), {0, 0}, QStringLiteral("U1"));
    const ItemId b = place(f.circuit, QStringLiteral("7400"), {0, 200}, QStringLiteral("U2"));
    const ItemId other = place(f.circuit, QStringLiteral("7404"), {0, 400}, QStringLiteral("U3"));
    f.scene.selectAll();
    QString error;
    QVERIFY(!f.scene.groupSelection(&error));                // a 7404 cannot join a 7400
    QVERIFY(!error.isEmpty());
    f.scene.clearSelection();
    for (QGraphicsItem* it : f.scene.items())
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it); c && c->id() != other) c->setSelected(true);
    QVERIFY(f.scene.groupSelection(&error));
    QCOMPARE(f.circuit.component(b)->designator, QStringLiteral("U1"));
    const SymbolDef& nand = *library().find(QStringLiteral("7400"));
    QCOMPARE(packagePart(nand, *f.circuit.component(a)), 1);
    QCOMPARE(packagePart(nand, *f.circuit.component(b)), 2);

    // a new 7400 fills part C of U1; with A-D taken the next one starts U4
    f.scene.beginPlace(QStringLiteral("7400"));
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({200, 0}));
    f.scene.beginPlace(QStringLiteral("7400"));
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({200, 200}));
    f.scene.beginPlace(QStringLiteral("7400"));
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({-200, 200}));
    f.scene.setTool(SchematicScene::Tool::Select);
    QStringList shown;
    for (const Component& c : f.circuit.components())
        if (c.symbolId == u"7400") shown << shownDesignator(*library().find(c.symbolId), c);
    shown.sort();
    QCOMPARE(shown, (QStringList{"U1A", "U1B", "U1C", "U1D", "U4A"}));
}

void TestGui::themeColours()
{
    const Theme saved = Theme::current();
    Theme t;
    t.wire = Qt::darkGreen;
    t.background = QColor(0x20, 0x20, 0x20);
    t.deviceFont = QStringLiteral("DejaVu Sans Mono");
    Theme::setCurrent(t);
    QCOMPARE(Theme::load(), t);                        // kept in the settings
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    QCOMPARE(scene.wireColor(1), QColor(Qt::darkGreen));
    Preferences p = Preferences::load();
    QCOMPARE(p.theme, t);
    PreferencesDialog dlg(p);
    QCOMPARE(dlg.result().theme.wire, QColor(Qt::darkGreen));
    Theme::setCurrent(saved);
}

void TestGui::deviceProbeCycles()
{
    ViewFixture f;
    const ItemId r = place(f.circuit, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R1"));
    f.scene.setTool(SchematicScene::Tool::Probe);
    QStringList modes;
    for (int i = 0; i < 4; ++i) {
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({0, 0}));
        modes << f.circuit.component(r)->props.value(QStringLiteral("probe")).toString();
    }
    QCOMPARE(modes, (QStringList{"I", "P", "Z", ""}));
    QVERIFY(f.circuit.probes().isEmpty());              // not a net probe
}

void TestGui::analysisWindowTools()
{
    WaveformView w;
    w.resize(600, 300);
    const QVector<double> x{0, 1, 2, 3};
    w.setData(QStringLiteral("t"), QStringLiteral("Time"), QStringLiteral("s"), QStringLiteral("V"), x,
              {{QStringLiteral("a"), {1, 2, 3, 4}, Qt::red}, {QStringLiteral("b"), {1, 1, 1, -1}, Qt::blue}}, false);
    QVERIFY(w.addMathTrace(QStringLiteral("T1-T2")));
    QCOMPARE(w.traces().last().y, (QVector<double>{0, 1, 2, 5}));
    QVERIFY(w.addMathTrace(QStringLiteral(" abs( t2 ) ")));
    QCOMPARE(w.traces().last().y, (QVector<double>{1, 1, 1, 1}));
    QVERIFY(w.addMathTrace(QStringLiteral("T1*2")));
    QCOMPARE(w.traces().last().y, (QVector<double>{2, 4, 6, 8}));
    QString error;
    QVERIFY(!w.addMathTrace(QStringLiteral("T9+1"), &error) && !error.isEmpty());
    QVERIFY(!w.addMathTrace(QStringLiteral("sin(T1)"), &error));
    QCOMPARE(w.traces().size(), 5);

    w.zoomX(2, 1.5);                                    // around the middle
    QCOMPARE(w.xRange(), (std::pair<double, double>{0.75, 2.25}));
    w.fit();
    QCOMPARE(w.xRange(), (std::pair<double, double>{0, 3}));
    w.setLogX(true);
    QVERIFY(!w.logX());                                 // x starts at 0
    w.store();
    w.setCursors(1.0, 3.0);
    QVERIFY(!w.grab().isNull());                        // paints cursors and the reference
}

void TestGui::propertiesVisibility()
{
    Component c;
    c.symbolId = QStringLiteral("resistor");
    c.designator = QStringLiteral("R1");
    const SymbolDef& s = *library().find(c.symbolId);
    PropertiesDialog unchanged(c, s);
    QVERIFY(!unchanged.result().showDesignator && !unchanged.result().showLabel);   // defaults stay implicit
    PropertiesDialog dlg(c, s);
    for (QCheckBox* box : dlg.findChildren<QCheckBox*>())
        if (box->text().contains(QStringLiteral("esignator"))) box->setChecked(false);
    QCOMPARE(dlg.result().showDesignator, std::optional<bool>(false));
}

void TestGui::stepSize()
{
    Circuit circuit;
    QUndoStack undo;
    SchematicScene scene(&circuit, &undo, library());
    place(circuit, QStringLiteral("pulser"), {0, 0}, QStringLiteral("P1"));
    scene.setStepTicks(10);                             // one cycle
    scene.stepSimulation();
    QCOMPARE(scene.simTime(), quint64(10));
    scene.stepSimulation();
    QCOMPARE(scene.simTime(), quint64(20));
    scene.stopSimulation();
}

void TestGui::wireAutoRoutes()
{
    ViewFixture f;
    const ItemId a = place(f.circuit, QStringLiteral("and2"), {-200, 0}, QStringLiteral("U1"));
    const ItemId b = place(f.circuit, QStringLiteral("and2"), {200, 0}, QStringLiteral("U2"));
    const ItemId mid = place(f.circuit, QStringLiteral("and2"), {0, 0}, QStringLiteral("U3"));   // in the way
    const QPointF from = pinPos(f.circuit, a, QStringLiteral("Y")), to = pinPos(f.circuit, b, QStringLiteral("B"));
    f.scene.setTool(SchematicScene::Tool::Wire);
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at(from));
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at(to));
    QCOMPARE(f.circuit.wires().size(), 1);
    const Wire w = f.circuit.wires().first();
    QCOMPARE(w.points.first(), from);
    QCOMPARE(w.points.last(), to);
    f.scene.refreshNetlist();
    const Netlist& nl = f.scene.netlist();
    const SymbolDef& g = *library().find(QStringLiteral("and2"));
    const int net = nl.netOf(a, g.pinIndex(QStringLiteral("Y")));
    QCOMPARE(nl.netOf(b, g.pinIndex(QStringLiteral("B"))), net);
    for (int i = 0; i < g.pins.size(); ++i)
        QVERIFY2(nl.netOf(mid, i) != net, "touches the device in between");
}

void TestGui::macroSymbolLayout()
{
    // a macro with inputs A, B and output Y
    Circuit inner;
    const ItemId g = place(inner, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    for (const auto& [pin, port, in] : {std::tuple{"A", "A", true}, {"B", "B", true}, {"Y", "Y", false}}) {
        const QString id = in ? QStringLiteral("port_in") : QStringLiteral("port_out");
        place(inner, id, pinPos(inner, g, QString::fromLatin1(pin)) - library().find(id)->pins[0].pos, {},
              QString::fromLatin1(port));
    }
    const QString id = macros::registerMacro(QStringLiteral("Sym Test"),
                                             QJsonDocument::fromJson(NativeFormat::serialize(inner)).object());
    QVERIFY(!id.isEmpty());
    MacroSymbolDialog dlg(*library().find(id));
    dlg.movePin(QStringLiteral("B"), 3);                  // B to the bottom
    QVERIFY(macros::setSymbolLayout(id, dlg.layout()));
    const SymbolDef& s = *library().find(id);
    const SymbolPin& b = s.pins[s.pinIndex(QStringLiteral("B"))];
    const SymbolPin& a = s.pins[s.pinIndex(QStringLiteral("A"))];
    QVERIFY2(b.pos.y() > a.pos.y() && std::abs(b.pos.x()) < std::abs(a.pos.x()), "B is not on the bottom");
    QCOMPARE(macros::symbolLayout(s).value(u"bottom").toArray().first().toString(), QStringLiteral("B"));
}

void TestGui::hexKeyTakesKeyboard()
{
    ViewFixture f;
    const ItemId hex = place(f.circuit, QStringLiteral("hexkey"), {0, 0}, QStringLiteral("K1"));
    const ItemId ascii = place(f.circuit, QStringLiteral("asciikey"), {300, 0}, QStringLiteral("K2"));
    f.scene.run();
    f.scene.pause();
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({0, 0}));
    QCOMPARE(f.scene.keyFocus(), hex);
    f.view.setFocus();
    QTest::keyPress(f.view.viewport(), Qt::Key_B, {}, 0);
    QCOMPARE(f.scene.simState(hex), 0xB);
    QTest::keyRelease(f.view.viewport(), Qt::Key_B);
    QTest::keyClick(f.view.viewport(), Qt::Key_7);
    QCOMPARE(f.scene.simState(hex), 7);
    QTest::keyClick(f.view.viewport(), Qt::Key_G);          // not hex: ignored
    QCOMPARE(f.scene.simState(hex), 7);
    QTest::keyClick(f.view.viewport(), Qt::Key_Escape);
    QCOMPARE(f.scene.keyFocus(), ItemId(0));
    QTest::keyClick(f.view.viewport(), Qt::Key_3);
    QCOMPARE(f.scene.simState(hex), 7);                     // no longer listening

    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({300, 0}));
    QCOMPARE(f.scene.keyFocus(), ascii);
    QTest::keyClick(f.view.viewport(), 'A');
    QCOMPARE(f.scene.simState(ascii), int('A'));
    f.scene.stopSimulation();
    QCOMPARE(f.scene.keyFocus(), ItemId(0));
}

// Quick clicks each switch a Logic Switch (the second arrives as a double
// click); once clicked it also switches with Space or Enter.
void TestGui::switchesClickFastAndKeyboard()
{
    ViewFixture f;
    const ItemId s = place(f.circuit, QStringLiteral("logicswitch"), {0, 0}, QStringLiteral("S1"));
    f.scene.run();
    f.scene.pause();
    const int start = f.scene.simState(s);
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({-10, 0}));
    QCOMPARE(f.scene.simState(s), 1 - start);
    // the second click of a quick pair arrives as a double click
    QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, {}, f.at({-10, 0}));
    QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, f.at({-10, 0}));
    QCOMPARE(f.scene.simState(s), start);
    // or after its own press: still one switch
    QTest::mousePress(f.view.viewport(), Qt::LeftButton, {}, f.at({-10, 0}));
    QTest::mouseDClick(f.view.viewport(), Qt::LeftButton, {}, f.at({-10, 0}));
    QTest::mouseRelease(f.view.viewport(), Qt::LeftButton, {}, f.at({-10, 0}));
    QCOMPARE(f.scene.simState(s), 1 - start);
    QCOMPARE(f.scene.keyFocus(), s);
    f.view.setFocus();
    QTest::keyClick(f.view.viewport(), Qt::Key_Space);
    QCOMPARE(f.scene.simState(s), start);
    QTest::keyClick(f.view.viewport(), Qt::Key_Return);
    QCOMPARE(f.scene.simState(s), 1 - start);
    f.scene.stopSimulation();
}

// A part's designator drags on its own, undoably.
void TestGui::dragDesignator()
{
    ViewFixture f;
    const ItemId u = place(f.circuit, QStringLiteral("74LS74"), {0, 0}, QStringLiteral("U1"), QStringLiteral("74LS74"));
    ComponentItem* item = nullptr;
    for (QGraphicsItem* it : f.scene.items())
        if (auto* ci = qgraphicsitem_cast<ComponentItem*>(it); ci && ci->id() == u) item = ci;
    QVERIFY(item);
    const QPointF from = item->pos() + item->textAnchor(0) + QPointF(0, -4);
    QCOMPARE(item->textAt(from), 0);
    f.drag(from, from + QPointF(-60, -40));
    QVERIFY(f.circuit.component(u)->designatorAt.has_value());
    QVERIFY(QLineF(*f.circuit.component(u)->designatorAt, item->textAnchor(0)).length() < 1);
    QCOMPARE(f.circuit.component(u)->pos, QPointF(0, 0));   // the part stays
    f.undo.undo();
    QVERIFY(!f.circuit.component(u)->designatorAt.has_value());
}

// A click on a wire selects it; Delete removes it.
void TestGui::clickSelectsWire()
{
    ViewFixture f;
    const ItemId id = f.circuit.addWire({0, {{-100, 0}, {100, 0}}});
    // with a vertex a reshape would tidy away (imported wires have them)
    const ItemId other = f.circuit.addWire({0, {{-100, 100}, {0, 100}, {100, 100}}});
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({0, 0}));
    QCOMPARE(f.scene.selection().wires.size(), 1);
    QCOMPARE(f.scene.selection().wires.first().id, id);
    // its end too
    QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at({-100, 100}));
    QCOMPARE(f.scene.selection().wires.size(), 1);
    QCOMPARE(f.scene.selection().wires.first().id, other);
    QCOMPARE(f.undo.count(), 0);                             // a click changes nothing
    f.scene.deleteSelection();
    QVERIFY(!f.circuit.wire(other));
    QVERIFY(f.circuit.wire(id));
}

namespace {

// Each device pin with the device pins it connects to: the same before and
// after an edit when connectivity is unchanged.
QStringList connections(const Circuit& c)
{
    const Netlist nl = NetlistBuilder::build(c, library());
    QStringList out;
    for (const Component& comp : c.components()) {
        const SymbolDef* s = library().find(comp.symbolId);
        if (s->netLabel) continue;
        for (int i = 0; i < s->pins.size(); ++i) {
            QStringList peers;
            for (const PinRef& r : nl.nets[nl.netOf(comp.id, i)].pins) {
                const Component* o = c.component(r.component);
                const SymbolDef* os = library().find(o->symbolId);
                if (!os->netLabel && !(r.component == comp.id && r.pin == i))
                    peers << o->designator + u'.' + os->pins[r.pin].name;
            }
            peers.sort();
            out << comp.designator + u'.' + s->pins[i].name + u':' + peers.join(u',');
        }
    }
    out.sort();
    return out;
}

} // namespace

void TestGui::busEntriesFollowBus()
{
    ViewFixture f;
    const ItemId bus = f.circuit.addWire({0, {{0, -100}, {0, 100}}, true, QStringLiteral("D")});
    const ItemId e0 = f.circuit.addWire({0, {{-100, 0}, {0, 0}}});
    const ItemId e1 = f.circuit.addWire({0, {{-100, 50}, {0, 50}}});
    const ItemId l0 = place(f.circuit, QStringLiteral("netlabel"), {0, 0}, {}, QStringLiteral("D0"));
    const ItemId l1 = place(f.circuit, QStringLiteral("netlabel"), {0, 50}, {}, QStringLiteral("D1"));
    const QPointF label0 = f.circuit.component(l0)->pos;

    f.drag({0, -50}, {60, -50});                     // slide the bus sideways
    QCOMPARE(f.circuit.wire(bus)->points.at(1).x(), 60.0);
    QCOMPARE(f.circuit.wire(e0)->points.last(), QPointF(60, 0));
    QCOMPARE(f.circuit.wire(e1)->points.last(), QPointF(60, 50));
    QCOMPARE(f.circuit.wire(e0)->points.first(), QPointF(-100, 0));
    QCOMPARE(f.circuit.component(l0)->pos, label0 + QPointF(60, 0));
    QCOMPARE(f.circuit.component(l1)->pos.x(), label0.x() + 60);
    f.undo.undo();                                   // one step brings everything back
    QCOMPARE(f.circuit.wire(e0)->points.last(), QPointF(0, 0));
    QCOMPARE(f.circuit.component(l0)->pos, label0);

    // moved as part of a selection: the bus and a part, dragged by the part
    const ItemId part = place(f.circuit, QStringLiteral("and2"), {-250, -200}, QStringLiteral("U1"));
    for (QGraphicsItem* it : f.scene.items()) {
        if (auto* w = qgraphicsitem_cast<WireItem*>(it); w && w->id() == bus) w->setSelected(true);
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it); c && c->id() == part) {
            c->setSelected(true);
        }
    }
    const QPointF grab((pinPos(f.circuit, part, QStringLiteral("A")).x() + pinPos(f.circuit, part, QStringLiteral("Y")).x()) / 2,
                       pinPos(f.circuit, part, QStringLiteral("Y")).y());
    f.drag(grab, grab + QPointF(40, 20));
    QCOMPARE(f.circuit.wire(bus)->points.first(), QPointF(40, -80));
    QCOMPARE(f.circuit.wire(e0)->points.last(), QPointF(40, 20));
    QCOMPARE(f.circuit.wire(e0)->points.first(), QPointF(-100, 0));
    QCOMPARE(f.circuit.component(l0)->pos, label0 + QPointF(40, 20));
}

void TestGui::draggedBusKeepsConnections()
{
    const QString file = QDir::homePath() + QStringLiteral("/Documents/github/_calliari_/tecdigi/CircuitMaker/Memoria_4k8_v3.CKT");
    if (!QFile::exists(file))
        QSKIP("course circuit not available");
    ViewFixture f;
    QVERIFY(CktImporter::import(file, f.circuit, library(), nullptr, nullptr));
    f.view.setSceneRect(f.scene.itemsBoundingRect());
    const QStringList before = connections(f.circuit);
    ItemId dados = 0;
    for (const Wire& w : f.circuit.wires())
        if (w.bus && w.name == u"DADOS") dados = w.id;
    QVERIFY(dados);
    const QVector<QPointF> line = f.circuit.wire(dados)->points;
    QVector<ItemId> entries;
    for (const Wire& w : f.circuit.wires())
        if (!w.bus && (WireGeometry::onPolyline(w.points.first(), line) || WireGeometry::onPolyline(w.points.last(), line)))
            entries << w.id;
    QVERIFY(entries.size() >= 16);
    const QPointF mid = (line[0] + line[1]) / 2;
    f.view.centerOn(mid);
    f.drag(mid, mid + QPointF(-60, 0));
    const QVector<QPointF> moved = f.circuit.wire(dados)->points;
    QVERIFY(moved != line);
    for (ItemId id : std::as_const(entries)) {
        const Wire* w = f.circuit.wire(id);
        QVERIFY2(WireGeometry::onPolyline(w->points.first(), moved) || WireGeometry::onPolyline(w->points.last(), moved),
                 "an entry was left off the bus");
    }
    QCOMPARE(connections(f.circuit), before);
}

QTEST_MAIN(TestGui)
#include "tst_gui.moc"

void TestGui::snapOffAndConnectionArea()
{
    ViewFixture f;
    SchematicScene::setSnapToGrid(false);
    // off the grid, a wire end goes where it is dropped...
    const ItemId free = f.circuit.addWire({0, {{-100, 0}, {100, 0}}});
    f.drag({100, 0}, {153, 0});
    QCOMPARE(f.circuit.wire(free)->points.last(), QPointF(153, 0));
    // ...unless a pin is within the connection area: then it lands on it
    place(f.circuit, QStringLiteral("resistor"), {-160, -200}, QStringLiteral("R1"));     // pin 2 at (-100, -200)
    const ItemId toPin = f.circuit.addWire({0, {{100, -200}, {0, -200}}});
    f.drag({0, -200}, {-102, -198});
    QCOMPARE(f.circuit.wire(toPin)->points.last(), QPointF(-100, -200));
    SchematicScene::setSnapToGrid(true);
    QCOMPARE(SchematicScene::snap({153, 7}), QPointF(150, 10));
}
