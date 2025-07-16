QT += core network

CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app

TARGET = SyncServer

INCLUDEPATH += $$PWD

SOURCES += \
    main.cpp \
    DiscoveryClient.cpp \
    DiscoveryResponder.cpp \
    FileMonitor.cpp \
    SyncServer.cpp \
    SyncService.cpp

HEADERS += \
    DiscoveryClient.h \
    DiscoveryResponder.h \
    FileEntry.h \
    FileMonitor.h \
    SyncServer.h \
    SyncService.h

#target.path = /usr/bin
#INSTALLS += target

DISTFILES += rpm/SyncServer.spec \
             SyncServer.desktop

#INSTALLS += desktop

#desktop.path = /usr/share/applications
#desktop.files = SyncServer.desktop
