#pragma once

#include <QObject>
#include <QTimer>
#include <QHostAddress>
#include "FileEntry.h"

class QTcpSocket;
class FileMonitor;
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
    QString m_syncDirectory;
    FileMonitor *m_monitor = nullptr;

    void sendPing();
    void sendSyncListToServer(const QList<FileEntry> &files);
    void uploadFile(const FileEntry &entry);
    void getFile(const QString &relativePath);
    void handleNotify(QTcpSocket *socket, const QByteArray &body);
};
