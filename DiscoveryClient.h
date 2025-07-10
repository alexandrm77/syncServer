#pragma once

#include <QObject>
#include <QHostAddress>

class QUdpSocket;
class DiscoveryClient : public QObject
{
    Q_OBJECT
public:
    explicit DiscoveryClient(QObject *parent = nullptr);

    void startDiscovery();

signals:
    void serverDiscovered(const QHostAddress &address);

private:
    QUdpSocket *m_socket;

private slots:
    void handleResponse();
};

