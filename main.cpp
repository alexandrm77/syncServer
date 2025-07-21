#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTcpSocket>
#include <QDebug>
#include "SyncServer.h"
//#include "DiscoveryResponder.h"
//#include "DiscoveryClient.h"
#include "SyncService.h"

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

    if (mode == "server") {
        qDebug() << "Running in SERVER mode";
        auto server = new SyncServer(&a);
        if (!server->listen(QHostAddress::AnyIPv4, 8080)) {
            qCritical() << "Failed to listen on port 8080";
            return 1;
        }
    } else if (mode == "client") {
        qDebug() << "Running in CLIENT mode";
        SyncService::discoverAndStart(&a);
    } else {
        qCritical() << "Specify --mode server or --mode client";
        return 1;
    }

    return a.exec();
}
