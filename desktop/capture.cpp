#include "capture.h"
#include "editor.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QWindow>
#include <algorithm>

namespace lc {
QSize fitSize(QSize size, int edge) {
    if (std::max(size.width(), size.height()) <= edge) return size;
    return size.scaled(edge, edge, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

QImage loadImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 12000 || size.height() > 12000 ||
        qint64(size.width()) * size.height() > 64000000) return {};
    auto image = reader.read();
    image.setDevicePixelRatio(1);
    return image;
}

bool saveImage(const QImage &image, const QString &path) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
}

class RegionPicker final : public QWidget {
    QImage snapshot;
    QPoint start, point;
    bool dragging = false;
    std::function<void(QRect)> chosen;
    std::function<void()> cancelled;

    void choose(const QRect &area) {
        auto result = chosen;
        hide();
        QTimer::singleShot(0, this, [result, area] { result(area); });
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.drawImage(rect(), snapshot);
        painter.fillRect(rect(), QColor(10, 18, 32, 120));
        if (dragging) {
            const auto area = QRect(start, point).normalized().intersected(rect());
            painter.save();
            painter.setClipRect(area);
            painter.drawImage(rect(), snapshot);
            painter.restore();
            painter.setPen(QPen(QColor("#70acff"), 2));
            painter.drawRect(area.adjusted(0, 0, -1, -1));
        } else if (point.x() >= 0) {
            painter.setPen(QColor(255, 255, 255, 155));
            painter.drawLine(point.x(), 0, point.x(), height());
            painter.drawLine(0, point.y(), width(), point.y());
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::RightButton) { cancelled(); return; }
        if (event->button() != Qt::LeftButton) return;
        start = point = event->position().toPoint();
        dragging = true;
        update();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        point = event->position().toPoint();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !dragging) return;
        dragging = false;
        auto selection = QRect(start, event->position().toPoint()).normalized().intersected(rect());
        if (selection.width() >= 2 && selection.height() >= 2) choose(selection);
        else update();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) cancelled();
        else QWidget::keyPressEvent(event);
    }
    void closeEvent(QCloseEvent *event) override {
        event->ignore();
        cancelled();
    }

public:
    RegionPicker(QScreen *screen, QImage image, bool gif, std::function<void(QRect)> result,
                 std::function<void()> cancel)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
          snapshot(std::move(image)), point(-1, -1), chosen(std::move(result)), cancelled(std::move(cancel)) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::CrossCursor);
        auto panel = new QWidget(this);
        panel->setCursor(Qt::ArrowCursor);
        panel->setStyleSheet("QWidget {background:white; color:#172033;} QPushButton {padding:5px 12px;}");
        auto layout = new QHBoxLayout(panel);
        layout->setContentsMargins(10, 5, 8, 5);
        layout->addWidget(new QLabel(gif ? "GIF로 녹화할 영역을 드래그하세요." : "캡처할 영역을 드래그하세요."));
        auto whole = new QPushButton("현재 화면");
        auto close = new QPushButton("취소 · Esc");
        layout->addWidget(whole);
        layout->addWidget(close);
        connect(whole, &QPushButton::clicked, this, [this] { choose(rect()); });
        connect(close, &QPushButton::clicked, this, [this] { cancelled(); });
        panel->adjustSize();
        setGeometry(screen->geometry());
        winId();
        windowHandle()->setScreen(screen);
        showFullScreen();
        panel->move(std::max(0, (width() - panel->width()) / 2), 14);
        raise();
        activateWindow();
        setFocus();
    }
};

CaptureFlow::CaptureFlow(Editor &window) : editor(window) {
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(20);
    connect(&timer, &QTimer::timeout, this, &CaptureFlow::tick);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *removed) {
        if (stage != Stage::Idle && stage != Stage::Preview && (!screen || screen == removed))
            reset(true, "모니터 연결이 바뀌어 캡처를 취소했습니다.");
    });
}

CaptureFlow::~CaptureFlow() { reset(false); }

void CaptureFlow::reset(bool restore, const QString &message) {
    ++generation;
    timer.stop();
    if (picker) {
        picker->hide();
        picker->deleteLater();
        picker = nullptr;
    }
    if (dialog) {
        dialog->hide();
        dialog->deleteLater();
        dialog = nullptr;
    }
    recording.reset();
    temporary.reset();
    source.reset();
    screen = nullptr;
    grabPending = false;
    stopButton = nullptr;
    status = nullptr;
    stage = Stage::Idle;
    if (restore && (restoreEditor || !message.isEmpty())) editor.showEditor();
    if (!message.isEmpty()) editor.notice(message);
}

void CaptureFlow::showCurrent() {
    if (stage == Stage::Idle) editor.showEditor();
    else if (dialog && stage != Stage::Recording) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }
}

void CaptureFlow::acceptImage(QImage image) {
    reset(false);
    editor.showEditor();
    editor.addImage(std::move(image));
}

void CaptureFlow::request(bool gif) {
    if (stage == Stage::Recording) { stopRecording(); return; }
    if (stage == Stage::Preparing || stage == Stage::Selecting) { reset(false); return; }
    if (stage != Stage::Idle) { showCurrent(); return; }
    if (QApplication::activeModalWidget()) return;
    if (gif && usesPortal()) {
        editor.notice("GIF 녹화는 Linux X11 세션에서 사용할 수 있습니다.");
        return;
    }
    editor.editorCanvas()->commitText();
    if (!editor.persist()) return;
    restoreEditor = editor.isVisible();
    editor.hide();
    stage = Stage::Preparing;
    const int ticket = ++generation;
    QTimer::singleShot(160, this, [this, ticket, gif] {
        if (generation != ticket) return;
        if (usesPortal()) {
            stage = Stage::Portal;
            portalScreenshot(this, [this, ticket](QImage image, QString error) {
                if (generation != ticket) return;
                if (image.isNull()) reset(true, error);
                else acceptImage(std::move(image));
            });
            return;
        }
        screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        openScreen(screen, this, [this, ticket, gif](std::shared_ptr<ScreenSource> opened, QString error) {
            if (generation != ticket) return;
            if (!opened) { reset(true, error); return; }
            source = std::move(opened);
            auto current = source;
            current->grab({}, 0, false, [this, ticket, gif](QImage snapshot, QString failure) {
                if (generation != ticket) return;
                if (snapshot.isNull() || !screen) { reset(true, failure); return; }
                const QSize logical = screen->size();
                stage = Stage::Selecting;
                auto image = std::make_shared<QImage>(std::move(snapshot));
                picker = new RegionPicker(screen, *image, gif, [this, ticket, gif, image, logical](QRect region) {
                    if (generation != ticket) return;
                    if (picker) picker->deleteLater();
                    picker = nullptr;
                    if (gif) { prepareRecording(region); return; }
                    const double sx = double(image->width()) / logical.width();
                    const double sy = double(image->height()) / logical.height();
                    const QRect physical = QRectF(region.x() * sx, region.y() * sy,
                                                   region.width() * sx, region.height() * sy).toAlignedRect();
                    acceptImage(image->copy(physical.intersected(image->rect())));
                }, [this] { reset(true); });
            });
        });
    });
    QTimer::singleShot(15000, this, [this, ticket] {
        if (generation == ticket && stage == Stage::Preparing)
            reset(true, "화면 캡처 응답 시간이 초과됐습니다.");
    });
}

static void placeControls(QDialog &panel, QScreen *screen, const QRect &capture) {
    panel.adjustSize();
    const auto work = screen->availableGeometry();
    const auto global = capture.translated(screen->geometry().topLeft());
    const auto frame = panel.frameGeometry();
    const int width = frame.width(), height = frame.height();
    const int x = std::max(work.left(), std::min(global.left(), work.right() - width + 1));
    const int y = global.top() - height - 12 >= work.top() ? global.top() - height - 12 :
                  global.bottom() + height + 12 <= work.bottom() ? global.bottom() + 12 : work.top() + 12;
    panel.move(x, y);
}

void CaptureFlow::prepareRecording(QRect region) {
    if (!screen) { reset(true, "모니터가 연결되어 있지 않습니다."); return; }
    area = region;
    stage = Stage::Ready;
    dialog = new QDialog(&editor, Qt::Tool | Qt::WindowStaysOnTopHint);
    dialog->setWindowTitle("LightCapture · GIF 녹화");
    dialog->setMinimumWidth(420);
    auto layout = new QVBoxLayout(dialog);
    status = new QLabel("화질을 고른 뒤 시작하세요. 녹화는 3초 뒤 시작합니다.");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto options = new QComboBox;
    for (const auto &option : gifProfiles) options->addItem(QString::fromUtf8(option.name));
    options->setCurrentIndex(profile);
    layout->addWidget(options);
#ifdef Q_OS_MACOS
    auto cursor = new QCheckBox("마우스 커서 포함");
    cursor->setChecked(includeCursor);
    layout->addWidget(cursor);
    connect(cursor, &QCheckBox::toggled, this, [this](bool checked) { includeCursor = checked; });
#endif
    auto row = new QHBoxLayout;
    auto start = new QPushButton("녹화 시작");
    auto stop = new QPushButton("정지");
    auto cancel = new QPushButton("취소");
    stopButton = stop;
    stop->setEnabled(false);
    row->addStretch();
    row->addWidget(start);
    row->addWidget(stop);
    row->addWidget(cancel);
    layout->addLayout(row);
    connect(start, &QPushButton::clicked, this, [this, options, start] {
        profile = options->currentIndex();
        startRecording();
        if (stage == Stage::Countdown) {
            start->setEnabled(false);
            options->setEnabled(false);
        }
    });
    connect(stop, &QPushButton::clicked, this, &CaptureFlow::stopRecording);
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::rejected, this, [this] { reset(true); });
    dialog->show();
    placeControls(*dialog, screen, area);
}

void CaptureFlow::startRecording() {
    if (stage != Stage::Ready || !screen) return;
#ifdef Q_OS_LINUX
    if (!globalKey && dialog->frameGeometry().intersects(area.translated(screen->geometry().topLeft()))) {
        status->setText("녹화 창을 영역 밖으로 옮기거나 전역 단축키를 등록한 뒤 시작하세요.");
        return;
    }
#endif
    temporary = std::make_unique<QTemporaryDir>();
    if (!temporary->isValid()) { reset(true, "GIF 임시 폴더를 만들지 못했습니다."); return; }
    gifPath = temporary->filePath("recording.gif");
    stage = Stage::Countdown;
    recordingNotice.clear();
    clock.restart();
    status->setText("3초 후 녹화합니다.");
    timer.start();
}

void CaptureFlow::tick() {
    if (stage == Stage::Countdown) {
        const auto remaining = 3000 - clock.elapsed();
        if (remaining > 0) {
            const auto text = QString("%1초 후 녹화합니다.").arg((remaining + 999) / 1000);
            if (status->text() != text) status->setText(text);
            return;
        }
        stage = Stage::Recording;
        recording = std::make_unique<GifRecorder>(gifPath);
        nextSample = 0;
        clock.restart();
        stopButton->setEnabled(true);
        status->setText(globalKey ? "녹화 중 · 캡처 단축키 또는 정지 버튼으로 마치기" : "녹화 중 · 정지 버튼으로 마치기");
#ifdef Q_OS_LINUX
        if (dialog->frameGeometry().intersects(area.translated(screen->geometry().topLeft())) && globalKey) {
            dialog->hide();
            nextSample = 160;
        }
#endif
    }
    if (stage == Stage::Finishing && recording->done()) {
        const auto error = recording->error();
        recording.reset();
        timer.stop();
        source.reset();
        if (!error.isEmpty()) reset(true, error);
        else showPreview();
        return;
    }
    if (stage != Stage::Recording) return;
    const auto now = clock.elapsed();
    if (recording->done() || now >= gifTimeLimit || recording->bytes() >= gifByteLimit) {
        if (now >= gifTimeLimit) recordingNotice = "최대 녹화 시간에 도달했습니다.";
        else if (recording->bytes() >= gifByteLimit) recordingNotice = "파일 크기 기준에 도달했습니다.";
        stopRecording();
        return;
    }
#ifdef Q_OS_LINUX
    if (dialog->isVisible() && dialog->frameGeometry().intersects(area.translated(screen->geometry().topLeft()))) {
        if (!globalKey) {
            recordingNotice = "녹화 제어창이 선택 영역에 들어와 녹화를 마쳤습니다.";
            stopRecording();
            return;
        }
        dialog->hide();
        nextSample = now + 160;
        return;
    }
#endif
    if (now < nextSample || grabPending || !recording->readyForSample()) return;
    const auto &quality = gifProfiles[profile];
    nextSample = (now * quality.fps / 1000 + 1) * 1000 / quality.fps;
    if (nextSample <= now) nextSample = now + 1000 / quality.fps;
    grabPending = true;
    const int ticket = generation;
    auto current = source;
    current->grab(area, quality.edge, includeCursor, [this, ticket, now](QImage image, QString error) {
        if (generation != ticket || stage != Stage::Recording) return;
        grabPending = false;
        if (image.isNull()) { reset(true, error); return; }
        recording->submit(std::move(image), now);
    });
}

void CaptureFlow::stopRecording() {
    if (stage != Stage::Recording) return;
    duration = clock.elapsed();
    stage = Stage::Finishing;
    recording->stop(duration);
    status->setText("GIF를 정리하고 있습니다.");
    stopButton->setEnabled(false);
    dialog->show();
}

void CaptureFlow::showPreview() {
    if (dialog) {
        dialog->hide();
        dialog->deleteLater();
    }
    stage = Stage::Preview;
    dialog = new QDialog(&editor, Qt::Window);
    dialog->setWindowTitle("LightCapture · GIF 미리보기");
    dialog->setMinimumWidth(480);
    auto layout = new QVBoxLayout(dialog);
    auto information = new QLabel(QString("%1초 · %2 KiB").arg(duration / 1000., 0, 'f', 1)
                                      .arg((QFileInfo(gifPath).size() + 1023) / 1024));
    layout->addWidget(information);
    auto picture = new QLabel;
    picture->setAlignment(Qt::AlignCenter);
    picture->setStyleSheet("background:#edf1f6; border:1px solid #dce3ed;");
    picture->setMinimumSize(450, 240);
    auto movie = new QMovie(gifPath, "gif", picture);
    if (!movie->isValid()) { reset(true, "GIF 미리보기를 열지 못했습니다."); return; }
    QImageReader reader(gifPath, "gif");
    if (reader.size().isValid()) movie->setScaledSize(fitSize(reader.size(), 720));
    picture->setMovie(movie);
    layout->addWidget(picture, 1);
    auto feedback = new QLabel(recordingNotice);
    layout->addWidget(feedback);
    auto row = new QHBoxLayout;
    auto pause = new QPushButton("일시 정지");
    auto save = new QPushButton("저장");
    auto close = new QPushButton("닫기");
    row->addWidget(pause);
    row->addStretch();
    row->addWidget(save);
    row->addWidget(close);
    layout->addLayout(row);
    connect(pause, &QPushButton::clicked, dialog, [movie, pause] {
        bool playing = movie->state() == QMovie::Running;
        movie->setPaused(playing);
        pause->setText(playing ? "재생" : "일시 정지");
    });
    connect(save, &QPushButton::clicked, this, [this, feedback] {
        auto path = QFileDialog::getSaveFileName(dialog, "GIF 저장", "LightCapture.gif", "GIF 이미지 (*.gif)");
        if (path.isEmpty()) return;
        if (!path.endsWith(".gif", Qt::CaseInsensitive)) path += ".gif";
        feedback->setText(saveRecording(gifPath, path) ? "GIF를 저장했습니다." : "저장하지 못했습니다. 다른 폴더를 선택하세요.");
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::rejected, this, [this] { reset(true); });
    dialog->show();
    movie->start();
}
}
