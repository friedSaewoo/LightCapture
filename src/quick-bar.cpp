#include "app.h"
namespace lc {
void App::createQuickBar(HWND parent) {
    UINT dpi = GetDpiForWindow(parent);
    if (!dpi)
        dpi = 96;
    auto s = [&](int v) { return MulDiv(v, dpi, 96); };
    RECT bounds{};
    GetClientRect(parent, &bounds);
    int width = std::min(rw(bounds), s(590)), x = (rw(bounds) - width) / 2, y = s(14);
    if (parent == overlay && !testMode) {
        POINT mouse{};
        GetCursorPos(&mouse);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(mouse, MONITOR_DEFAULTTONEAREST), &mi);
        x = mi.rcWork.left - virtualRect.left + (rw(mi.rcWork) - width) / 2;
        y = mi.rcWork.top - virtualRect.top + s(14);
        x = std::clamp(x, 0, std::max(0, rw(bounds) - width));
    }
    quickBar = CreateWindowExW(WS_EX_CONTROLPARENT, L"LCQuickBar", L"캡처 작업",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, x, y, width, s(50), parent, nullptr,
                               instance, this);
    require(quickBar != nullptr, "capture toolbar creation failed");
    if (quickFont)
        DeleteObject(quickFont);
    quickFont =
        CreateFontW(-s(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
    struct Button {
        UINT id;
        const wchar_t *label;
        int width;
    };
    Button buttons[] = {{ID_CAPTURE, L"영역", 64},   {ID_WINDOW, L"창", 54},
                        {ID_FULL, L"전체 화면", 94}, {ID_RESTORE, L"최근 목록", 104},
                        {ID_FIXED, L"더 보기", 88},  {ID_HIDE, L"취소 · Esc", 106}};
    int bx = s(10);
    for (auto &button : buttons) {
        DWORD style =
            button.id == ID_CAPTURE || button.id == ID_WINDOW ? BS_CHECKBOX | BS_PUSHLIKE : BS_PUSHBUTTON;
        auto h =
            CreateWindowExW(0, L"BUTTON", button.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, bx, s(9),
                            s(button.width), s(32), quickBar, (HMENU)(UINT_PTR)button.id, instance, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)quickFont, TRUE);
        bx += s(button.width + 8);
    }
    SendMessageW(GetDlgItem(quickBar, ID_CAPTURE), BM_SETCHECK,
                 mode == Mode::Rectangle ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(quickBar, ID_WINDOW), BM_SETCHECK,
                 mode == Mode::Window ? BST_CHECKED : BST_UNCHECKED, 0);
}
void App::openHistoryFromCapture() {
    previouslyVisible = true;
    previous = nullptr;
    cancelCapture();
    if (!testMode)
        SetFocus(list);
    statusText(L"최근 캡처 목록 · 항목을 선택해 편집하거나 저장하세요.");
}
LRESULT CALLBACK App::quickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT *)l)->lpCreateParams);
    auto a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        if (m == WM_SETCURSOR) {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (m == WM_CLOSE) {
            a->cancelCapture();
            return 0;
        }
        if (m == WM_COMMAND) {
            UINT id = LOWORD(w);
            if (id == ID_RESTORE) {
                a->openHistoryFromCapture();
                return 0;
            }
            if (id == ID_HIDE) {
                a->cancelCapture();
                return 0;
            }
            if (id == ID_FIXED) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, ID_FREE, L"자유형 캡처");
                AppendMenuW(menu, MF_STRING, ID_UNIT, L"단위영역 캡처");
                AppendMenuW(menu, MF_STRING, ID_SCROLL, L"스크롤 캡처");
                AppendMenuW(menu, MF_STRING, ID_FIXED, L"크기 지정 캡처");
                RECT rect{};
                GetWindowRect(GetDlgItem(h, ID_FIXED), &rect);
                UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, rect.left, rect.bottom, 0,
                                             h, nullptr);
                DestroyMenu(menu);
                if (!chosen)
                    return 0;
                id = chosen;
            }
            if (id >= ID_CAPTURE && id <= ID_FIXED)
                a->switchCaptureMode((Mode)(id - ID_CAPTURE));
            return 0;
        }
        if (m == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT r;
            GetClientRect(h, &r);
            FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            FrameRect(dc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
            EndPaint(h, &ps);
            return 0;
        }
        if (m == WM_PRINTCLIENT) {
            RECT r;
            GetClientRect(h, &r);
            FillRect((HDC)w, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            FrameRect((HDC)w, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
            return 0;
        }
    } catch (const std::exception &e) {
        a->cancelCapture();
        a->report(e);
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace lc
