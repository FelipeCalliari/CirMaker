#include "library/LibraryRegistry.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CirMaker Qt"));
    QApplication::setOrganizationName(QStringLiteral("cirmaker-qt"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    QTranslator qtTranslator, appTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    if (appTranslator.load(QLocale(), QStringLiteral("cirmaker"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&appTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "Schematic editor and digital simulator"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QApplication::translate("main", "Circuit to open (.cmq or .ckt)"));
    const QCommandLineOption exportOption(QStringLiteral("export"),
        QApplication::translate("main", "Render the opened circuit to <image> (.png, .svg, .pdf) and exit."),
        QStringLiteral("image"));
    const QCommandLineOption sheetOption(QStringLiteral("library-sheet"),
        QApplication::translate("main", "Render every library symbol to <image> and exit."), QStringLiteral("image"));
    parser.addOptions({exportOption, sheetOption});
    parser.process(app);

    cm::LibraryRegistry::instance().loadBuiltin();

    cm::MainWindow window;
    if (parser.isSet(sheetOption)) {
        window.showLibrarySheet();
        return window.exportTo(parser.value(sheetOption)) ? 0 : 1;
    }
    if (!parser.positionalArguments().isEmpty() && !window.openFile(parser.positionalArguments().first(), !parser.isSet(exportOption)))
        return parser.isSet(exportOption) ? 1 : app.exec();
    if (parser.isSet(exportOption))
        return window.exportTo(parser.value(exportOption)) ? 0 : 1;
    window.show();
    return app.exec();
}
