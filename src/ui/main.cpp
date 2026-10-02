// MaxLabel GUI entry point.
//
//   MaxLabel <folder> [--vocab <symbols.txt>] [--g2p <config.json> --dicts <dir>]
//
// The two optional inputs are what turn the pronunciation dialog from "type
// the phonemes" into "pick one of the dictionary's answers, and be told when a
// phoneme the model cannot resolve slips in".

#include "MainWindow.h"

#include <QApplication>
#include <QStringList>

int main(int argc, char ** argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MaxLabel"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("MaxLabel"));

    const QStringList arguments = QApplication::arguments();
    QString directory;
    QString vocabulary;
    QString g2pConfig;
    QString dictionaries;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString & argument = arguments.at(i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == QLatin1String("--vocab") && has_value) {
            vocabulary = arguments.at(++i);
        } else if (argument == QLatin1String("--g2p") && has_value) {
            g2pConfig = arguments.at(++i);
        } else if (argument == QLatin1String("--dicts") && has_value) {
            dictionaries = arguments.at(++i);
        } else if (directory.isEmpty() && !argument.startsWith(QLatin1String("--"))) {
            directory = argument;
        }
    }

    MainWindow window;
    if (!vocabulary.isEmpty()) window.loadVocabulary(vocabulary);
    if (!g2pConfig.isEmpty()) window.loadG2P(g2pConfig, dictionaries);
    if (!directory.isEmpty()) window.loadDirectory(directory);
    window.show();
    return QApplication::exec();
}
