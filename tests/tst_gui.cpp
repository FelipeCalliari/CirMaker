// Widget-level tests on the offscreen platform: scene interaction paths that
// crashed or need the real event flow.

#include "TestUtil.h"

#include "ui/ComponentItem.h"
#include "ui/MainWindow.h"
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

QTEST_MAIN(TestGui)
#include "tst_gui.moc"
