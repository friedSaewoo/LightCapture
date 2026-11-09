#include "editor.h"
#include "gif.h"
#include "platform.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QThread>

namespace {
bool require(bool passed, const char *name) {
    if (!passed) qCritical("FAIL %s", name);
    return passed;
}
void drag(lc::Canvas *canvas, QPointF a, QPointF b) {
    QMouseEvent down(QEvent::MouseButtonPress, a, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &down);
    QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent up(QEvent::MouseButtonRelease, b, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &up);
}
}

int selfTest(const QString &previewPath) {
    QTemporaryDir data;
    if (!require(data.isValid(), "isolated data directory")) return 1;
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor("#93c5fd"));
    lc::Editor editor(data.path());
    if (!require(editor.addImage(image), "image persistence")) return 1;
    editor.show();
    QApplication::processEvents();
    auto canvas = editor.editorCanvas();
    const QPointF origin((canvas->width() - 320) / 2., (canvas->height() - 180) / 2.);
    canvas->setTool(lc::Tool::Select);
    drag(canvas, origin + QPointF(40, 30), origin + QPointF(99, 79));
    if (!require(canvas->copyImage().size() == QSize(60, 50), "rectangle selection copy size")) return 1;
    canvas->deleteRegion();
    if (!require(canvas->pixels().pixelColor(45, 45) == Qt::white &&
                 canvas->pixels().pixelColor(10, 10) == image.pixelColor(10, 10), "selection erase")) return 1;
    canvas->undo();
    if (!require(canvas->pixels().pixelColor(45, 45) == image.pixelColor(45, 45), "erase undo")) return 1;
    drag(canvas, origin + QPointF(40, 30), origin + QPointF(99, 79));
    drag(canvas, origin + QPointF(60, 50), origin + QPointF(180, 85));
    if (!require(canvas->pixels().pixelColor(50, 40) == Qt::white &&
                 canvas->copyImage().pixelColor(15, 15) == image.pixelColor(10, 10), "region move")) return 1;
    drag(canvas, origin + QPointF(219, 114), origin + QPointF(249, 134));
    if (!require(canvas->copyImage().size() == QSize(90, 70), "region corner resize")) return 1;
    canvas->extend(QSize(360, 200), QPoint(18, 12));
    if (!require(canvas->pixels().size() == QSize(360, 200) && canvas->pixels().pixelColor(0, 0) == Qt::white,
                 "canvas expansion")) return 1;
    QImage paste(420, 210, QImage::Format_RGB32);
    paste.fill(Qt::red);
    canvas->paste(paste);
    if (!require(canvas->pixels().size() == QSize(484, 274) && canvas->pixels().pixelColor(40, 40) == Qt::red,
                 "paste grows canvas and composites pixels")) return 1;
    canvas->undo();
    if (!require(canvas->pixels().size() == QSize(360, 200), "paste undo")) return 1;
    if (!require(editor.persist(), "edited image flush")) return 1;
    lc::Editor reopened(data.path());
    if (!require(reopened.editorCanvas()->pixels() == canvas->pixels(), "reopened capture pixels")) return 1;
    if (!previewPath.isEmpty() && !require(editor.grab().save(previewPath), "own window render")) return 1;

    const auto animation = data.filePath("recording.gif");
    QImage first(120, 80, QImage::Format_RGB32);
    first.fill(QColor("#1761d7"));
    QImage second = first;
    second.setPixelColor(15, 16, Qt::red);
    lc::GifRecorder recorder(animation);
    recorder.submit(first, 0);
    recorder.submit(first, 100);
    recorder.submit(second, 200);
    recorder.stop(350);
    QElapsedTimer deadline;
    deadline.start();
    while (!recorder.done() && deadline.elapsed() < 10000) QThread::msleep(5);
    if (!require(recorder.done() && recorder.error().isEmpty(), "GIF encode")) return 1;
    QImageReader gif(animation, "gif");
    if (!require(gif.imageCount() == 2 && gif.loopCount() == -1, "GIF coalescing and infinite loop")) return 1;
    const auto frame1 = gif.read();
    const int delay = gif.nextImageDelay();
    const auto frame2 = gif.read();
    if (!require(frame1.size() == first.size() && frame1.pixelColor(15, 16) == first.pixelColor(15, 16) &&
                 frame2.size() == second.size() && frame2.pixelColor(15, 16) == Qt::red &&
                 frame2.pixelColor(0, 0) == first.pixelColor(0, 0) &&
                 delay == 200 && gif.nextImageDelay() == 150, "GIF color, delta and timing")) return 1;
    if (!require(lc::saveRecording(animation, data.filePath("saved.gif")) &&
                 QFileInfo(data.filePath("saved.gif")).size() == QFileInfo(animation).size(),
                 "GIF save")) return 1;
    qInfo("PASS editor selection, move, resize, erase, undo, canvas, paste, history and animated GIF");
    return 0;
}
