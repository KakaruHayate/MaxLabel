// MaxLabel GUI entry point.

#include "MainWindow.h"

#include <QApplication>

int main(int argc, char ** argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MaxLabel"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("MaxLabel"));

    MainWindow window;
    window.show();
    return QApplication::exec();
}
