QT += core network

CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app

TARGET = SyncServer

INCLUDEPATH += $$PWD

SOURCES += \
    SyncController.cpp \
    SyncServiceHelper.cpp \
    main.cpp \
    FileMonitor.cpp \
    SyncServer.cpp \
    SyncService.cpp

HEADERS += \
    FileEntry.h \
    FileMonitor.h \
    SyncController.h \
    SyncServer.h \
    SyncService.h \
    SyncServiceHelper.h

#target.path = /usr/bin
#INSTALLS += target

DISTFILES += rpm/SyncServer.spec \
             SyncServer.desktop

#INSTALLS += desktop

#desktop.path = /usr/share/applications
#desktop.files = SyncServer.desktop
