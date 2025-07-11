QT += core network
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

SOURCES += main.cpp \
           DiscoveryClient.cpp \
           DiscoveryResponder.cpp \
  FileMonitor.cpp \
           SyncServer.cpp \
           SyncService.cpp \

HEADERS += \
           DiscoveryClient.h \
           DiscoveryResponder.h \
  FileEntry.h \
  FileMonitor.h \
           SyncServer.h \
           SyncService.h \
