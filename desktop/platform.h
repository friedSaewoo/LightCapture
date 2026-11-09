#pragma once
#include <QImage>
#include <QKeySequence>
#include <QScreen>
#include <functional>
#include <memory>

namespace lc {
using ImageResult = std::function<void(QImage, QString)>;

class ScreenSource {
public:
    virtual ~ScreenSource() = default;
    virtual void grab(QRect area, int maximumEdge, bool cursor, ImageResult result) = 0;
};
using SourceResult = std::function<void(std::shared_ptr<ScreenSource>, QString)>;
void openScreen(QScreen *screen, QObject *context, SourceResult result);
bool usesPortal();
void portalScreenshot(QObject *context, ImageResult result);

class GlobalKey {
public:
    virtual ~GlobalKey() = default;
};
std::unique_ptr<GlobalKey> registerGlobalKey(QKeySequence sequence, QObject *context,
                                             std::function<void()> action);
QKeySequence defaultCaptureKey();
QSize fitSize(QSize original, int edge);
QImage loadImage(const QString &path);
bool saveImage(const QImage &image, const QString &path);
}
