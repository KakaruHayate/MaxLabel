// MaxLabel GUI entry point.

#include "MainWindow.h"

#include <QApplication>
#include <QStringList>

int main(int argc, char ** argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MaxLabel"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("MaxLabel"));

    MainWindow window;
    // `MaxLabel <folder>` opens straight into a project; without it the user
    // picks one from the toolbar.
    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1) window.loadDirectory(arguments.at(1));
    window.show();
    return QApplication::exec();
}
