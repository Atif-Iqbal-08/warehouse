QT += widgets sql printsupport
CONFIG += c++17

TEMPLATE = app
TARGET = warehouse_sku_generator
win32:RC_ICONS += Assets/app_icon.ico

SOURCES += \
    src/qrcodegen.cpp \
    src/printsettingsdialog.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/mainwindow_history.cpp

HEADERS += \
    src/qrcodegen.hpp \
    src/printsettingsdialog.h \
    src/mainwindow.h \
    src/globals.h \
    src/app_settings.h

FORMS += \
    src/mainwindow.ui

RESOURCES += \
    src/assets.qrc

win32:LIBS += -lcrypt32

