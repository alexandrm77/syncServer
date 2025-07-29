// AppController.h
#pragma once

#include <QObject>
#include <QString>

class SyncServer;
class SyncService;
class SyncServiceHelper;

class SyncController : public QObject
{
    Q_OBJECT
public:
    explicit SyncController(QObject *parent = nullptr);
    ~SyncController();

    enum class Mode { None, Server, Client };
    Q_ENUM(Mode)

    void switchToMode(Mode mode);
    Mode currentMode() const;

signals:
    void modeChanged(Mode newMode);

private:
    void clearCurrentMode();

private:
    Mode m_mode = Mode::None;
    SyncServer *m_server = nullptr;
    SyncService *m_client = nullptr;
    SyncServiceHelper *m_clientHelper = nullptr;
};
