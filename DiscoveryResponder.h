#pragma once

#include <QObject>

class QUdpSocket;
class DiscoveryResponder : public QObject
{
    Q_OBJECT
public:
    explicit DiscoveryResponder(QObject *parent = nullptr);

private slots:
    void handleDatagram();

private:
    QUdpSocket *m_socket;
};
