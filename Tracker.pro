QT       += core gui widgets

CONFIG += c++11

TARGET = InputMonitor
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

# Link against libevdev
INCLUDEPATH += /usr/include/libevdev-1.0
unix:!macx: LIBS += -levdev
