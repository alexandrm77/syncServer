#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTcpSocket>
#include <QDebug>
#include "SyncServer.h"
#include "DiscoveryResponder.h"
#include "DiscoveryClient.h"
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
            qCritical() << "Failed to listen on port 8080:";
            return 1;
        }

        auto responder = new DiscoveryResponder(&a);

    } else if (mode == "client") {
        qDebug() << "Running in CLIENT mode";

        auto client = new DiscoveryClient(&a);

        QObject::connect(client, &DiscoveryClient::serverDiscovered, [=](const QHostAddress &serverAddr) {
            qDebug() << "Discovered server at" << serverAddr.toString();

            auto socket = new QTcpSocket(); // родитель — QCoreApplication, чтобы не удалить раньше времени

            QObject::connect(socket, &QTcpSocket::connected, [=]() {
                qDebug() << "GET /register ...";
                QByteArray req = "GET /register HTTP/1.1\r\nHost: server\r\nConnection: close\r\n\r\n";
                socket->write(req);
            });

            QObject::connect(socket, &QTcpSocket::readyRead, [=]() {
                QByteArray response = socket->readAll();
                qDebug() << "Response:\n" << response;

                // Запускаем SyncService после успешной регистрации
                auto syncService = new SyncService(serverAddr, 8080, qApp);
                syncService->start();
            });

            QObject::connect(socket,
                static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
                [socket](QAbstractSocket::SocketError err) {
                    if (err != QAbstractSocket::RemoteHostClosedError) {
                        qWarning() << "Socket error:" << err << socket->errorString();
                    }
                }
            );

            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

            socket->connectToHost(serverAddr, 8080);
        });

        client->startDiscovery();

    } else {
        qCritical() << "Specify --mode server or --mode client";
        return 1;
    }

    return a.exec();
}
