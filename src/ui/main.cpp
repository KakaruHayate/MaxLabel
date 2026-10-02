// MaxLabel GUI entry point.
//
//   MaxLabel <folder> [--vocab <symbols.txt>] [--g2p <config.json> --dicts <dir>]
//
// The two optional inputs are what turn the pronunciation dialog from "type
// the phonemes" into "pick one of the dictionary's answers, and be told when a
// phoneme the model cannot resolve slips in".

#include "MainWindow.h"

#include "Resources.h"

#include "maxlabel/models.h"

#include <QApplication>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QByteArray>
#include <QStringList>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

int main(int argc, char ** argv) {
    maxlabel::ui::init_resources();

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MaxLabel"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("MaxLabel"));

    // The interface follows the system language.  English is the source
    // language, so there is nothing to install for it — the strings in the
    // code are what it shows.
    // --lang overrides the system language, which is also how the translations
    // get exercised from one machine.
    QLocale locale = QLocale::system();
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--lang") == 0) locale = QLocale(QString::fromUtf8(argv[i + 1]));
    }
    auto * translator = new QTranslator(&application);
    if (translator->load(locale, QStringLiteral("maxlabel"), QStringLiteral("_"),
                         QStringLiteral(":/i18n"))) {
        application.installTranslator(translator);
    }
    // Qt's own strings — the file dialog, the standard buttons — come from its
    // own catalogue, which has to be installed too or the app is half
    // translated.
    auto * qtTranslator = new QTranslator(&application);
    if (qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        application.installTranslator(qtTranslator);
    }

    // Fusion is the base style the theme is written against: the native
    // Windows style draws its own button chrome and ignores parts of a
    // stylesheet, which is how a themed app ends up half-themed.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFile theme(QStringLiteral(":/theme.qss"));
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    // A release ships its data files beside the binary; a development build
    // keeps the compiled-in default, which is the source tree.
    maxlabel::use_bundled_models(argc > 0 ? argv[0] : "");

    const QStringList arguments = QApplication::arguments();
    QString directory;
    QString vocabulary;
    QString g2pConfig;
    QString g2pDir;
    QString dictionaries;
    QString screenshot;
    bool spectrum = false;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString & argument = arguments.at(i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == QLatin1String("--vocab") && has_value) {
            vocabulary = arguments.at(++i);
        } else if (argument == QLatin1String("--g2p-dir") && has_value) {
            g2pDir = arguments.at(++i);
        } else if (argument == QLatin1String("--g2p") && has_value) {
            g2pConfig = arguments.at(++i);
        } else if (argument == QLatin1String("--dicts") && has_value) {
            dictionaries = arguments.at(++i);
        } else if (argument == QLatin1String("--models") && has_value) {
            maxlabel::set_model_directory(arguments.at(++i).toStdString());
        } else if (argument == QLatin1String("--screenshot") && has_value) {
            screenshot = arguments.at(++i);
        } else if (argument == QLatin1String("--spectrum")) {
            spectrum = true;
        } else if (directory.isEmpty() && !argument.startsWith(QLatin1String("--"))) {
            directory = argument;
        }
    }

    MainWindow window;
    if (!vocabulary.isEmpty()) window.loadVocabulary(vocabulary);
    if (!g2pDir.isEmpty()) window.loadG2PDirectory(g2pDir);
    if (!g2pConfig.isEmpty()) window.loadG2P(g2pConfig, dictionaries);
    if (!directory.isEmpty()) window.loadDirectory(directory);
    if (spectrum) window.setSpectrumMode(true);
    window.show();

    if (!screenshot.isEmpty()) {
        // Render the window to a file and exit.  Layout is easier to judge
        // from a picture than from a description, and this is how the theme
        // gets looked at while it is being written.
        QTimer::singleShot(700, &application, [&window, screenshot]() {
            window.grab().save(screenshot);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
