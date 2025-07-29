#include "SyncServiceHelper.h"
#include "SyncService.h"

#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QDebug>

SyncServiceHelper::SyncServiceHelper(QObject *parent)
    : QObject(parent)
{
}

void SyncServiceHelper::start()
{
    auto socket = new QUdpSocket(this);
    socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);

    auto timer = new QTimer(this);
    timer->setInterval(3000); // каждые 3 секунды

    connect(timer, &QTimer::timeout, [socket]() {
        socket->writeDatagram("DISCOVER_REQUEST", QHostAddress::Broadcast, 45454);
        qDebug() << "Broadcasted DISCOVER_REQUEST";
    });

    connect(socket, &QUdpSocket::readyRead, this, [=]() {
        while (socket->hasPendingDatagrams()) {
            QByteArray buffer;
            buffer.resize(socket->pendingDatagramSize());

            QHostAddress sender;
            quint16 port;
            socket->readDatagram(buffer.data(), buffer.size(), &sender, &port);

            if (buffer == "DISCOVER_RESPONSE") {
                qDebug() << "Discovered SyncServer at" << sender.toString();

                timer->stop();
                socket->deleteLater();
                timer->deleteLater();

                auto tcpSocket = new QTcpSocket(this);
                connect(tcpSocket, &QTcpSocket::connected, [tcpSocket]() {
                    QByteArray req = "GET /register HTTP/1.1\r\nHost: server\r\nConnection: close\r\n\r\n";
                    tcpSocket->write(req);
                });

                connect(tcpSocket, &QTcpSocket::readyRead, this, [=]() {
                    QByteArray response = tcpSocket->readAll();
                    qDebug() << "Response:\n" << response;

                    auto service = new SyncService(sender, 8080, this);
                    service->start();
                    emit discovered(service);

                    connect(service, &SyncService::connectionLost, this, [this, service]() {
                        service->deleteLater();
                        qWarning() << "Connection lost. Rediscovering...";
                        this->start();
                    });

                    tcpSocket->disconnectFromHost();
                    tcpSocket->deleteLater();
                });

                tcpSocket->connectToHost(sender, 8080);
            }
        }
    });

    timer->start();
    socket->writeDatagram("DISCOVER_REQUEST", QHostAddress::Broadcast, 45454);
    qDebug() << "Broadcasted initial DISCOVER_REQUEST";
}
