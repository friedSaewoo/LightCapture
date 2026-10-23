#include "app.h"
namespace lc {
static BOOL CALLBACK enumChild(HWND h, LPARAM data) {
    auto a = (App *)data;
    RECT r;
    if (IsWindowVisible(h) && GetWindowRect(h, &r) && rw(r) > 2 && rh(r) > 2)
        a->targets.push_back({h, r, true});
    return TRUE;
}
static BOOL CALLBACK enumTop(HWND h, LPARAM data) {
    auto a = (App *)data;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT r{};
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (IsWindowVisible(h) && !IsIconic(h) && !cloaked && pid != GetCurrentProcessId() &&
        GetWindowRect(h, &r) && rw(r) > 1 && rh(r) > 1) {
        a->targets.push_back({h, r, false});
        if (a->mode == Mode::Unit)
            EnumChildWindows(h, enumChild, data);
    }
    return TRUE;
}
void App::begin(Mode m) {
    finishText();
    if (busy) {
        if (!overlay && !scrollBar) {
            cancelCapture();
            statusText(L"지연 캡처를 취소했습니다.");
            return;
        }
        statusText(L"현재 캡처를 먼저 완료하거나 Esc로 취소하세요.");
        return;
    }
    mode = m;
    previous = GetForegroundWindow();
    previouslyVisible = IsWindowVisible(main);
    busy = true;
    waitingForHide = true;
    hiddenFrameCount = 0;
    captureNotBefore = GetTickCount64() + (ULONGLONG)store.settings.delay * 1000;
    captureStartDeadline = captureNotBefore + 2000;
    BOOL transitionsDisabled = TRUE;
    DwmSetWindowAttribute(main, DWMWA_TRANSITIONS_FORCEDISABLED, &transitionsDisabled,
                          sizeof(transitionsDisabled));
    SendMessageW(tooltip, TTM_POP, 0, 0);
    ShowOwnedPopups(main, FALSE);
    ShowWindow(main, SW_HIDE);
    original.reset();
    rendered.reset();
    thumbs.clear();
    store.releaseEdits();
    targets.clear();
    if (store.settings.delay > 0) {
        statusText(L"지연 캡처 대기 중");
    }
    require(SetTimer(main, 5, 16, nullptr) != 0, "capture hide timer failed");
}
bool App::advanceCaptureStart(bool hidden, bool composed, ULONGLONG now) {
    if (!busy || !waitingForHide)
        return false;
    if (!hidden || !composed || now < captureNotBefore) {
        hiddenFrameCount = 0;
        return false;
    }
    return ++hiddenFrameCount >= 2;
}
void App::pollCaptureStart() {
    if (!busy || !waitingForHide) {
        KillTimer(main, 5);
        return;
    }
    auto now = GetTickCount64();
    if (now >= captureStartDeadline) {
        cancelCapture(false);
        statusText(L"프로그램창 숨김·화면 갱신을 확인하지 못해 캡처를 취소했습니다. 다시 시도하세요.");
        return;
    }
    HWND popup = GetLastActivePopup(main);
    bool hidden = !IsWindowVisible(main) && (popup == main || !IsWindowVisible(popup));
    if (!hidden) {
        ShowOwnedPopups(main, FALSE);
        ShowWindow(main, SW_HIDE);
    }
    bool composed = hidden && SUCCEEDED(DwmFlush());
    if (advanceCaptureStart(hidden, composed, now)) {
        waitingForHide = false;
        KillTimer(main, 5);
        createOverlay();
    }
}
void App::createOverlay() {
    if (!busy || waitingForHide)
        return;
    KillTimer(main, 3);
    KillTimer(main, 5);
    if (IsWindowVisible(main)) {
        cancelCapture(false);
        statusText(L"프로그램창이 다시 표시되어 캡처를 취소했습니다.");
        return;
    }
    require(SUCCEEDED(DwmFlush()), "desktop composition did not complete before capture");
    virtualRect = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    virtualRect.right = virtualRect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virtualRect.bottom = virtualRect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (mode == Mode::Fixed &&
        (store.settings.fixedW > rw(virtualRect) || store.settings.fixedH > rh(virtualRect))) {
        cancelCapture();
        message(main, L"지정한 크기가 전체 화면보다 큽니다. 환경설정에서 폭과 높이를 조정하세요.", true);
        return;
    }
    snapshot = grab(virtualRect, store.settings.cursor);
    if (mode == Mode::Window || mode == Mode::Unit)
        EnumWindows(enumTop, (LPARAM)this);
    if (mode == Mode::Fullscreen) {
        selection = {0, 0, rw(virtualRect), rh(virtualRect)};
        if (currentMonitor) {
            POINT p;
            GetCursorPos(&p);
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
            selection = mi.rcMonitor;
            OffsetRect(&selection, -virtualRect.left, -virtualRect.top);
        }
        finishCapture();
        return;
    }
    overlayRenderer.prepare(*snapshot);
    POINT cursor{};
    GetCursorPos(&cursor);
    mouse = {cursor.x - virtualRect.left, cursor.y - virtualRect.top};
    selection = {};
    selectionReady = false;
    movingSelection = false;
    polygon.clear();
    dragging = false;
    overlay =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"LCOverlay", L"LightCapture 영역 선택 — Esc 취소",
                        WS_POPUP | WS_CLIPCHILDREN, virtualRect.left, virtualRect.top, rw(virtualRect),
                        rh(virtualRect), nullptr, nullptr, instance, this);
    SetForegroundWindow(overlay);
    SetFocus(overlay);
    createQuickBar(overlay);
    ShowWindow(overlay, SW_SHOW);
}
void App::cancelCapture(bool restoreEditor) {
    KillTimer(main, 3);
    KillTimer(main, 5);
    waitingForHide = false;
    hiddenFrameCount = 0;
    if (overlay) {
        ReleaseCapture();
        DestroyWindow(overlay);
        overlay = nullptr;
        quickBar = nullptr;
    }
    if (quickBar) {
        DestroyWindow(quickBar);
        quickBar = nullptr;
    }
    overlayRenderer.clear();
    snapshot.reset();
    targets.clear();
    polygon.clear();
    busy = false;
    dragging = false;
    if (!restoreEditor)
        hide();
    else if (previouslyVisible && !testMode)
        show();
    else if (previouslyVisible && selected >= 0)
        select(selected);
    if (!testMode && IsWindow(previous) && (restoreEditor || previous != main))
        SetForegroundWindow(previous);
}
void App::switchCaptureMode(Mode next) {
    if (!snapshot)
        return;
    if (next == Mode::Fixed &&
        (store.settings.fixedW > rw(virtualRect) || store.settings.fixedH > rh(virtualRect))) {
        message(overlay, L"지정한 크기가 화면보다 큽니다. 환경설정에서 크기를 조정하세요.", true);
        return;
    }
    mode = next;
    selection = {};
    selectionReady = false;
    dragging = false;
    movingSelection = false;
    polygon.clear();
    targets.clear();
    if ((mode == Mode::Window || mode == Mode::Unit) && !testMode)
        EnumWindows(enumTop, (LPARAM)this);
    if (mode == Mode::Fullscreen) {
        selection = {0, 0, rw(virtualRect), rh(virtualRect)};
        finishCapture();
        return;
    }
    if (quickBar) {
        SendMessageW(GetDlgItem(quickBar, ID_CAPTURE), BM_SETCHECK,
                     mode == Mode::Rectangle ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(quickBar, ID_WINDOW), BM_SETCHECK,
                     mode == Mode::Window ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    hover(mouse);
    if (overlay) {
        InvalidateRect(overlay, nullptr, FALSE);
        SetFocus(overlay);
    }
}
void App::hover(POINT p) {
    auto before = selection;
    auto oldMouse = mouse;
    mouse = p;
    if (mode == Mode::Window || mode == Mode::Unit) {
        POINT screen{p.x + virtualRect.left, p.y + virtualRect.top};
        selection = {};
        HWND root = nullptr;
        for (auto &t : targets)
            if (!t.child && inside(t.rect, screen)) {
                selection = t.rect;
                root = t.hwnd;
                break;
            }
        if (mode == Mode::Unit && root) {
            int64_t area = (int64_t)rw(selection) * rh(selection);
            for (auto &t : targets)
                if (t.child && GetAncestor(t.hwnd, GA_ROOT) == root && inside(t.rect, screen)) {
                    auto n = (int64_t)rw(t.rect) * rh(t.rect);
                    if (n < area) {
                        selection = t.rect;
                        area = n;
                    }
                }
        }
        OffsetRect(&selection, -virtualRect.left, -virtualRect.top);
        selection = bounded(selection, {0, 0, rw(virtualRect), rh(virtualRect)});
    } else if (mode == Mode::Fixed) {
        int w = std::min(store.settings.fixedW, rw(virtualRect)),
            h = std::min(store.settings.fixedH, rh(virtualRect));
        int x = std::clamp((int)p.x, 0, rw(virtualRect) - w),
            y = std::clamp((int)p.y, 0, rh(virtualRect) - h);
        selection = {x, y, x + w, y + h};
    } else if (dragging && movingSelection) {
        selection = selectionBefore;
        int x = std::clamp<LONG>((int)(p.x - start.x), -selectionBefore.left,
                                 virtualRect.right - virtualRect.left - selectionBefore.right);
        int y = std::clamp<LONG>((int)(p.y - start.y), -selectionBefore.top,
                                 virtualRect.bottom - virtualRect.top - selectionBefore.bottom);
        OffsetRect(&selection, x, y);
    } else if (dragging) {
        selection = normalized(start, p);
        if (mode == Mode::Freeform)
            polygon.push_back(p);
    }
    invalidateOverlay(before, oldMouse);
}
void App::invalidateOverlay(RECT before, POINT oldMouse) {
    if (!overlay)
        return;
    auto dirty = overlayRenderer.damage(before, oldMouse, selection, mouse, mode == Mode::Freeform);
    InvalidateRgn(overlay, dirty, FALSE);
    DeleteObject(dirty);
}
void App::paintOverlay(HDC dc, HRGN dirty) {
    overlayRenderer.paint(dc, dirty, selection, mouse, {virtualRect.left, virtualRect.top}, mode, polygon,
                          selectionReady);
}
void App::finishCapture() {
    RECT r = bounded(selection, {0, 0, (LONG)snapshot->GetWidth(), (LONG)snapshot->GetHeight()});
    if (rw(r) < 1 || rh(r) < 1) {
        cancelCapture();
        return;
    }
    if (overlay) {
        ReleaseCapture();
        DestroyWindow(overlay);
        overlay = nullptr;
        quickBar = nullptr;
    }
    if (quickBar) {
        DestroyWindow(quickBar);
        quickBar = nullptr;
    }
    overlayRenderer.clear();
    if (mode == Mode::Scroll) {
        RECT screen = r;
        OffsetRect(&screen, virtualRect.left, virtualRect.top);
        snapshot.reset();
        targets.clear();
        startScroll(screen);
        return;
    }
    Image image;
    if (mode == Mode::Freeform && polygon.size() > 2) {
        image = std::make_unique<Bitmap>(rw(r), rh(r), PixelFormat32bppARGB);
        Graphics g(image.get());
        g.Clear(Color::Transparent);
        Gdiplus::GraphicsPath path;
        std::vector<Gdiplus::Point> points;
        for (auto p : polygon)
            points.emplace_back(p.x - r.left, p.y - r.top);
        path.AddPolygon(points.data(), points.size());
        g.SetClip(&path);
        g.DrawImage(snapshot.get(), (int)-r.left, (int)-r.top);
    } else
        image = crop(*snapshot, r);
    overlayRenderer.clear();
    snapshot.reset();
    targets.clear();
    polygon.clear();
    busy = false;
    add(std::move(image));
}
LRESULT CALLBACK App::overlayProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        a = (App *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
    }
    if (!a)
        return DefWindowProcW(h, msg, w, l);
    try {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            auto dirty = CreateRectRgn(0, 0, 0, 0);
            GetUpdateRgn(h, dirty, FALSE);
            PAINTSTRUCT ps;
            auto dc = BeginPaint(h, &ps);
            a->paintOverlay(dc, dirty);
            EndPaint(h, &ps);
            DeleteObject(dirty);
            return 0;
        }
        case WM_MOUSEMOVE:
            a->hover({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            return 0;
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            a->hover(p);
            if (a->selectionReady && inside(a->selection, p) && a->mode != Mode::Freeform) {
                a->selectionBefore = a->selection;
                a->start = p;
                a->movingSelection = true;
                a->dragging = true;
                SetCapture(h);
                return 0;
            }
            if (a->mode == Mode::Window || a->mode == Mode::Unit || a->mode == Mode::Fixed) {
                a->finishCapture();
                return 0;
            }
            auto before = a->selection;
            a->start = p;
            a->selection = {p.x, p.y, p.x, p.y};
            a->invalidateOverlay(before, a->mouse);
            a->polygon = {p};
            a->dragging = true;
            SetCapture(h);
            return 0;
        }
        case WM_LBUTTONUP:
            if (a->dragging) {
                a->hover({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
                a->dragging = false;
                a->movingSelection = false;
                ReleaseCapture();
                if (a->store.settings.confirmRegion && a->mode != Mode::Freeform) {
                    a->selectionReady = true;
                    a->invalidateOverlay(a->selection, a->mouse);
                } else
                    a->finishCapture();
            }
            return 0;
        case WM_RBUTTONDOWN:
            a->cancelCapture();
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE)
                a->cancelCapture();
            else if (w == VK_RETURN && rw(a->selection) > 0)
                a->finishCapture();
            else if (a->selectionReady && (w == VK_LEFT || w == VK_RIGHT || w == VK_UP || w == VK_DOWN)) {
                int dx = w == VK_LEFT    ? -1
                         : w == VK_RIGHT ? 1
                                         : 0,
                    dy = w == VK_UP     ? -1
                         : w == VK_DOWN ? 1
                                        : 0;
                RECT before = a->selection;
                RECT r = a->selection;
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    r.right += dx;
                    r.bottom += dy;
                } else
                    OffsetRect(&r, dx, dy);
                if (r.left >= 0 && r.top >= 0 && r.right <= rw(a->virtualRect) &&
                    r.bottom <= rh(a->virtualRect) && rw(r) > 0 && rh(r) > 0)
                    a->selection = r;
                a->invalidateOverlay(before, a->mouse);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            a->cancelCapture();
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_CROSS));
            return TRUE;
        }
    } catch (const std::exception &e) {
        a->cancelCapture();
        a->report(e);
    }
    return DefWindowProcW(h, msg, w, l);
}
void App::startScroll(RECT r) {
    scrollRect = r;
    POINT center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    scrollTarget = WindowFromPoint(center);
    require(scrollTarget != nullptr, "scroll target missing");
    SetForegroundWindow(GetAncestor(scrollTarget, GA_ROOT));
    scrollFrames = 0;
    scrollPrevious = grab(r, false);
    if (scrollHorizontal)
        scrollPrevious->RotateFlip(Gdiplus::Rotate90FlipNone);
    scrollResult = clone(*scrollPrevious);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.right - 390, y = work.top + 12;
    if (x < r.right && x + 380 > r.left && y < r.bottom && y + 75 > r.top)
        y = work.bottom - 90;
    scrollBar =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"LCScroll", L"스크롤 캡처",
                        WS_POPUP | WS_BORDER, x, y, 380, 76, nullptr, nullptr, instance, this);
    control(scrollBar, L"BUTTON", L"완료", WS_TABSTOP, 12, 35, 90, 28, 1);
    control(scrollBar, L"BUTTON", L"취소", WS_TABSTOP, 112, 35, 90, 28, 2);
    control(scrollBar, L"STATIC",
            scrollManual ? L"직접 스크롤한 뒤 완료 · Esc 취소" : L"자동 스크롤 · 최대 60회 / 256 MiB", 0, 12,
            9, 350, 20, 3);
    ShowWindow(scrollBar, SW_SHOWNOACTIVATE);
    SetWindowDisplayAffinity(scrollBar, 0x00000011);
    RegisterHotKey(main, 66, MOD_NOREPEAT, VK_ESCAPE);
    SetTimer(main, 4, 700, nullptr);
    POINT wheel{center.x, center.y};
    if (!scrollManual)
        PostMessageW(scrollTarget, scrollHorizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL,
                     MAKEWPARAM(0, (WORD)(scrollHorizontal ? 360 : -360)), MAKELPARAM(wheel.x, wheel.y));
}
void App::scrollTick() {
    if (!scrollPrevious)
        return;
    ShowWindow(scrollBar, SW_HIDE);
    DwmFlush();
    auto next = grab(scrollRect, false);
    ShowWindow(scrollBar, SW_SHOWNOACTIVATE);
    if (scrollHorizontal)
        next->RotateFlip(Gdiplus::Rotate90FlipNone);
    if (sameFrame(*scrollPrevious, *next)) {
        if (scrollManual)
            return;
        if (scrollFrames == 0)
            warning = L"스크롤 이동을 확인하지 못했습니다. 대상 앱이 자동 스크롤을 받지 않으면 캡처 메뉴의 "
                      L"수동 스크롤을 사용하세요. 현재 화면 한 장만 보관했습니다.";
        stopScroll();
        return;
    }
    int ov = overlap(*scrollPrevious, *next);
    if (!ov) {
        warning = L"연결할 공통 영역을 찾지 못해 수집을 멈췄습니다. 현재 결과를 확인하세요. 고정 헤더를 "
                  L"제외하고 다시 시도할 수 있습니다.";
        stopScroll();
        return;
    }
    int newH = scrollResult->GetHeight() + next->GetHeight() - ov;
    uint64_t bytes = (uint64_t)newH * next->GetWidth() * 4;
    if (bytes > 256ull * 1024 * 1024 || newH > 60000) {
        warning = L"이미지 안전 한도에 도달해 수집을 멈췄습니다. 현재 결과를 확인하세요.";
        stopScroll();
        return;
    }
    auto merged = std::make_unique<Bitmap>(next->GetWidth(), newH, PixelFormat32bppARGB);
    {
        Graphics g(merged.get());
        g.DrawImage(scrollResult.get(), 0, 0);
        g.DrawImage(next.get(),
                    Gdiplus::Rect(0, scrollResult->GetHeight(), next->GetWidth(), next->GetHeight() - ov), 0,
                    ov, next->GetWidth(), next->GetHeight() - ov, Gdiplus::UnitPixel);
    }
    scrollResult = std::move(merged);
    scrollPrevious = std::move(next);
    scrollFrames++;
    if (scrollFrames >= 60) {
        warning = L"60회 한도에 도달했습니다. 현재 결과를 확인하세요.";
        stopScroll();
        return;
    }
    std::wstring s =
        L"스크롤 캡처 " + std::to_wstring(scrollFrames) + L"회 · " + std::to_wstring(newH) + L" px";
    SetWindowTextW(scrollBar, s.c_str());
    POINT c{(scrollRect.left + scrollRect.right) / 2, (scrollRect.top + scrollRect.bottom) / 2};
    if (!scrollManual)
        PostMessageW(scrollTarget, scrollHorizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL,
                     MAKEWPARAM(0, (WORD)(scrollHorizontal ? 360 : -360)), MAKELPARAM(c.x, c.y));
}
void App::stopScroll(bool cancelled, bool restoreEditor) {
    KillTimer(main, 4);
    UnregisterHotKey(main, 66);
    if (scrollBar) {
        DestroyWindow(scrollBar);
        scrollBar = nullptr;
    }
    scrollPrevious.reset();
    busy = false;
    if (cancelled) {
        scrollResult.reset();
        if (!restoreEditor)
            hide();
        else if (previouslyVisible && !testMode)
            show();
        if (!testMode && IsWindow(previous) && (restoreEditor || previous != main))
            SetForegroundWindow(previous);
        return;
    }
    auto image = std::move(scrollResult);
    if (scrollHorizontal && image)
        image->RotateFlip(Gdiplus::Rotate270FlipNone);
    add(std::move(image));
    if (!warning.empty()) {
        message(main, warning);
        warning.clear();
    }
}
LRESULT CALLBACK App::scrollProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT *)l)->lpCreateParams);
    auto a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (a && m == WM_COMMAND) {
        try {
            a->stopScroll(LOWORD(w) == 2);
        } catch (const std::exception &e) {
            a->report(e);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace lc
