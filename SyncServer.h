#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHash>
#include <QDateTime>
#include <QHostAddress>
#include <QTimer>
#include <functional>
#include "FileEntry.h"

class QTcpSocket;
class FileMonitor;
class SyncServer : public QObject
{
    Q_OBJECT
public:
    explicit SyncServer(QObject *parent = nullptr);
    bool listen(const QHostAddress &address, quint16 port);
    void stop();

signals:
    void serverStarted();
    void serverStopped();

private slots:
    void handleNewConnection();
    void handleClientReadyRead();
    void handleClientDisconnected();
    void cleanupInactiveClients();

private:
    QTcpServer m_server;
    QHash<QString, QDateTime> m_fileVersions;
    QHash<QString, QDateTime> m_registeredClients;
    QTimer m_cleanupTimer;

    QHash<QTcpSocket*, QByteArray> m_clientBuffers;
    FileMonitor *m_monitor = nullptr;
    // актуальное состояние файлов сервера
    QHash<QString, FileEntry> m_fileEntries;
    QString m_syncDirectory;

    void handleClient(QTcpSocket *clientSocket);
    void handleClientRequest(QTcpSocket *socket, const QByteArray &data,
                             const QMap<QString, QString>& headers,
                             const QByteArray &body,
                             const QByteArray& path);
    void handleRegisterRequest(const QHostAddress &addr);
    void handleSyncList(QTcpSocket *socket, const QByteArray &body);
    void handleDownloadRequest(QTcpSocket *socket, const QString &fileName);
    void handleDownload(QTcpSocket *socket, const QString &relativePath);
    void handleDelete(QTcpSocket *socket, const QMap<QString, QString> &headers);
    void handleUpload(QTcpSocket *socket, const QMap<QString, QString> &headers, const QByteArray &body);
    void fetchFromRemote(const QString &path, std::function<void(QByteArray)> callback);
    void sendHttpResponse(QTcpSocket *socket, int code, const QString &status, const QString &body);
    void notifyUpdate(const QString &relativePath, bool deleted = false);
};
