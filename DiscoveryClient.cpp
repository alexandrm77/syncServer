#include "DiscoveryClient.h"
#include <QTimer>
#include <QUdpSocket>
#include <QDebug>

DiscoveryClient::DiscoveryClient(QObject *parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);
    connect(m_socket, &QUdpSocket::readyRead, this, &DiscoveryClient::handleResponse);
}

void DiscoveryClient::startDiscovery()
{
    QByteArray message = "DISCOVER_REQUEST";
    m_socket->writeDatagram(message, QHostAddress::Broadcast, 45454);
    qDebug() << "Broadcasted DISCOVER_REQUEST";
}

void DiscoveryClient::handleResponse()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(m_socket->pendingDatagramSize());

        QHostAddress sender;
        quint16 senderPort;

        m_socket->readDatagram(buffer.data(), buffer.size(), &sender, &senderPort);

        if (buffer == "DISCOVER_RESPONSE") {
            qDebug() << "Received DISCOVER_RESPONSE from" << sender.toString();
            emit serverDiscovered(sender);
        }
    }
}
