#pragma once
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include "pairingserver.h"
#include "filetransfer.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void sendFilePath(const QString &filePath);

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onQuitClicked();
    void onRegenerateClicked();
    void onTickTimer();
    void onDevicePaired(const QString &deviceName, const QString &token, const QString &deviceIp);
    void onPairingFailed(const QString &reason);
    void onTransferDone(const QString &fileName);
    void onTransferFailed(const QString &reason);

private:
    void setupUI();
    void setupTray();
    QString generateCode();
    void applyCode(const QString &code);
    void showLinkedState(const QString &deviceName);

    QSystemTrayIcon *trayIcon;
    QMenu           *trayMenu;

    QLabel      *codeLabel;
    QLabel      *statusLabel;
    QLabel      *timerLabel;
    QLabel      *linkedLabel;
    QPushButton *regenerateBtn;

    QSettings      *settings;
    QTimer         *countdownTimer;
    PairingServer  *pairingServer;
    FileTransfer   *fileTransfer;
    int             secondsLeft;
    QString         currentCode;
};