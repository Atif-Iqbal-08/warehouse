#include <QApplication>
#include "globals.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(AppGlobals::organizationName());
    QCoreApplication::setApplicationName(AppGlobals::appName());
    QCoreApplication::setApplicationVersion(AppGlobals::appVersion());
    QApplication::setQuitOnLastWindowClosed(false);
    MainWindow window;
    window.resize(1200, 700);
    window.show();
    return app.exec();
}
