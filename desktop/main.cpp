#include "capture.h"
#include "editor.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

int selfTest(const QString &previewPath);

static QIcon captureIcon() {
    QPixmap image(64, 64);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#1761d7"));
    painter.drawRoundedRect(QRectF(2, 2, 60, 60), 14, 14);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(20, 23, 24, 18), 3, 3);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 2.5, Qt::DashLine));
    painter.drawRoundedRect(QRectF(13, 16, 38, 32), 4, 4);
    return QIcon(image);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("LightCapture");
    app.setApplicationName("LightCapture");
    app.setApplicationVersion("1.2.0-preview.1");
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(captureIcon());
    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, Qt::white);
    palette.setColor(QPalette::WindowText, QColor("#172033"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Text, QColor("#172033"));
    palette.setColor(QPalette::Highlight, QColor("#1761d7"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption captureOption("capture", "Start or cancel area capture.");
    const QCommandLineOption trayOption("tray", "Start in the tray when available.");
    const QCommandLineOption testOption("self-test", "Verify the editor and GIF encoder with synthetic pixels.");
    const QCommandLineOption previewOption("test-preview", "Render the test editor to a PNG.", "path");
    for (const auto &option : {captureOption, trayOption, testOption, previewOption}) parser.addOption(option);
    parser.process(app);
    if (parser.isSet(testOption)) return selfTest(parser.value(previewOption));

    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty() || !QDir().mkpath(root)) return 1;
    QByteArray session = root.toUtf8() + '\n' + qgetenv("DISPLAY") + '\n' + qgetenv("WAYLAND_DISPLAY");
    QString name = "lightcapture-" + QString::fromLatin1(QCryptographicHash::hash(session, QCryptographicHash::Sha256)
                                                          .toHex().left(32));
    QLockFile lock(QDir(root).filePath(name + ".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) {
        QLocalSocket client;
        for (int attempt = 0; attempt < 3; ++attempt) {
            client.connectToServer(name, QIODevice::WriteOnly);
            if (!client.waitForConnected(500)) {
                client.abort();
                continue;
            }
            client.write(parser.isSet(captureOption) ? "capture\n" : "show\n");
            client.flush();
            if (client.bytesToWrite() && !client.waitForBytesWritten(500)) return 1;
            return 0;
        }
        qCritical("LightCapture is already running but could not be contacted.");
        return 1;
    }

    lc::Editor editor(QDir(root).filePath("captures"));
    lc::CaptureFlow flow(editor);
    QSettings settings;
    QKeySequence sequence = QKeySequence::fromString(settings.value("captureKey",
        lc::defaultCaptureKey().toString(QKeySequence::PortableText)).toString(), QKeySequence::PortableText);
    auto onCapture = [&flow] { flow.request(false); };
    auto hotkey = lc::registerGlobalKey(sequence, &editor, onCapture);
    auto showKey = [&] {
        flow.setGlobalKey(bool(hotkey));
        editor.setCaptureHint(hotkey ? "영역 캡처 · " + sequence.toString(QKeySequence::NativeText)
            : lc::usesPortal() ? "Wayland 전역 단축키는 시스템 설정에 lightcapture --capture를 등록하세요."
                               : "전역 단축키를 등록하지 못했습니다. 단축키 버튼에서 변경하세요.");
    };
    showKey();
    QObject::connect(&editor, &lc::Editor::captureRequested, &flow, &lc::CaptureFlow::request);
    QObject::connect(&editor, &lc::Editor::shortcutRequested, &editor, [&] {
        if (lc::usesPortal()) {
            QMessageBox::information(&editor, "Wayland 단축키",
                "시스템 설정의 사용자 지정 키보드 단축키에 명령 lightcapture --capture를 연결하세요.");
            return;
        }
        QDialog dialog(&editor);
        dialog.setWindowTitle("캡처 단축키");
        auto layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel("Ctrl, Alt 또는 Command/Super와 문자·숫자·F1~F12를 함께 누르세요."));
        auto entry = new QKeySequenceEdit(sequence);
        layout->addWidget(entry);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        const auto next = entry->keySequence();
        if (next == sequence && hotkey) return;
        auto key = lc::registerGlobalKey(next, &editor, onCapture);
        if (!key) {
            QMessageBox::warning(&editor, "단축키 등록 실패", "다른 키 조합을 지정하세요.");
            return;
        }
        sequence = next;
        hotkey = std::move(key);
        settings.setValue("captureKey", sequence.toString(QKeySequence::PortableText));
        showKey();
    });

    QMenu trayMenu;
    QSystemTrayIcon tray(app.windowIcon());
    const bool haveTray = QSystemTrayIcon::isSystemTrayAvailable();
    if (haveTray) {
        trayMenu.addAction("편집기 열기", &flow, &lc::CaptureFlow::showCurrent);
        trayMenu.addAction("영역 캡처", &flow, onCapture);
        auto gif = trayMenu.addAction("GIF 녹화", &flow, [&] { flow.request(true); });
        gif->setEnabled(!lc::usesPortal());
        trayMenu.addSeparator();
        trayMenu.addAction("프로그램 종료", &editor, &lc::Editor::quit);
        tray.setToolTip("LightCapture");
        tray.setContextMenu(&trayMenu);
        QObject::connect(&tray, &QSystemTrayIcon::activated, &flow, [&](auto reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                flow.showCurrent();
        });
        tray.show();
        editor.setTrayAvailable(true);
    }

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name);
    if (!server.listen(name)) {
        qCritical("LightCapture could not create its local shortcut socket.");
        return 1;
    }
    QObject::connect(&server, &QLocalServer::newConnection, &editor, [&] {
        while (auto socket = server.nextPendingConnection()) {
            socket->setReadBufferSize(32);
            auto receive = [&flow, socket] {
                if (!socket->canReadLine()) return;
                const auto command = socket->readLine(16).trimmed();
                socket->disconnectFromServer();
                if (command == "capture") flow.request(false);
                else if (command == "show") flow.showCurrent();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, receive);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(1500, socket, [socket] { socket->disconnectFromServer(); socket->deleteLater(); });
            receive();
        }
    });
    if (!parser.isSet(trayOption) || !haveTray) editor.showEditor();
    if (parser.isSet(captureOption)) QTimer::singleShot(0, &flow, onCapture);
    return app.exec();
}
