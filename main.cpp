#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTcpSocket>
#include <QDebug>
#include "SyncServer.h"
#include "DiscoveryResponder.h"
#include "DiscoveryClient.h"

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

        SyncServer *server = new SyncServer(&a);
        if (!server->listen(QHostAddress::AnyIPv4, 8080)) {
            qCritical() << "Failed to listen on port 8080:";
            return 1;
        }

        auto responder = new DiscoveryResponder(&a);

    } else if (mode == "client") {
        qDebug() << "Running in CLIENT mode";

        DiscoveryClient *client = new DiscoveryClient();

        QObject::connect(client, &DiscoveryClient::serverDiscovered, [=](const QHostAddress &serverAddr) {
            qDebug() << "Discovered server at" << serverAddr.toString();

            QTcpSocket *socket = new QTcpSocket();
            qDebug() << "before register, trying to connect to" << serverAddr.toString();

            QObject::connect(socket, &QTcpSocket::connected, [=]() {
                qDebug() << "GET /register ...";
                QByteArray req = "GET /register HTTP/1.1\r\nHost: server\r\nConnection: close\r\n\r\n";
                socket->write(req);
            });

            QObject::connect(socket, &QTcpSocket::readyRead, [=]() {
                QByteArray response = socket->readAll();
                qDebug() << "Response:\n" << response;
            });

            QObject::connect(socket, &QTcpSocket::errorOccurred, [=](QAbstractSocket::SocketError err) {
                if (err != QAbstractSocket::RemoteHostClosedError) {
                    qWarning() << "Socket error:" << err << socket->errorString();
                }
            });

            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

            socket->connectToHost(serverAddr, 8080);  // ← это запускает асинхронное соединение

        });

        client->startDiscovery();

    } else {
        qCritical() << "Specify --mode server or --mode client";
        return 1;
    }

    return a.exec();
}
