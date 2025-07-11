#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHash>
#include <QDateTime>
#include <QHostAddress>
#include <QTimer>
#include <functional>

class QTcpSocket;
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

    void handleClient(QTcpSocket *clientSocket);
    void handleClientRequest(QTcpSocket *socket, const QByteArray &data);
    void handleRegisterRequest(const QHostAddress &addr);
    void handleSyncListRequest(QTcpSocket *socket);
    void handleDownloadRequest(QTcpSocket *socket, const QString &fileName);
    void handleUploadRequest(QTcpSocket *socket, const QByteArray &rawRequest);
    void fetchFromRemote(const QString &path, std::function<void(QByteArray)> callback);
};
