#include "DiscoveryResponder.h"
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QDebug>

DiscoveryResponder::DiscoveryResponder(QObject *parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(45454, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "Failed to bind UDP socket";
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &DiscoveryResponder::handleDatagram);
}

void DiscoveryResponder::handleDatagram()
{
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        if (datagram.data() == "DISCOVER_REQUEST") {
            qDebug() << "Received DISCOVER_REQUEST from" << datagram.senderAddress();
            m_socket->writeDatagram("DISCOVER_RESPONSE", datagram.senderAddress(), datagram.senderPort());
        }
    }
}
