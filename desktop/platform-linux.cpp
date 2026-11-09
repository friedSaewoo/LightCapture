#include "platform.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QPointer>
#include <QPixmap>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>
#include <QUrl>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <set>

namespace lc {
bool usesPortal() {
    return QGuiApplication::platformName().startsWith("wayland") ||
           qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland";
}

class XScreenSource final : public ScreenSource {
    QPointer<QScreen> screen;
public:
    explicit XScreenSource(QScreen *display) : screen(display) {}

    void grab(QRect area, int maximumEdge, bool, ImageResult result) override {
        if (!screen) {
            result({}, "선택한 모니터가 연결되어 있지 않습니다.");
            return;
        }
        const QRect bounds(QPoint(), screen->size());
        area = area.isValid() ? area.intersected(bounds) : bounds;
        if (!area.isValid()) {
            result({}, "캡처 영역이 모니터 밖에 있습니다.");
            return;
        }
        QImage image = screen->grabWindow(0, area.x(), area.y(), area.width(), area.height()).toImage();
        image.setDevicePixelRatio(1);
        if (maximumEdge && !image.isNull())
            image = image.scaled(fitSize(image.size(), maximumEdge), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        result(image, image.isNull() ? QString("화면을 읽지 못했습니다.") : QString{});
    }
};

void openScreen(QScreen *screen, QObject *, SourceResult result) {
    if (!screen || QGuiApplication::platformName() != "xcb" || usesPortal()) {
        result({}, "이 세션에서는 직접 화면 캡처를 사용할 수 없습니다.");
        return;
    }
    result(std::make_shared<XScreenSource>(screen), {});
}

class PortalRequest final : public QObject {
    Q_OBJECT
    const QString service = "org.freedesktop.portal.Desktop";
    const QString requestInterface = "org.freedesktop.portal.Request";
    QDBusConnection bus = QDBusConnection::sessionBus();
    QString path;
    ImageResult done;

    void complete(QImage image, QString error) {
        if (!done) return;
        bus.disconnect(service, path, requestInterface, "Response", this, SLOT(response(uint,QVariantMap)));
        auto callback = std::move(done);
        done = {};
        deleteLater();
        callback(std::move(image), std::move(error));
    }
    bool subscribe() {
        return bus.connect(service, path, requestInterface, "Response", this, SLOT(response(uint,QVariantMap)));
    }

private slots:
    void response(uint status, const QVariantMap &results) {
        if (status == 1) {
            complete({}, {});
            return;
        }
        if (status != 0) {
            complete({}, "데스크톱 캡처 요청을 완료하지 못했습니다.");
            return;
        }
        const QUrl uri(results.value("uri").toString());
        auto image = uri.isLocalFile() ? loadImage(uri.toLocalFile()) : QImage{};
        const QString error = image.isNull() ? QString("캡처 이미지를 열지 못했습니다.") : QString{};
        complete(std::move(image), error);
    }

public:
    PortalRequest(QObject *parent, ImageResult result) : QObject(parent), done(std::move(result)) {}

    void start() {
        const QString token = "lightcapture_" + QUuid::createUuid().toString(QUuid::Id128);
        QString sender = bus.baseService().mid(1);
        sender.replace('.', '_');
        path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
        if (!bus.isConnected() || !subscribe()) {
            complete({}, "데스크톱 캡처 서비스에 연결할 수 없습니다.");
            return;
        }
        auto message = QDBusMessage::createMethodCall(service, "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.portal.Screenshot", "Screenshot");
        message << QString() << QVariantMap{{"handle_token", token}, {"interactive", true}, {"modal", false}};
        auto watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 10000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
            QDBusPendingReply<QDBusObjectPath> reply = *watcher;
            watcher->deleteLater();
            if (!done) return;
            if (reply.isError()) {
                complete({}, "시스템 캡처가 응답하지 않습니다. 데스크톱의 캡처 포털을 확인하세요.");
                return;
            }
            if (reply.value().path() != path) {
                bus.disconnect(service, path, requestInterface, "Response", this, SLOT(response(uint,QVariantMap)));
                path = reply.value().path();
                if (!subscribe()) complete({}, "캡처 결과를 구독하지 못했습니다.");
            }
        });
        QTimer::singleShot(120000, this, [this] {
            if (!done) return;
            bus.asyncCall(QDBusMessage::createMethodCall(service, path, requestInterface, "Close"));
            complete({}, "시스템 캡처 선택 시간이 초과됐습니다.");
        });
    }
};

void portalScreenshot(QObject *context, ImageResult result) {
    (new PortalRequest(context, std::move(result)))->start();
}

class XGlobalKey final : public GlobalKey {
    Display *display = nullptr;
    std::unique_ptr<QSocketNotifier> notifier;
    int code = 0;
    unsigned modifiers = 0;
    bool held = false;
    bool detectableRepeat = false;
    std::set<unsigned> locks;
    static int grabError;

    static int handleError(Display *, XErrorEvent *) {
        grabError = 1;
        return 0;
    }

public:
    ~XGlobalKey() override {
        notifier.reset();
        if (display) XCloseDisplay(display);
    }

    bool init(QKeySequence sequence, QObject *context, std::function<void()> action) {
        if (usesPortal() || QGuiApplication::platformName() != "xcb" || sequence.count() != 1)
            return false;
        const auto combination = sequence[0];
        int key = combination.key();
        KeySym symbol = NoSymbol;
        if (key >= Qt::Key_A && key <= Qt::Key_Z) symbol = XK_a + key - Qt::Key_A;
        if (key >= Qt::Key_0 && key <= Qt::Key_9) symbol = XK_0 + key - Qt::Key_0;
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12) symbol = XK_F1 + key - Qt::Key_F1;
        const auto flags = combination.keyboardModifiers();
        if (flags.testFlag(Qt::ControlModifier)) modifiers |= ControlMask;
        if (flags.testFlag(Qt::ShiftModifier)) modifiers |= ShiftMask;
        if (flags.testFlag(Qt::AltModifier)) modifiers |= Mod1Mask;
        if (flags.testFlag(Qt::MetaModifier)) modifiers |= Mod4Mask;
        if (!symbol || !(modifiers & (ControlMask | Mod1Mask | Mod4Mask))) return false;
        display = XOpenDisplay(nullptr);
        if (!display) return false;
        Bool supported = False;
        detectableRepeat = XkbSetDetectableAutoRepeat(display, True, &supported) && supported;
        code = XKeysymToKeycode(display, symbol);
        if (!code) return false;
        locks = {0, LockMask};
        auto mapping = XGetModifierMapping(display);
        const auto numberLock = XKeysymToKeycode(display, XK_Num_Lock);
        if (mapping) {
            for (int i = 0; i < 8 * mapping->max_keypermod; ++i) {
                if (numberLock && mapping->modifiermap[i] == numberLock) {
                    const unsigned mask = 1u << (i / mapping->max_keypermod);
                    locks.insert(mask);
                    locks.insert(mask | LockMask);
                }
            }
            XFreeModifiermap(mapping);
        }
        auto previous = XSetErrorHandler(handleError);
        grabError = 0;
        for (int i = 0; i < ScreenCount(display); ++i)
            for (auto lock : locks)
                XGrabKey(display, code, modifiers | lock, RootWindow(display, i), False,
                         GrabModeAsync, GrabModeAsync);
        XSync(display, False);
        XSetErrorHandler(previous);
        if (grabError) return false;
        notifier = std::make_unique<QSocketNotifier>(ConnectionNumber(display), QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, context, [this, action] {
            while (XPending(display)) {
                XEvent event{};
                XNextEvent(display, &event);
                if ((event.type != KeyPress && event.type != KeyRelease) ||
                    event.xkey.keycode != static_cast<unsigned>(code)) continue;
                if (event.type == KeyRelease) {
                    if (!detectableRepeat && XPending(display)) {
                        XEvent next{};
                        XPeekEvent(display, &next);
                        if (next.type == KeyPress && next.xkey.keycode == event.xkey.keycode &&
                            next.xkey.time == event.xkey.time) continue;
                    }
                    held = false;
                } else if (!held) {
                    held = true;
                    action();
                }
            }
        });
        return true;
    }
};
int XGlobalKey::grabError = 0;

std::unique_ptr<GlobalKey> registerGlobalKey(QKeySequence sequence, QObject *context,
                                             std::function<void()> action) {
    auto key = std::make_unique<XGlobalKey>();
    if (!key->init(sequence, context, std::move(action))) return {};
    return key;
}

QKeySequence defaultCaptureKey() {
    return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X);
}
}
#include "platform-linux.moc"
