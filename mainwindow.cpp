#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QRandomGenerator>
#include <QFont>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    settings       = new QSettings("OneNode", "DropIn", this);
    countdownTimer = new QTimer(this);
    pairingServer  = new PairingServer(this);
    fileTransfer   = new FileTransfer(this);

    connect(countdownTimer, &QTimer::timeout,
            this, &MainWindow::onTickTimer);
    connect(pairingServer, &PairingServer::devicePaired,
            this, &MainWindow::onDevicePaired);
    connect(pairingServer, &PairingServer::pairingFailed,
            this, &MainWindow::onPairingFailed);
    connect(fileTransfer, &FileTransfer::transferDone,
            this, &MainWindow::onTransferDone);
    connect(fileTransfer, &FileTransfer::transferFailed,
            this, &MainWindow::onTransferFailed);

    setupUI();
    setupTray();

    QString savedDevice = settings->value("device_name").toString();
    if (!savedDevice.isEmpty()) {
        showLinkedState(savedDevice);
    } else {
        onRegenerateClicked();
    }
}

void MainWindow::sendFilePath(const QString &filePath) {
    QString peerIp = settings->value("device_ip").toString();
    if (peerIp.isEmpty()) {
        trayIcon->showMessage("DropIn", "No device linked — open app to pair",
                              QSystemTrayIcon::Warning, 3000);
        return;
    }
    trayIcon->showMessage("DropIn",
        "Sending: " + QFileInfo(filePath).fileName(),
        QSystemTrayIcon::Information, 2000);
    fileTransfer->sendFile(filePath, peerIp);
}

QString MainWindow::generateCode() {
    int n = QRandomGenerator::global()->bounded(100000, 999999);
    return QString::number(n);
}

void MainWindow::applyCode(const QString &code) {
    currentCode = code;
    QString display = code.left(3) + "  " + code.right(3);
    codeLabel->setText(display);
    statusLabel->setText("⏳  Waiting for device...");
    secondsLeft = 300;
    countdownTimer->start(1000);
    onTickTimer();
    pairingServer->start(code);
}

void MainWindow::onTickTimer() {
    if (secondsLeft <= 0) {
        timerLabel->setText("Code expired — click Regenerate");
        countdownTimer->stop();
        pairingServer->stop();
        return;
    }
    int m = secondsLeft / 60;
    int s = secondsLeft % 60;
    timerLabel->setText(QString("Expires in %1:%2")
        .arg(m).arg(s, 2, 10, QChar('0')));
    secondsLeft--;
}

void MainWindow::onRegenerateClicked() {
    applyCode(generateCode());
}

void MainWindow::onDevicePaired(const QString &deviceName, const QString &token, const QString &deviceIp) {
    countdownTimer->stop();

    QString cleanIp = deviceIp;
    if (cleanIp.startsWith("::ffff:"))
        cleanIp = cleanIp.mid(7);

    settings->setValue("device_name", deviceName);
    settings->setValue("pairing_token", token);
    settings->setValue("device_ip", cleanIp);
    settings->sync();
    showLinkedState(deviceName);
}

void MainWindow::onPairingFailed(const QString &reason) {
    statusLabel->setText("❌  " + reason);
}

void MainWindow::showLinkedState(const QString &deviceName) {
    codeLabel->setText("Linked ✓");
    timerLabel->setText("");
    statusLabel->setText("✅  Connected to " + deviceName);
    linkedLabel->setText("Linked to: " + deviceName);
    regenerateBtn->setText("Unlink");
    trayIcon->setToolTip("DropIn — linked to " + deviceName);

    disconnect(regenerateBtn, &QPushButton::clicked, nullptr, nullptr);
    connect(regenerateBtn, &QPushButton::clicked, this, [this]() {
        settings->remove("device_name");
        settings->remove("pairing_token");
        settings->remove("device_ip");
        settings->sync();
        regenerateBtn->setText("Regenerate");
        disconnect(regenerateBtn, nullptr, this, nullptr);
        connect(regenerateBtn, &QPushButton::clicked,
                this, &MainWindow::onRegenerateClicked);
        onRegenerateClicked();
    });
}

void MainWindow::onTransferDone(const QString &fileName) {
    trayIcon->showMessage("DropIn", "Sent: " + fileName,
                          QSystemTrayIcon::Information, 3000);
}

void MainWindow::onTransferFailed(const QString &reason) {
    trayIcon->showMessage("DropIn", "Failed: " + reason,
                          QSystemTrayIcon::Warning, 3000);
}

void MainWindow::setupUI() {
    setWindowTitle("DropIn — Link device");
    setFixedSize(400, 380);
    setWindowIcon(QIcon(":/icon.png"));

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    QLabel *title = new QLabel("Link your phone", this);
    QFont tf = title->font();
    tf.setPointSize(15);
    tf.setWeight(QFont::Medium);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);

    QLabel *subtitle = new QLabel("Open DropIn on Android and enter this code", this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("color: gray; font-size: 12px;");

    root->addWidget(title);
    root->addWidget(subtitle);

    QFrame *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(
        "QFrame { border: 1px solid #ddd; border-radius: 12px; background: white; }"
    );

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 20, 20, 20);
    cardLayout->setSpacing(12);

    QLabel *codeHint = new QLabel("Your pairing code", card);
    codeHint->setStyleSheet("color: gray; font-size: 11px; border: none;");

    codeLabel = new QLabel("--- ---", card);
    QFont cf = codeLabel->font();
    cf.setPointSize(32);
    cf.setWeight(QFont::Medium);
    cf.setFamily("monospace");
    codeLabel->setFont(cf);
    codeLabel->setAlignment(Qt::AlignCenter);
    codeLabel->setStyleSheet("letter-spacing: 6px; border: none;");

    timerLabel = new QLabel("", card);
    timerLabel->setAlignment(Qt::AlignCenter);
    timerLabel->setStyleSheet("color: gray; font-size: 12px; border: none;");

    QHBoxLayout *bottomRow = new QHBoxLayout();
    statusLabel = new QLabel("⏳  Waiting...", card);
    statusLabel->setStyleSheet("color: gray; font-size: 12px; border: none;");

    regenerateBtn = new QPushButton("Regenerate", card);
    regenerateBtn->setStyleSheet(
        "QPushButton { border: 1px solid #ccc; border-radius: 6px;"
        "padding: 5px 14px; font-size: 12px; background: white; }"
        "QPushButton:hover { background: #f5f5f5; }"
    );
    connect(regenerateBtn, &QPushButton::clicked,
            this, &MainWindow::onRegenerateClicked);

    bottomRow->addWidget(statusLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(regenerateBtn);

    cardLayout->addWidget(codeHint);
    cardLayout->addWidget(codeLabel);
    cardLayout->addWidget(timerLabel);
    cardLayout->addSpacing(4);
    cardLayout->addLayout(bottomRow);

    root->addWidget(card);

    linkedLabel = new QLabel("", this);
    linkedLabel->setAlignment(Qt::AlignCenter);
    linkedLabel->setStyleSheet("color: gray; font-size: 12px;");
    root->addWidget(linkedLabel);

    QLabel *hint = new QLabel(
        "Drag any file onto the DropIn dock icon to send it to your phone.", this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(
        "background: #EEEDFE; border-radius: 8px;"
        "padding: 12px; color: #534AB7; font-size: 12px;"
    );
    hint->setWordWrap(true);
    root->addWidget(hint);
    root->addStretch();
}

void MainWindow::setupTray() {
    trayMenu = new QMenu(this);
    trayMenu->addAction("Open", this, &MainWindow::show);
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", this, &MainWindow::onQuitClicked);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon(":/icon.png"));
    trayIcon->setContextMenu(trayMenu);
    trayIcon->setToolTip("DropIn — not linked");
    trayIcon->show();

    connect(trayIcon, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger)
        isVisible() ? hide() : show();
}

void MainWindow::onQuitClicked() {
    QApplication::quit();
}