#pragma once
#include "platform.h"
#include "gif.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

class QDialog;
class QLabel;
class QPushButton;

namespace lc {
class Editor;
class RegionPicker;

class CaptureFlow final : public QObject {
    enum class Stage { Idle, Preparing, Portal, Selecting, Ready, Countdown, Recording, Finishing, Preview };
    Stage stage = Stage::Idle;
    Editor &editor;
    QPointer<QScreen> screen;
    QPointer<RegionPicker> picker;
    QPointer<QDialog> dialog;
    QLabel *status = nullptr;
    QPointer<QPushButton> stopButton;
    std::shared_ptr<ScreenSource> source;
    std::unique_ptr<QTemporaryDir> temporary;
    std::unique_ptr<GifRecorder> recording;
    QTimer timer;
    QElapsedTimer clock;
    int generation = 0, profile = 0;
    bool restoreEditor = false, globalKey = false, grabPending = false, includeCursor = true;
    QRect area;
    qint64 nextSample = 0, duration = 0;
    QString gifPath, recordingNotice;

    void reset(bool restore, const QString &message = {});
    void acceptImage(QImage image);
    void prepareRecording(QRect region);
    void startRecording();
    void tick();
    void stopRecording();
    void showPreview();

public:
    explicit CaptureFlow(Editor &window);
    ~CaptureFlow() override;
    void request(bool gif);
    void showCurrent();
    void setGlobalKey(bool registered) { globalKey = registered; }
};
}
