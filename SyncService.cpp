#include "SyncService.h"
#include <QTcpSocket>
#include <QDebug>

SyncService::SyncService(const QHostAddress &serverAddress,
                         quint16 serverPort,
                         QObject *parent)
    : QObject(parent), m_serverAddress(serverAddress), m_serverPort(serverPort)
{
    m_pingTimer.setInterval(30 * 1000); // 30 секунд
    connect(&m_pingTimer, &QTimer::timeout, this, &SyncService::sendPing);
}

void SyncService::start()
{
    qDebug() << "SyncService started";
    m_pingTimer.start();
    sendPing(); // первый ping сразу
}

void SyncService::sendPing()
{
    QTcpSocket *socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [=]() {
        socket->write("GET /ping HTTP/1.1\r\nHost: sync\r\nConnection: close\r\n\r\n");
    });
    connect(socket, &QTcpSocket::readyRead, this, [=]() {
        QByteArray response = socket->readAll(); // можно игнорировать
        qDebug() << "Response:\n" << response;
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    socket->connectToHost(m_serverAddress, m_serverPort);
}
