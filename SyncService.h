#pragma once

#include <QObject>
#include <QTimer>
#include <QHostAddress>

class SyncService : public QObject
{
    Q_OBJECT
public:
    explicit SyncService(const QHostAddress &serverAddress, quint16 serverPort, QObject *parent = nullptr);
    void start();

private:
    QHostAddress m_serverAddress;
    quint16 m_serverPort;
    QTimer m_pingTimer;

    void sendPing();
};
