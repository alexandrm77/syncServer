#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTcpSocket>
#include <QDebug>

#include "SyncController.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName("SimpleCacheServer");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Cache Server with UDP Discovery");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption modeOption(QStringList() << "m" << "mode",
                                  "Mode to run: server or client",
                                  "mode");
    parser.addOption(modeOption);
    parser.process(a);

    QString mode = parser.value(modeOption).toLower();
    SyncController controller;

    if (mode == "server") {
        controller.switchToMode(SyncController::Mode::Server);
    } else if (mode == "client") {
        controller.switchToMode(SyncController::Mode::Client);
    } else {
        qCritical() << "Specify --mode server or --mode client";
        return 1;
    }

    return a.exec();
}
