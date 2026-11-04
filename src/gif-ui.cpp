#include "app.h"
#include "theme.h"
#include <iomanip>
#include <sstream>
namespace lc {
enum : UINT {
    GIF_INFO = 10,
    GIF_PROFILE,
    GIF_CURSOR,
    GIF_START,
    GIF_STOP,
    GIF_CANCEL,
    GIF_SAVE,
    GIF_AGAIN,
    GIF_PLAY
};
static constexpr UINT GIF_SHARP = 22, GIF_SMALL = 23;
static std::wstring sizeText(uint64_t bytes) {
    std::wostringstream s;
    if (bytes < 1024 * 1024)
        s << (bytes + 1023) / 1024 << L" KB";
    else
        s << std::fixed << std::setprecision(1) << bytes / (1024. * 1024.) << L" MB";
    return s.str();
}
void App::beginGif() {
    if (gifState != GifState::Closed) {
        if (gifWindow && !testMode) {
            ShowWindow(gifWindow, SW_SHOWNORMAL);
            SetForegroundWindow(gifWindow);
        }
        return;
    }
    if (busy)
        return;
    begin(Mode::Rectangle);
    gifRequested = true;
    statusText(L"GIF로 녹화할 영역을 드래그하세요. Esc 취소");
}
void App::prepareGif(RECT area) {
    require(rw(area) >= 2 && rh(area) >= 2, "GIF recording area must be at least 2 x 2");
    gifRequested = false;
    gifRect = area;
    gifState = GifState::Ready;
    gifNotice.clear();
    busy = true;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = gifProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LCGif";
        require(RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                "GIF window class unavailable");
        registered = true;
    }
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST), &mi);
    UINT dpi = GetDpiForWindow(main);
    auto s = [&](int v) { return MulDiv(v, dpi ? dpi : 96, 96); };
    int w = std::min(s(660), rw(mi.rcWork)), h = s(220);
    int x = std::clamp((int)area.left, (int)mi.rcWork.left, (int)mi.rcWork.right - w);
    int y = area.top >= mi.rcWork.top + h + s(8)         ? area.top - h - s(8)
            : area.bottom + h + s(8) <= mi.rcWork.bottom ? area.bottom + s(8)
                                                         : mi.rcWork.top + s(16);
    gifWindow =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, L"LCGif",
                        L"LightCapture · GIF 녹화", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                        x, y, w, h, nullptr, nullptr, instance, this);
    require(gifWindow != nullptr, "GIF recording window unavailable");
    gifExcluded = testMode || SetWindowDisplayAffinity(gifWindow, 0x11) != FALSE;
    if (!testMode) {
        ShowWindow(gifWindow, SW_SHOW);
        SetForegroundWindow(gifWindow);
    }
    layoutGif();
}
void App::layoutGif() {
    if (!gifWindow)
        return;
    bool preview = gifState == GifState::Preview;
    bool ready = gifState == GifState::Ready;
    UINT dpi = GetDpiForWindow(gifWindow);
    auto s = [&](int v) { return MulDiv(v, dpi ? dpi : 96, 96); };
    RECT r{};
    GetClientRect(gifWindow, &r);
    auto show = [&](UINT id, bool visible) {
        ShowWindow(GetDlgItem(gifWindow, id), visible ? SW_SHOWNA : SW_HIDE);
    };
    MoveWindow(GetDlgItem(gifWindow, GIF_INFO), s(20), s(14), std::max(1, rw(r) - s(40)), s(48), TRUE);
    int choiceWidth = std::max(1, (rw(r) - s(40)) / 3);
    int choice = 0;
    for (UINT id : {(UINT)GIF_PROFILE, GIF_SHARP, GIF_SMALL}) {
        MoveWindow(GetDlgItem(gifWindow, id), s(20) + choice++ * choiceWidth, s(66), choiceWidth, s(26),
                   TRUE);
        show(id, !preview);
        EnableWindow(GetDlgItem(gifWindow, id), ready);
    }
    MoveWindow(GetDlgItem(gifWindow, GIF_CURSOR), s(20), s(98), s(180), s(28), TRUE);
    int bottom = rh(r) - s(48);
    int right = rw(r) - s(20);
    for (UINT id : {GIF_START, GIF_STOP, GIF_SAVE})
        MoveWindow(GetDlgItem(gifWindow, id), right - s(220), bottom, s(104), s(32), TRUE);
    MoveWindow(GetDlgItem(gifWindow, GIF_CANCEL), right - s(104), bottom, s(104), s(32), TRUE);
    MoveWindow(GetDlgItem(gifWindow, GIF_AGAIN), s(20), bottom, s(116), s(32), TRUE);
    MoveWindow(GetDlgItem(gifWindow, GIF_PLAY), s(148), bottom, s(104), s(32), TRUE);
    show(GIF_CURSOR, !preview);
    EnableWindow(GetDlgItem(gifWindow, GIF_CURSOR), ready);
    show(GIF_START, ready || gifState == GifState::Countdown);
    EnableWindow(GetDlgItem(gifWindow, GIF_START), ready);
    show(GIF_STOP, gifState == GifState::Recording || gifState == GifState::Finishing);
    EnableWindow(GetDlgItem(gifWindow, GIF_STOP), gifState == GifState::Recording);
    show(GIF_SAVE, preview);
    show(GIF_AGAIN, preview);
    show(GIF_PLAY, preview);
    SetWindowTextW(GetDlgItem(gifWindow, GIF_CANCEL), preview ? L"닫기" : L"취소");
    auto output = gifSize(rw(gifRect), rh(gifRect), gifProfile);
    std::wstring text;
    if (preview && gifSession) {
        std::wostringstream info;
        info << output.cx << L" × " << output.cy << L"  ·  " << std::fixed << std::setprecision(1)
             << gifSession->stats.duration / 1000. << L"초  ·  " << sizeText(gifSession->stats.bytes);
        text = info.str() + L"\n" + (gifNotice.empty() ? L"미리보기를 확인하고 저장하세요." : gifNotice);
    } else if (gifState == GifState::Countdown) {
        auto time = gifUiTime ? gifUiTime : GetTickCount64();
        auto remain = gifCountdown > time ? (gifCountdown - time + 999) / 1000 : 0;
        text = std::to_wstring(remain) + L"초 후 녹화합니다. 움직일 화면을 준비하세요.";
    } else if (gifState == GifState::Recording) {
        auto time = gifUiTime ? gifUiTime : GetTickCount64();
        auto elapsed = time >= gifStarted ? time - gifStarted : 0;
        text = L"녹화 중  " + std::to_wstring(elapsed / 1000) + L"초 / 120초  ·  " +
               sizeText(gifSession ? gifSession->bytes.load() : 0) + L"\n" +
               (gifStopKey ? std::wstring(L"F8 또는 ") : std::wstring{}) +
               hotkeyName(store.settings.modifiers, store.settings.key) + L": 정지 · " +
               (gifCancelKey ? L"Esc: 취소" : L"취소 버튼으로 취소");
    } else if (gifState == GifState::Finishing)
        text = L"GIF를 정리하고 있습니다. 잠시 기다려 주세요.";
    else
        text = L"영역 " + std::to_wstring(rw(gifRect)) + L" × " + std::to_wstring(rh(gifRect)) + L" → " +
               std::to_wstring(output.cx) + L" × " + std::to_wstring(output.cy) +
               L"\n최대 2분 · 화면 변화에 따라 파일 크기가 달라집니다.";
    SetWindowTextW(GetDlgItem(gifWindow, GIF_INFO), text.c_str());
    InvalidateRect(gifWindow, nullptr, FALSE);
}
void App::startGif(ULONGLONG now) {
    if (gifState != GifState::Ready)
        return;
    if (!now)
        now = GetTickCount64();
    gifProfile = SendMessageW(GetDlgItem(gifWindow, GIF_SHARP), BM_GETCHECK, 0, 0) == BST_CHECKED   ? 1
                 : SendMessageW(GetDlgItem(gifWindow, GIF_SMALL), BM_GETCHECK, 0, 0) == BST_CHECKED ? 2
                                                                                                    : 0;
    gifCursor = SendMessageW(GetDlgItem(gifWindow, GIF_CURSOR), BM_GETCHECK, 0, 0) == BST_CHECKED;
    gifOutput = gifSize(rw(gifRect), rh(gifRect), gifProfile);
    gifCancelled = false;
    gifCountdown = now + 3000;
    gifUiTime = now;
    gifState = GifState::Countdown;
    require(SetTimer(main, 7, 15, nullptr) != 0, "GIF recording timer unavailable");
    if (!testMode) {
        // The configured capture hotkey is always available even if F8/Esc are already in use.
        gifStopKey = RegisterHotKey(main, 67, MOD_NOREPEAT, VK_F8) != FALSE;
        gifCancelKey = RegisterHotKey(main, 68, MOD_NOREPEAT, VK_ESCAPE) != FALSE;
        if (!gifStopKey || !gifCancelKey)
            SetWindowTextW(
                gifWindow,
                (L"GIF 녹화 · 정지: " + hotkeyName(store.settings.modifiers, store.settings.key)).c_str());
    }
    layoutGif();
}
void App::tickGif(ULONGLONG now) {
    if (!now)
        now = GetTickCount64();
    bool refresh = now / 500 != gifUiTime / 500;
    gifUiTime = now;
    if (gifState == GifState::Countdown && refresh)
        layoutGif();
    if (gifState == GifState::Countdown && now >= gifCountdown) {
        if (!testMode) {
            RECT window{}, overlap{};
            GetWindowRect(gifWindow, &window);
            // Windows 11 excludes our controls; on a failed exclusion, hide only when overlapping.
            if (!gifExcluded && IntersectRect(&overlap, &window, &gifRect)) {
                require(hotkey || gifStopKey,
                        "GIF recording requires a stop hotkey when controls overlap capture");
                ShowWindow(gifWindow, SW_HIDE);
            }
            require(SUCCEEDED(DwmFlush()), "GIF recording compositor not ready");
            gifBuffer = std::make_unique<GifCaptureBuffer>(gifOutput.cx, gifOutput.cy);
        } else
            require((bool)gifFrameProvider, "test GIF frame provider missing");
        auto temporary = store.root / (L"gif-recording-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                       std::to_wstring(GetTickCount64()) + L".gif");
        gifSession = std::make_unique<GifSession>(temporary, gifOutput.cx, gifOutput.cy);
        gifStarted = now;
        gifLastFrame = 0;
        gifSamples = 0;
        gifState = GifState::Recording;
        if (!testMode && IsWindow(previous) && previous != main)
            SetForegroundWindow(previous);
        layoutGif();
    }
    if (gifState == GifState::Recording) {
        if (now - gifStarted >= GifDurationLimit || gifSession->limit) {
            gifNotice = gifSession->limit ? L"파일 크기가 80 MB에 도달해 녹화를 마쳤습니다."
                                          : L"최대 녹화 시간 2분에 도달했습니다.";
            stopGif(false, now);
        } else if (gifSession->done) {
            stopGif(false, now);
        } else if (now >= gifStarted + (ULONGLONG)gifSamples * 1000 / GifProfiles[gifProfile].fps) {
            auto frame = gifFrameProvider ? gifFrameProvider(gifRect, gifOutput, now)
                                          : gifBuffer->capture(gifRect, gifCursor, now);
            gifSession->submit(std::move(frame));
            gifLastFrame = now;
            gifSamples = (UINT)((now - gifStarted) * GifProfiles[gifProfile].fps / 1000) + 1;
        }
        // Refresh the status twice a second, without relaying out the entire window per frame.
        if (refresh)
            layoutGif();
    }
    if (gifState == GifState::Finishing && gifSession && gifSession->done) {
        auto error = gifSession->error;
        if (gifCancelled || !error.empty()) {
            closeGif();
            if (!error.empty()) {
                log(error);
                if (!testMode)
                    message(main,
                            L"GIF를 만들지 못했습니다. 저장 공간을 확인하고 다시 시도하세요.\n" +
                                std::wstring(error.begin(), error.end()),
                            true);
            }
            return;
        }
        gifPreview = std::make_unique<GifPreview>(gifSession->file);
        gifState = GifState::Preview;
        gifPlaying = true;
        KillTimer(main, 7);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromRect(&gifRect, MONITOR_DEFAULTTONEAREST), &mi);
        auto dpi = GetDpiForWindow(gifWindow);
        int w = std::min(MulDiv(920, dpi, 96), rw(mi.rcWork)),
            h = std::min(MulDiv(660, dpi, 96), rh(mi.rcWork));
        SetWindowLongPtrW(gifWindow, GWL_STYLE,
                          GetWindowLongPtrW(gifWindow, GWL_STYLE) | WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowPos(gifWindow, HWND_TOPMOST, mi.rcWork.left + (rw(mi.rcWork) - w) / 2,
                     mi.rcWork.top + (rh(mi.rcWork) - h) / 2, w, h, SWP_FRAMECHANGED | SWP_NOACTIVATE);
        SetWindowTextW(gifWindow, L"LightCapture · GIF 미리보기");
        layoutGif();
        if (!testMode) {
            ShowWindow(gifWindow, SW_SHOW);
            SetForegroundWindow(gifWindow);
        }
        SetTimer(gifWindow, 1, gifPreview->delay, nullptr);
    }
}
void App::stopGif(bool cancelled, ULONGLONG now) {
    if (!now)
        now = GetTickCount64();
    if (gifState == GifState::Closed)
        return;
    if (gifState == GifState::Ready || gifState == GifState::Countdown || gifState == GifState::Preview) {
        closeGif();
        return;
    }
    gifCancelled = gifCancelled || cancelled;
    gifUiTime = now;
    if (gifState == GifState::Recording)
        gifStopped = now;
    gifState = GifState::Finishing;
    gifBuffer.reset();
    if (!testMode) {
        UnregisterHotKey(main, 67);
        UnregisterHotKey(main, 68);
    }
    if (gifSession)
        gifSession->finish(now, cancelled);
    layoutGif();
}
void App::closeGif(bool restore) {
    KillTimer(main, 7);
    if (!testMode) {
        UnregisterHotKey(main, 67);
        UnregisterHotKey(main, 68);
    }
    gifPreview.reset();
    gifBuffer.reset();
    gifSession.reset();
    auto window = gifWindow;
    gifWindow = nullptr;
    gifState = GifState::Closed;
    gifRequested = false;
    if (window)
        DestroyWindow(window);
    busy = false;
    if (restore && previouslyVisible) {
        if (!testMode)
            show();
        else if (selected >= 0)
            select(selected);
    }
}
void App::saveGif() {
    if (gifState != GifState::Preview || !gifSession)
        return;
    bool playing = gifPlaying;
    KillTimer(gifWindow, 1);
    std::wstring destination;
    if (savePathPicker)
        destination = savePathPicker(L"LightCapture.gif");
    else {
        wchar_t path[32768] = L"LightCapture.gif";
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner = gifWindow;
        ofn.lpstrFilter = L"움직이는 GIF (*.gif)\0*.gif\0\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = 32768;
        ofn.lpstrDefExt = L"gif";
        ofn.lpstrInitialDir = lastSaveFolder.empty() ? nullptr : lastSaveFolder.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&ofn))
            destination = path;
    }
    if (!destination.empty()) {
        try {
            saveGifFile(gifSession->file, destination);
            lastSaveFolder = fs::path(destination).parent_path();
            gifNotice = L"저장 완료: " + fs::path(destination).filename().wstring();
        } catch (const std::exception &ex) {
            log(ex.what());
            gifNotice = L"저장하지 못했습니다. 다른 폴더를 골라 다시 저장하세요.";
            if (!testMode)
                message(gifWindow, gifNotice, true);
        }
        layoutGif();
    }
    if (playing && gifPreview && gifWindow)
        SetTimer(gifWindow, 1, gifPreview->delay, nullptr);
}
void App::paintGif(HDC dc) {
    RECT r{};
    GetClientRect(gifWindow, &r);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP buffer = CreateCompatibleBitmap(dc, std::max(1, rw(r)), std::max(1, rh(r)));
    auto old = SelectObject(memory, buffer);
    FillRect(memory, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    if (gifPreview && gifState == GifState::Preview) {
        int pad = MulDiv(20, GetDpiForWindow(gifWindow), 96);
        RECT area{pad, pad * 4, rw(r) - pad, rh(r) - pad * 3};
        HBRUSH background = CreateSolidBrush(RGB(237, 241, 246));
        FillRect(memory, &area, background);
        DeleteObject(background);
        double scale = std::min((double)rw(area) / gifPreview->width, (double)rh(area) / gifPreview->height);
        scale = std::min(1., std::max(.01, scale));
        int w = std::max(1, (int)std::lround(gifPreview->width * scale));
        int h = std::max(1, (int)std::lround(gifPreview->height * scale));
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER),
                          (LONG)gifPreview->width,
                          -(LONG)gifPreview->height,
                          1,
                          32,
                          BI_RGB,
                          0,
                          0,
                          0,
                          0,
                          0};
        SetStretchBltMode(memory, HALFTONE);
        SetBrushOrgEx(memory, 0, 0, nullptr);
        StretchDIBits(memory, area.left + (rw(area) - w) / 2, area.top + (rh(area) - h) / 2, w, h, 0, 0,
                      gifPreview->width, gifPreview->height, gifPreview->pixels.data(), &info, DIB_RGB_COLORS,
                      SRCCOPY);
    }
    BitBlt(dc, 0, 0, rw(r), rh(r), memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(buffer);
    DeleteDC(memory);
}
LRESULT CALLBACK App::gifProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT *)l)->lpCreateParams);
    auto a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        switch (m) {
        case WM_CREATE:
            control(h, L"STATIC", L"", SS_LEFT, 20, 14, 560, 48, GIF_INFO);
            for (auto item : {std::pair<UINT, LPCWSTR>{GIF_START, L"녹화 시작"},
                              {GIF_STOP, L"정지"},
                              {GIF_SAVE, L"저장"},
                              {GIF_CANCEL, L"취소"},
                              {GIF_AGAIN, L"다시 녹화"},
                              {GIF_PLAY, L"일시 정지"}}) {
                auto button =
                    control(h, L"BUTTON", item.second, WS_TABSTOP | BS_OWNERDRAW, 0, 0, 100, 32, item.first);
                SetWindowSubclass(button, toolbarProc, 1, (DWORD_PTR)a);
            }
            {
                UINT ids[] = {GIF_PROFILE, GIF_SHARP, GIF_SMALL};
                const wchar_t *labels[] = {L"표준 12fps / 1280px", L"선명 15fps / 1600px",
                                           L"작게 10fps / 960px"};
                for (int i = 0; i < 3; ++i) {
                    auto radio = control(h, L"BUTTON", labels[i],
                                         WS_TABSTOP | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
                                         20 + i * 200, 66, 200, 26, ids[i]);
                    SendMessageW(radio, BM_SETCHECK, i == a->gifProfile ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            }
            control(h, L"BUTTON", L"마우스 커서 포함", WS_TABSTOP | BS_AUTOCHECKBOX, 328, 66, 150, 28,
                    GIF_CURSOR);
            SendMessageW(GetDlgItem(h, GIF_CURSOR), BM_SETCHECK, a->gifCursor ? BST_CHECKED : BST_UNCHECKED,
                         0);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case GIF_START:
                a->startGif();
                break;
            case GIF_STOP:
                a->stopGif();
                break;
            case GIF_CANCEL:
                a->stopGif(true);
                break;
            case GIF_SAVE:
                a->saveGif();
                break;
            case GIF_AGAIN: {
                auto area = a->gifRect;
                a->closeGif(false);
                a->prepareGif(area);
                break;
            }
            case GIF_PLAY:
                a->gifPlaying = !a->gifPlaying;
                SetWindowTextW(GetDlgItem(h, GIF_PLAY), a->gifPlaying ? L"일시 정지" : L"재생");
                if (a->gifPlaying)
                    SetTimer(h, 1, a->gifPreview->delay, nullptr);
                else
                    KillTimer(h, 1);
                break;
            case GIF_PROFILE:
            case GIF_SHARP:
            case GIF_SMALL:
                if (HIWORD(w) == BN_CLICKED) {
                    a->gifProfile = LOWORD(w) == GIF_SHARP ? 1 : LOWORD(w) == GIF_SMALL ? 2 : 0;
                    a->layoutGif();
                }
                break;
            }
            return 0;
        case WM_TIMER:
            if (w == 1 && a->gifPreview && a->gifPlaying) {
                a->gifPreview->advance();
                SetTimer(h, 1, a->gifPreview->delay, nullptr);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_DRAWITEM:
            a->paintActionButton((DRAWITEMSTRUCT *)l);
            return TRUE;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor((HDC)w, RGB(255, 255, 255));
            SetTextColor((HDC)w, RGB(23, 32, 51));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            auto dc = BeginPaint(h, &ps);
            a->paintGif(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            a->paintGif((HDC)w);
            return 0;
        case WM_SIZE:
            if (a->gifWindow)
                a->layoutGif();
            return 0;
        case WM_GETMINMAXINFO: {
            auto mm = (MINMAXINFO *)l;
            UINT dpi = GetDpiForWindow(h);
            mm->ptMinTrackSize = {MulDiv(640, dpi, 96),
                                  MulDiv(a->gifState == GifState::Preview ? 440 : 220, dpi, 96)};
            return 0;
        }
        case WM_CLOSE:
            a->stopGif(true);
            return 0;
        }
    } catch (const std::exception &ex) {
        a->log(ex.what());
        a->closeGif();
        a->report(ex);
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace lc
