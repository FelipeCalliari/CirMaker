// Widget-level tests on the offscreen platform: scene interaction paths that
// crashed or need the real event flow.

#include "TestUtil.h"

#include "ui/ComponentItem.h"
#include "ui/MainWindow.h"
#include "ui/SchematicScene.h"
#include "ui/WireItem.h"

#include <QGraphicsView>
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
