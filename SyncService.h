#pragma once

#include <QObject>
#include <QTimer>
#include <QHostAddress>
#include <QTcpServer>
#include "FileEntry.h"

class QTcpSocket;
class FileMonitor;
class SyncService : public QObject
{
    Q_OBJECT
public:
    explicit SyncService(const QHostAddress &serverAddress, quint16 serverPort, QObject *parent = nullptr);
    void start();
    static void discoverAndStart(QObject *parent);

private slots:
    void handleNewConnection();
    void handleSocketError(QAbstractSocket::SocketError err);
    void onPingSocketError(QAbstractSocket::SocketError socketError);

signals:
    void connectionLost();

private:
    QHostAddress m_serverAddress;
    quint16 m_serverPort;
    QTimer m_pingTimer;
    QStringList m_syncDirectories;
    FileMonitor *m_monitor = nullptr;
    QTcpServer m_server;
    QSet<QString> m_ignoreNextChange;

    void sendPing();
    void sendSyncListToServer(const QList<FileEntry> &files);
    void uploadFile(const FileEntry &entry);
    void getFile(const QString &relativePath);
    void handleNotify(QTcpSocket *socket, const QByteArray &body);
    void sendDeleteRequest(const QString &relativePath);
    void synchronizeWithServer();
    QList<FileEntry> scanLocalDirectories();
    QVector<FileDiff> parseDiffs(const QByteArray& response);
    void onResponse(const QVector<FileDiff> &diffs);
    QString resolveFullPath(const QString &relativePath) const;
};
