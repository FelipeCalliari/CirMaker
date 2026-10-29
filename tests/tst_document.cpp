#include "TestUtil.h"

#include "core/Bom.h"
#include "core/UndoCommands.h"
#include "core/io/NativeFormat.h"

#include <QUndoStack>

using namespace cm;
using namespace cm::test;

class TestDocument : public QObject {
    Q_OBJECT
private slots:
    void bomGroupsAndSorts();
    void bomCsvQuotes();
    void titleBlockRoundTrip();
    void titleBlockUndo();
};

void TestDocument::bomGroupsAndSorts()
{
    Circuit c;
    for (const char* ref : {"U10", "U2", "U1"})
        place(c, QStringLiteral("7408"), {0, 0}, QString::fromLatin1(ref), QStringLiteral("7408"));
    place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R1"), QStringLiteral("10k"));
    place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R2"), QStringLiteral("1k"));
    place(c, QStringLiteral("ground"), {0, 0}, QStringLiteral("GND1"));
    place(c, QStringLiteral("terminal"), {0, 0}, QStringLiteral("T1"), QStringLiteral("CLK"));
    place(c, QStringLiteral("netlabel"), {0, 0}, {}, QStringLiteral("D0"));

    const QVector<BomLine> bom = Bom::build(c, library());
    QCOMPARE(bom.size(), 3);                                  // power, terminal and label are not parts
    QCOMPARE(bom[0].designators, QStringList({QStringLiteral("R1")}));
    QCOMPARE(bom[2].quantity, 3);
    QCOMPARE(bom[2].designators, QStringList({QStringLiteral("U1"), QStringLiteral("U2"), QStringLiteral("U10")}));
}

void TestDocument::bomCsvQuotes()
{
    BomLine l;
    l.quantity = 2;
    l.designators = {QStringLiteral("U1"), QStringLiteral("U2")};
    l.device = QStringLiteral("Counter, \"fast\"");
    l.value = QStringLiteral("74163");
    const QString csv = Bom::toCsv({l});
    QCOMPARE(csv.section(u'\n', 1, 1), QStringLiteral("2,U1 U2,\"Counter, \"\"fast\"\"\",74163,"));
}

void TestDocument::titleBlockRoundTrip()
{
    Circuit c;
    TitleBlock tb;
    tb.title = QStringLiteral("Contador síncrono");
    tb.author = QStringLiteral("Calliari");
    tb.date = QStringLiteral("29/10/2026");
    tb.revision = QStringLiteral("B");
    tb.visible = true;
    tb.pos = {500, 300};
    c.setTitleBlock(tb);
    Circuit loaded;
    QVERIFY(NativeFormat::deserialize(NativeFormat::serialize(c), loaded, nullptr));
    QVERIFY(loaded.titleBlock() == tb);
}

void TestDocument::titleBlockUndo()
{
    Circuit c;
    QUndoStack undo;
    TitleBlock tb;
    tb.title = QStringLiteral("A");
    tb.visible = true;
    undo.push(new SetTitleBlockCommand(&c, tb, {}));
    QCOMPARE(c.titleBlock().title, QStringLiteral("A"));
    undo.undo();
    QVERIFY(!c.titleBlock().visible);
    QVERIFY(c.titleBlock().title.isEmpty());
}

QTEST_GUILESS_MAIN(TestDocument)
#include "tst_document.moc"
