// Widget-level tests on the offscreen platform: scene interaction paths that
// crashed or need the real event flow.

#include "TestUtil.h"

#include "ui/ComponentItem.h"
#include "ui/MainWindow.h"
#include "ui/PropertiesDialog.h"
#include "ui/DataSequencerDialog.h"

#include <QComboBox>
#include "ui/SchematicScene.h"
#include "ui/WireItem.h"

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
    void simulationStress();
    void buttonsAnimate_data();
    void buttonsAnimate();
    void circuitMakerShortcuts();
    void editMenuTools();
    void ledColourProperty_data();
    void ledColourProperty();
    void patternEditorFills();
    void dataSequencerDialogKeepsWords();
    void screenshot();
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

    // an end segment keeps its end in place and grows an elbow
    const ItemId straight = f.circuit.addWire({0, {{-100, -200}, {100, -200}}});
    f.drag({0, -200}, {0, -150});
    QCOMPARE(f.circuit.wire(straight)->points,
             QVector<QPointF>({{-100, -200}, {-100, -150}, {100, -150}, {100, -200}}));
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

QTEST_MAIN(TestGui)
#include "tst_gui.moc"
