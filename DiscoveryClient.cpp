#include "DiscoveryClient.h"
#include <QTimer>
#include <QNetworkDatagram>
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
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        if (datagram.data() == "DISCOVER_RESPONSE") {
            qDebug() << "Received DISCOVER_RESPONSE from" << datagram.senderAddress();
            emit serverDiscovered(datagram.senderAddress());
        }
    }
}
