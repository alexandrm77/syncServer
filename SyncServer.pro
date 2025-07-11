QT += core network
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

SOURCES += main.cpp \
           DiscoveryClient.cpp \
           DiscoveryResponder.cpp \
           SyncServer.cpp \
           SyncService.cpp \

HEADERS += \
           DiscoveryClient.h \
           DiscoveryResponder.h \
           SyncServer.h \
           SyncService.h \
