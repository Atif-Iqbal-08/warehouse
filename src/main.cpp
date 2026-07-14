#include <QApplication>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QStyleHints>
#include "globals.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // The app ships a single fixed dark theme. Pin the color scheme so a
    // Windows light/dark theme switch can never repaint palette-driven
    // backgrounds (dialogs, scroll areas) white under our light text.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    QApplication::setStyle("Fusion");
    // Configure shared metadata before any settings/database paths are resolved.
    QCoreApplication::setOrganizationName(AppGlobals::organizationName());
    QCoreApplication::setApplicationName(AppGlobals::appName());
    QCoreApplication::setApplicationVersion(AppGlobals::appVersion());
    QApplication::setQuitOnLastWindowClosed(false);
    MainWindow window;

    // On small/high-DPI-scaled displays (e.g. 14" laptops at 150% scaling) a fixed
    // 1200x700 window can exceed the available desktop area, pushing controls off
    // screen. Start maximized whenever the preferred size wouldn't comfortably fit.
    const QScreen *screen = window.screen() ? window.screen() : QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1200, 700);
    if (avail.width() < 1240 || avail.height() < 740) {
        window.showMaximized();
    } else {
        window.resize(1200, 700);
        window.show();
    }
    return app.exec();
}
