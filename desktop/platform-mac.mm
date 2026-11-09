#include "platform.h"

#include <QMetaObject>
#include <QPointer>
#include <Carbon/Carbon.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#include <algorithm>

namespace lc {
bool usesPortal() { return false; }

void portalScreenshot(QObject *, ImageResult result) {
    result({}, "시스템 캡처 포털은 Linux에서 사용합니다.");
}

static QImage fromCGImage(CGImageRef image) {
    if (!image) return {};
    const size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    if (!w || !h || w > 16000 || h > 16000 || w * h > 64000000) return {};
    QImage pixels(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
    if (pixels.isNull()) return {};
    auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto bitmap = CGBitmapContextCreate(pixels.bits(), w, h, 8, pixels.bytesPerLine(), space,
                                        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
    CGColorSpaceRelease(space);
    if (!bitmap) return {};
    CGContextDrawImage(bitmap, CGRectMake(0, 0, w, h), image);
    CGContextRelease(bitmap);
    return pixels;
}

class MacScreenSource final : public ScreenSource {
    SCContentFilter *filter;
    QSize screenSize;
    qreal scale;
    QPointer<QObject> owner;

public:
    MacScreenSource(SCContentFilter *f, QSize size, qreal pixelScale, QObject *context)
        : filter(f), screenSize(size), scale(pixelScale), owner(context) {}

    void grab(QRect area, int maximumEdge, bool cursor, ImageResult result) override {
        const QRect bounds(QPoint(), screenSize);
        area = area.isValid() ? area.intersected(bounds) : bounds;
        if (!area.isValid()) {
            result({}, "캡처 영역이 모니터 밖에 있습니다.");
            return;
        }
        const QSize size = fitSize(area.size() * scale, maximumEdge ? maximumEdge : 12000);
        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.sourceRect = CGRectMake(area.x(), area.y(), area.width(), area.height());
        config.width = size.width();
        config.height = size.height();
        config.showsCursor = cursor;
        config.capturesAudio = NO;
        auto target = owner;
        [SCScreenshotManager captureImageWithFilter:filter configuration:config
                                  completionHandler:^(CGImageRef image, NSError *error) {
            QImage pixels = error ? QImage{} : fromCGImage(image);
            dispatch_async(dispatch_get_main_queue(), ^{
                if (target)
                    result(pixels, pixels.isNull() ? QString("화면을 읽지 못했습니다.") : QString{});
            });
        }];
    }
};

void openScreen(QScreen *screen, QObject *context, SourceResult result) {
    if (!screen) {
        result({}, "모니터를 찾지 못했습니다.");
        return;
    }
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        result({}, "시스템 설정 → 개인정보 보호 및 보안 → 화면 및 시스템 오디오 녹음에서 "
                   "LightCapture를 허용한 뒤 다시 실행하세요.");
        return;
    }
    const auto center = screen->geometry().center();
    CGDirectDisplayID displayID = 0;
    uint32_t count = 0;
    if (CGGetDisplaysWithPoint(CGPointMake(center.x(), center.y()), 1, &displayID, &count) != kCGErrorSuccess ||
        !count) {
        result({}, "선택한 모니터를 찾지 못했습니다.");
        return;
    }
    const auto size = screen->size();
    const auto scale = screen->devicePixelRatio();
    QPointer<QObject> owner(context);
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:YES
                                                  completionHandler:^(SCShareableContent *content, NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!owner) return;
            SCDisplay *chosen = nil;
            for (SCDisplay *display in content.displays)
                if (display.displayID == displayID) chosen = display;
            if (error || !chosen) {
                result({}, "화면 녹화 권한과 모니터 연결을 확인하세요.");
                return;
            }
            NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
            for (SCRunningApplication *application in content.applications)
                if (application.processID == NSProcessInfo.processInfo.processIdentifier)
                    [excluded addObject:application];
            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:chosen
                                                       excludingApplications:excluded exceptingWindows:@[]];
            result(std::make_shared<MacScreenSource>(filter, size, scale, owner.data()), {});
        });
    }];
}

class MacGlobalKey final : public GlobalKey {
    EventHandlerRef handler = nullptr;
    EventHotKeyRef key = nullptr;
    bool pressed = false;
    QPointer<QObject> owner;
    std::function<void()> action;

    static OSStatus fire(EventHandlerCallRef, EventRef event, void *pointer) {
        auto self = static_cast<MacGlobalKey *>(pointer);
        EventHotKeyID identifier{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                              sizeof(identifier), nullptr, &identifier) != noErr ||
            identifier.signature != 0x4c434150 || identifier.id != self->identifier.id)
            return eventNotHandledErr;
        if (GetEventKind(event) == kEventHotKeyReleased) self->pressed = false;
        else if (!self->pressed && self->owner) {
            self->pressed = true;
            QMetaObject::invokeMethod(self->owner.data(), self->action, Qt::QueuedConnection);
        }
        return noErr;
    }
    EventHotKeyID identifier{0x4c434150, 0};

public:
    ~MacGlobalKey() override {
        if (key) UnregisterEventHotKey(key);
        if (handler) RemoveEventHandler(handler);
    }

    bool init(QKeySequence sequence, QObject *context, std::function<void()> callback) {
        if (sequence.count() != 1) return false;
        const auto combination = sequence[0];
        const int letter = combination.key();
        static constexpr UInt32 letters[] = {
            kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
            kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
            kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
            kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z};
        static constexpr UInt32 digits[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
                                            kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
        static constexpr UInt32 functions[] = {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6,
                                               kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12};
        UInt32 code;
        if (letter >= Qt::Key_A && letter <= Qt::Key_Z) code = letters[letter - Qt::Key_A];
        else if (letter >= Qt::Key_0 && letter <= Qt::Key_9) code = digits[letter - Qt::Key_0];
        else if (letter >= Qt::Key_F1 && letter <= Qt::Key_F12) code = functions[letter - Qt::Key_F1];
        else return false;
        const auto flags = combination.keyboardModifiers();
        UInt32 modifiers = 0;
        if (flags.testFlag(Qt::MetaModifier)) modifiers |= controlKey;
        if (flags.testFlag(Qt::ControlModifier)) modifiers |= cmdKey;
        if (flags.testFlag(Qt::AltModifier)) modifiers |= optionKey;
        if (flags.testFlag(Qt::ShiftModifier)) modifiers |= shiftKey;
        if (!(modifiers & (controlKey | cmdKey | optionKey))) return false;
        owner = context;
        action = std::move(callback);
        static UInt32 nextIdentifier = 1;
        identifier.id = nextIdentifier++;
        EventTypeSpec events[] = {{kEventClassKeyboard, kEventHotKeyPressed},
                                  {kEventClassKeyboard, kEventHotKeyReleased}};
        if (InstallApplicationEventHandler(fire, 2, events, this, &handler) != noErr) return false;
        return RegisterEventHotKey(code, modifiers, identifier, GetApplicationEventTarget(), 0, &key) == noErr;
    }
};

std::unique_ptr<GlobalKey> registerGlobalKey(QKeySequence sequence, QObject *context,
                                             std::function<void()> action) {
    auto key = std::make_unique<MacGlobalKey>();
    if (!key->init(sequence, context, std::move(action))) return {};
    return key;
}

QKeySequence defaultCaptureKey() {
    return QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_X);
}
}
