#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QTcpSocket>
#include <QDebug>

#include "SyncController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("SimpleCacheServer");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Cache Server with UDP Discovery");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption modeOption(QStringList() << "m" << "mode",
                                  "Mode to run: server, client or qml",
                                  "mode");
    parser.addOption(modeOption);
    parser.process(app);

    QString mode = parser.value(modeOption).toLower();
    SyncController controller;

    if (mode == "qml") {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("syncController", &controller);
        engine.load(QUrl::fromLocalFile("TestPage.qml"));
        if (engine.rootObjects().isEmpty()) {
            qCritical() << "Failed to load QML interface";
            return 1;
        }
        return app.exec();
    }

    if (mode == "server") {
        controller.switchToMode(SyncController::Mode::Server);
    } else if (mode == "client") {
        controller.switchToMode(SyncController::Mode::Client);
    } else {
        qCritical() << "Specify --mode server or --mode client";
        return 1;
    }

    return app.exec();
}
