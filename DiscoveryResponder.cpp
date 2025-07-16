#include "DiscoveryResponder.h"
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
        QByteArray buffer;
        buffer.resize(m_socket->pendingDatagramSize());

        QHostAddress sender;
        quint16 senderPort;

        m_socket->readDatagram(buffer.data(), buffer.size(), &sender, &senderPort);

        if (buffer == "DISCOVER_REQUEST") {
            qDebug() << "Received DISCOVER_REQUEST from" << sender.toString() << ":" << senderPort;
            m_socket->writeDatagram("DISCOVER_RESPONSE", sender, senderPort);
        }
    }
}
