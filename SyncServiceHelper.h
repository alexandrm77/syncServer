#pragma once

#include <QObject>
#include <QHostAddress>

class SyncService;

class SyncServiceHelper : public QObject
{
    Q_OBJECT
public:
    explicit SyncServiceHelper(QObject *parent = nullptr);
    void start();

signals:
    void discovered(SyncService *service);
};
