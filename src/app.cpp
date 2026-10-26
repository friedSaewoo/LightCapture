#include "app.h"
#include "toolbar.h"
#include "theme.h"
#include <sstream>
namespace lc {
App *app = nullptr;
HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
             UINT id) {
    UINT dpi = GetDpiForWindow(parent);
    if (!dpi)
        dpi = 96;
    auto scale = [&](int v) { return MulDiv(v, dpi, 96); };
    auto c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text,
                             WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(w), scale(h), parent,
                             (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SetPropW(c, L"LCBaseWidth", (HANDLE)(INT_PTR)w);
    SendMessageW(c, WM_SETFONT, (WPARAM)(app ? app->uiFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}
std::wstring hotkeyName(UINT mods, UINT key) {
    std::wstring s;
    if (mods & MOD_CONTROL)
        s += L"Ctrl + ";
    if (mods & MOD_SHIFT)
        s += L"Shift + ";
    if (mods & MOD_ALT)
        s += L"Alt + ";
    if (mods & MOD_WIN)
        s += L"Win + ";
    wchar_t name[100]{};
    GetKeyNameTextW(MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16, name, 100);
    return s + name;
}
struct Prompt {
    std::wstring value;
    bool done = false, ok = false, multiline = false;
    HWND edit = nullptr;
};
static LRESULT CALLBACK promptProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto p = (Prompt *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        p = (Prompt *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
    }
    if (!p)
        return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_CREATE:
        p->edit =
            control(h, L"EDIT", p->value.c_str(),
                    WS_TABSTOP | ES_AUTOHSCROLL |
                        (p->multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL) : 0),
                    16, 16, 480, p->multiline ? 170 : 28, 10);
        control(h, L"BUTTON", L"확인", WS_TABSTOP | BS_DEFPUSHBUTTON, 310, p->multiline ? 204 : 62, 88, 30,
                IDOK);
        control(h, L"BUTTON", L"취소", WS_TABSTOP, 408, p->multiline ? 204 : 62, 88, 30, IDCANCEL);
        SetFocus(p->edit);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            int n = GetWindowTextLengthW(p->edit);
            p->value.resize(n + 1);
            GetWindowTextW(p->edit, p->value.data(), n + 1);
            p->value.resize(n);
            p->ok = true;
            p->done = true;
        } else if (LOWORD(w) == IDCANCEL)
            p->done = true;
        return 0;
    case WM_CLOSE:
        p->done = true;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
bool textPrompt(HWND owner, const wchar_t *title, std::wstring &value, bool multi) {
    Prompt p;
    p.value = value;
    p.multiline = multi;
    RECT r;
    GetWindowRect(owner, &r);
    auto h =
        CreateWindowExW(WS_EX_DLGMODALFRAME, L"LCPrompt", title, WS_CAPTION | WS_SYSMENU, r.left + 80,
                        r.top + 120, 530, multi ? 290 : 150, owner, nullptr, GetModuleHandleW(nullptr), &p);
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SetFocus(p.edit);
    MSG msg;
    bool captureRequested = false;
    while (!p.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && app && (UINT)msg.wParam == app->hotId) {
            captureRequested = true;
            p.done = true;
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            p.done = true;
            continue;
        }
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(h);
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    if (p.ok)
        value = p.value;
    if (captureRequested && app)
        PostMessageW(app->main, WM_COMMAND, ID_CAPTURE, 0);
    return p.ok;
}
void App::log(const std::string &s) {
    std::ofstream f(store.root / L"diagnostics.log", std::ios::app);
    f << GetTickCount64() << " " << s << "\n";
}
void App::report(const std::exception &ex) {
    log(ex.what());
    if (testMode) {
        PostMessageW(main, WM_COMMAND, ID_QUIT, 0);
        return;
    }
    std::string s = ex.what();
    std::wstring ws(s.begin(), s.end());
    statusText(L"작업 실패: " + ws);
    message(main,
            L"작업을 완료하지 못했습니다. 내부 보관함과 기존 파일은 유지됩니다.\n\n" + ws +
                L"\n\n보관 폴더의 diagnostics.log를 확인하세요.",
            true);
}
bool App::registerKey(UINT mods, UINT key, bool notify) {
    UINT next = hotId == 1 ? 2 : 1;
    if (!RegisterHotKey(main, next, mods | MOD_NOREPEAT, key)) {
        if (notify)
            message(main,
                    L"단축키 " + hotkeyName(mods, key) +
                        L"를 등록하지 못했습니다. 다른 앱의 사용 여부를 확인하세요.\n기존 단축키 설정을 "
                        L"유지합니다.\n" +
                        errorText(GetLastError()),
                    true);
        return false;
    }
    if (hotkey)
        UnregisterHotKey(main, hotId);
    hotId = next;
    hotkey = true;
    return true;
}
void App::statusText(const std::wstring &s) {
    if (status)
        SendMessageW(status, SB_SETTEXTW, 0, (LPARAM)s.c_str());
}
void App::updateTitle() {
    std::wstring title = L"LightCapture · " + hotkeyName(store.settings.modifiers, store.settings.key);
    if (doc()) {
        auto r = viewRect(*doc());
        title += L" · " + std::to_wstring(rw(r)) + L" × " + std::to_wstring(rh(r)) + L" · " +
                 std::to_wstring((int)std::lround(zoom * 100)) + L"%";
    }
    SetWindowTextW(main, title.c_str());
}
void App::notifyTray() {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = main;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_TRAY;
    tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(2));
    auto tip = L"LightCapture · " + hotkeyName(store.settings.modifiers, store.settings.key);
    wcsncpy_s(tray.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &tray);
    tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray);
}
void App::show() {
    if (busy)
        return;
    BOOL transitionsDisabled = FALSE;
    DwmSetWindowAttribute(main, DWMWA_TRANSITIONS_FORCEDISABLED, &transitionsDisabled,
                          sizeof(transitionsDisabled));
    ShowOwnedPopups(main, TRUE);
    if (!original && selected >= 0)
        select(selected);
    ShowWindow(main, SW_RESTORE);
    SetForegroundWindow(main);
    SetFocus(canvas);
}
void App::hide() {
    finishText();
    clearPixelSelection();
    saveView();
    ShowWindow(main, SW_HIDE);
    original.reset();
    rendered.reset();
    thumbs.clear();
    store.releaseEdits();
    dragging = false;
    panning = false;
}
void App::saveView() {
    store.settings.color = color;
    store.settings.tool = (int)tool;
    store.settings.stroke = (int)stroke;
    store.settings.font = fontName;
    store.settings.fontSize = (int)fontSize;
    store.settings.fontStyle = textStyle;
    store.settings.fill = fill;
    store.saveSettings();
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(main, &wp);
    auto p = store.root / L"view.ini.partial";
    auto write = [&](const wchar_t *key, const std::wstring &val) {
        require(WritePrivateProfileStringW(L"View", key, val.c_str(), p.c_str()), "view save failed");
    };
    if (doc())
        write(L"selected", doc()->id);
    write(L"x", std::to_wstring(wp.rcNormalPosition.left));
    write(L"y", std::to_wstring(wp.rcNormalPosition.top));
    write(L"w", std::to_wstring(rw(wp.rcNormalPosition)));
    write(L"h", std::to_wstring(rh(wp.rcNormalPosition)));
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, p.c_str());
    atomicReplace(p, store.root / L"view.ini");
}
void App::trayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_CAPTURE, L"영역 캡처");
    AppendMenuW(menu, MF_STRING, ID_RESTORE, L"편집창 / 최근 목록");
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"환경설정");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_QUIT, L"프로그램 종료");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(main);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, main, nullptr);
    DestroyMenu(menu);
    if (id)
        command(id);
    PostMessageW(main, WM_NULL, 0, 0);
}
static void item(HMENU m, UINT id, const wchar_t *name) {
    AppendMenuW(m, MF_STRING, id, name);
}
void App::create(HINSTANCE inst, bool hidden) {
    instance = inst;
    color = store.settings.color;
    tool = (Tool)store.settings.tool;
    stroke = store.settings.stroke;
    fontName = store.settings.font;
    fontSize = store.settings.fontSize;
    textStyle = store.settings.fontStyle;
    fill = store.settings.fill;
    uiFont = CreateFontW(-MulDiv(14, GetDpiForSystem(), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"맑은 고딕");
    auto reg = [&](const wchar_t *name, WNDPROC proc, HBRUSH brush) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.lpszClassName = name;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.style = CS_DBLCLKS;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(2));
        wc.hbrBackground = brush;
        require(RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "window class failed");
    };
    reg(L"LCMain", mainProc, (HBRUSH)(COLOR_WINDOW + 1));
    reg(L"LCCanvas", canvasProc, nullptr);
    reg(L"LCOverlay", overlayProc, nullptr);
    reg(L"LCQuickBar", quickProc, (HBRUSH)(COLOR_WINDOW + 1));
    reg(L"LCScroll", scrollProc, (HBRUSH)(COLOR_WINDOW + 1));
    reg(L"LCPrompt", promptProc, (HBRUSH)(COLOR_WINDOW + 1));
    extern LRESULT CALLBACK settingsProc(HWND, UINT, WPARAM, LPARAM);
    reg(L"LCSettings", settingsProc, (HBRUSH)(COLOR_WINDOW + 1));
    main = CreateWindowExW(0, L"LCMain", AppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                           CW_USEDEFAULT, 1360, 860, nullptr, nullptr, inst, this);
    require(main != nullptr, "main window failed");
    taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    notifyTray();
    hotkey = registerKey(store.settings.modifiers, store.settings.key, false);
    log(hotkey ? "hotkey registration OK" : "hotkey registration FAILED (conflict or OS restriction)");
    if (!hotkey) {
        statusText(L"단축키 충돌: 환경설정에서 변경하거나 다른 캡처 앱을 종료하세요.");
        if (!testMode)
            PostMessageW(main, WM_COMMAND, ID_SETTINGS, 0);
    }
    ready = true;
    if (!store.docs.empty()) {
        selected = 0;
        wchar_t id[256]{};
        auto p = store.root / L"view.ini";
        GetPrivateProfileStringW(L"View", L"selected", L"", id, 256, p.c_str());
        for (int i = 0; i < (int)store.docs.size(); i++)
            if (store.docs[i].id == id) {
                selected = i;
                break;
            }
    }
    {
        auto p = store.root / L"view.ini";
        int x = GetPrivateProfileIntW(L"View", L"x", 100, p.c_str()),
            y = GetPrivateProfileIntW(L"View", L"y", 100, p.c_str());
        int w = GetPrivateProfileIntW(L"View", L"w", 1360, p.c_str()),
            hh = GetPrivateProfileIntW(L"View", L"h", 860, p.c_str());
        RECT r{x, y, x + std::clamp(w, 720, 8000), y + std::clamp(hh, 480, 8000)};
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
        w = std::min(rw(r), rw(mi.rcWork));
        hh = std::min(rh(r), rh(mi.rcWork));
        x = std::clamp(x, (int)mi.rcWork.left, (int)mi.rcWork.right - w);
        y = std::clamp(y, (int)mi.rcWork.top, (int)mi.rcWork.bottom - hh);
        SetWindowPos(main, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    refreshList();
    if (!hidden)
        show();
    updateTitle();
}
void App::layout() {
    RECT r;
    GetClientRect(main, &r);
    SendMessageW(status, WM_SIZE, 0, 0);
    UINT dpi = GetDpiForWindow(main);
    if (!dpi)
        dpi = 96;
    auto scale = [&](int v) { return MulDiv(v, dpi, 96); };
    int top = layoutToolbar(rw(r), dpi);
    int sidebarWidth = scale(std::clamp(MulDiv(rw(r), 96, dpi) * 27 / 100, 296, 380) * 2 / 3);
    int sidebar = rw(r) - sidebarWidth;
    int actionWidth = (sidebarWidth - scale(32)) / 2;
    MoveWindow(historyDelete, sidebar + scale(12), top + scale(12), actionWidth, scale(36), TRUE);
    MoveWindow(historySave, sidebar + scale(20) + actionWidth, top + scale(12), actionWidth, scale(36), TRUE);
    MoveWindow(historyCheckAll, sidebar + scale(12), top + scale(60), scale(100), scale(26), TRUE);
    MoveWindow(historyCheckCount, sidebar + sidebarWidth - scale(82), top + scale(63), scale(70), scale(24),
               TRUE);
    int listHeight = std::max(1, rh(r) - top - scale(96 + 32));
    MoveWindow(list, sidebar + scale(8), top + scale(96), sidebarWidth - scale(16), listHeight, TRUE);
    SendMessageW(list, LB_SETITEMHEIGHT, 0, std::min(scale(174), std::max(scale(116), listHeight)));
    MoveWindow(canvas, 0, top, std::max(1, sidebar), std::max(1, rh(r) - top - scale(26)), TRUE);
}
void App::command(UINT id) {
    if (id == 181) {
        HMENU menu = CreatePopupMenu();
        int widths[] = {1, 2, 3, 5, 8, 12, 20};
        for (int i = 0; i < 7; ++i) {
            auto label = std::to_wstring(widths[i]) + L" px";
            AppendMenuW(menu, MF_STRING | ((int)stroke == widths[i] ? MF_CHECKED : 0), 400 + i,
                        label.c_str());
        }
        RECT anchor{};
        GetWindowRect(widthControl, &anchor);
        UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD, anchor.left, anchor.bottom, 0, main, nullptr);
        DestroyMenu(menu);
        if (chosen >= 400 && chosen < 407) {
            stroke = widths[chosen - 400];
            SetWindowTextW(widthControl, (std::to_wstring((int)stroke) + L" px").c_str());
        }
        return;
    }
    if (id >= ID_PALETTE && id < ID_PALETTE + PaletteCount) {
        setColor(Palette[id - ID_PALETTE].value);
        return;
    }
    if (id != ID_FONT && id != ID_COLOR && id != ID_FILL)
        finishText();
    if (pixelSelecting || pixelMoving) {
        if (id == ID_UNDO) {
            clearPixelSelection();
            return;
        }
        pixelUp();
    }
    if (id >= ID_TOOL && id <= ID_TOOL + (UINT)Tool::PixelSelect && id != ID_TOOL + (UINT)Tool::Reset) {
        setTool((Tool)(id - ID_TOOL));
        return;
    }
    switch (id) {
    case ID_CAPTURE:
        begin(Mode::Rectangle);
        break;
    case ID_FREE:
        begin(Mode::Freeform);
        break;
    case ID_WINDOW:
        begin(Mode::Window);
        break;
    case ID_UNIT:
        begin(Mode::Unit);
        break;
    case ID_FULL:
        currentMonitor = false;
        begin(Mode::Fullscreen);
        break;
    case ID_MONITOR:
        currentMonitor = true;
        begin(Mode::Fullscreen);
        break;
    case ID_SCROLL:
        scrollHorizontal = false;
        scrollManual = false;
        begin(Mode::Scroll);
        break;
    case ID_SCROLLH:
        scrollHorizontal = true;
        scrollManual = false;
        begin(Mode::Scroll);
        break;
    case ID_SCROLLMANUAL:
        scrollHorizontal = false;
        scrollManual = true;
        begin(Mode::Scroll);
        break;
    case ID_FIXED:
        begin(Mode::Fixed);
        break;
    case ID_OPEN:
        open();
        break;
    case ID_COPY:
        copy();
        break;
    case ID_SAVE:
        saveChecked();
        break;
    case ID_SAVEAS:
        save(true);
        break;
    case ID_CHECK_ALL:
        checkAllHistory();
        break;
    case ID_HISTORY_DELETE:
        deleteCheckedHistory();
        break;
    case ID_PRINT:
        print();
        break;
    case ID_EXTERNAL:
        if (doc() && original) {
            if (store.settings.external.empty()) {
                message(main, L"환경설정에서 외부 편집기 실행 파일을 먼저 지정하세요.");
                break;
            }
            auto target = store.root / (L"external_" + doc()->id + L".png");
            saveImage(*output(*original, *doc()), target);
            auto args = L"\"" + target.wstring() + L"\"";
            auto result = (INT_PTR)ShellExecuteW(main, L"open", store.settings.external.c_str(), args.c_str(),
                                                 nullptr, SW_SHOWNORMAL);
            require(result > 32, "external editor failed");
        }
        break;
    case ID_UNDO:
        if (!busy)
            undo();
        break;
    case ID_REDO:
        undo(true);
        break;
    case ID_ORIGINAL:
        if (doc() && MessageBoxW(main, L"현재 캡처의 주석과 자르기를 해제하고 원본 상태로 되돌릴까요?",
                                 AppName, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            Command reset;
            reset.tool = Tool::Reset;
            edit(reset);
        }
        break;
    case ID_DELETE:
        if (GetFocus() == canvas && tool == Tool::Select && selectedObject >= 0)
            deleteObject();
        else
            deleteSelected();
        break;
    case ID_DUPLICATE:
        duplicateObject();
        break;
    case ID_FONT: {
        LOGFONTW lf{};
        lf.lfHeight = -(int)fontSize;
        lf.lfWeight = (textStyle & Gdiplus::FontStyleBold) ? FW_BOLD : FW_NORMAL;
        lf.lfItalic = (textStyle & Gdiplus::FontStyleItalic) != 0;
        lf.lfUnderline = (textStyle & Gdiplus::FontStyleUnderline) != 0;
        lf.lfStrikeOut = (textStyle & Gdiplus::FontStyleStrikeout) != 0;
        wcsncpy_s(lf.lfFaceName, fontName.c_str(), _TRUNCATE);
        CHOOSEFONTW cf{sizeof(cf)};
        cf.hwndOwner = main;
        cf.lpLogFont = &lf;
        cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
        if (ChooseFontW(&cf)) {
            fontName = lf.lfFaceName;
            fontSize = std::clamp((float)std::abs(lf.lfHeight), 1.f, 200.f);
            textStyle = (lf.lfWeight >= FW_BOLD ? Gdiplus::FontStyleBold : 0) |
                        (lf.lfItalic ? Gdiplus::FontStyleItalic : 0) |
                        (lf.lfUnderline ? Gdiplus::FontStyleUnderline : 0) |
                        (lf.lfStrikeOut ? Gdiplus::FontStyleStrikeout : 0);
        }
        if (textInput) {
            textDraft.font = fontName;
            textDraft.fontSize = fontSize;
            textDraft.fontStyle = textStyle;
            updateTextStyle();
            growText();
            if (!testMode)
                SetFocus(textInput);
        }
        break;
    }
    case ID_FILL:
        fill = SendMessageW(GetDlgItem(main, ID_FILL), BM_GETCHECK, 0, 0) == BST_CHECKED;
        break;
    case ID_ZOOMIN:
        setZoom(zoom * 1.25f);
        break;
    case ID_ZOOMOUT:
        setZoom(zoom / 1.25f);
        break;
    case ID_ACTUAL:
        setZoom(1);
        break;
    case ID_FIT:
        if (doc()) {
            RECT r;
            GetClientRect(canvas, &r);
            auto v = viewRect(*doc());
            setZoom(std::min((rw(r) - 48.f) / rw(v), (rh(r) - 48.f) / rh(v)));
        }
        break;
    case ID_FOLDER:
        ShellExecuteW(main, L"open", store.root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case ID_SETTINGS:
        settingsDialog();
        break;
    case ID_RESTORE:
        show();
        break;
    case ID_HIDE:
        hide();
        break;
    case ID_QUIT:
        quit = true;
        DestroyWindow(main);
        break;
    case ID_ABOUT:
        message(main, L"LightCapture 0.1.15\n전역 캡처 · 통합 편집 · 최근 캡처\n\nCtrl + Shift + X "
                      L"(환경설정에서 변경)\n닫기: 트레이 대기 / 파일 메뉴: 프로그램 종료\n\n성능·알캡처 "
                      L"동등성 전체 검증은 아직 완료되지 않았습니다.");
        break;
    }
}
LRESULT CALLBACK App::mainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        a = (App *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
        a->main = h;
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        if (m == a->taskbarMessage && a->taskbarMessage) {
            a->notifyTray();
            return 0;
        }
        switch (m) {
        case WM_CREATE: {
            HMENU menu = CreateMenu(), file = CreatePopupMenu(), capture = CreatePopupMenu(),
                  edit = CreatePopupMenu(), view = CreatePopupMenu();
            item(file, ID_OPEN, L"이미지 열기...\tCtrl+O");
            item(file, ID_SAVE, L"체크한 캡처 저장...\tCtrl+S");
            item(file, ID_SAVEAS, L"다른 이름으로 저장...\tCtrl+Shift+S");
            item(file, ID_PRINT, L"인쇄...\tCtrl+P");
            item(file, ID_EXTERNAL, L"외부 편집기에서 열기");
            item(file, ID_FOLDER, L"보관 폴더 열기");
            AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
            item(file, ID_HIDE, L"트레이로 숨기기");
            item(file, ID_QUIT, L"프로그램 종료");
            const wchar_t *modes[] = {L"사각형 영역", L"자유형", L"창",       L"단위영역",
                                      L"전체 화면",   L"스크롤", L"크기 지정"};
            for (int i = 0; i < 7; i++)
                item(capture, ID_CAPTURE + i, modes[i]);
            item(capture, ID_SCROLLH, L"가로 스크롤 캡처");
            item(capture, ID_SCROLLMANUAL, L"수동 스크롤 캡처");
            item(capture, ID_MONITOR, L"마우스가 있는 모니터 캡처");
            item(edit, ID_COPY, L"이미지 복사\tCtrl+C");
            item(edit, ID_ORIGINAL, L"원본으로 복원...");
            item(edit, ID_DELETE, L"체크한 캡처 삭제...\tDelete");
            item(edit, ID_DUPLICATE, L"선택 개체 복제\tCtrl+D");
            item(edit, ID_FONT, L"글꼴...");
            item(view, ID_FIT, L"화면에 맞춤");
            item(view, ID_ACTUAL, L"실제 크기");
            item(view, ID_ZOOMIN, L"확대");
            item(view, ID_ZOOMOUT, L"축소");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"파일");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)capture, L"캡처");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)edit, L"편집");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"보기");
            item(menu, ID_SETTINGS, L"환경설정");
            item(menu, ID_ABOUT, L"정보");
            SetMenu(h, menu);
            a->createToolbar();
            a->createHistoryActions();
            a->list = control(h, L"LISTBOX", L"최근 캡처",
                              WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                                  LBS_NOINTEGRALHEIGHT,
                              0, 0, 192, 600, ID_LIST);
            SendMessageW(a->list, LB_SETITEMHEIGHT, 0, 116);
            SetWindowSubclass(a->list, listProc, 1, (DWORD_PTR)a);
            a->canvas = CreateWindowExW(0, L"LCCanvas", L"편집 화면", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 194,
                                        90, 960, 600, h, (HMENU)ID_CANVAS, a->instance, a);
            a->status = CreateWindowExW(0, STATUSCLASSNAMEW, L"준비", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h,
                                        nullptr, a->instance, nullptr);
            a->layout();
            return 0;
        }
        case WM_ERASEBKGND:
            a->paintChrome((HDC)w);
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            a->paintChrome(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            a->paintChrome((HDC)w);
            return 0;
        case WM_SIZE:
            a->layout();
            return 0;
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
            if ((HWND)l == GetDlgItem(h, ID_FILL) || (HWND)l == a->historyCheckAll ||
                (HWND)l == a->historyCheckCount) {
                SetBkMode((HDC)w, OPAQUE);
                COLORREF background =
                    (HWND)l == GetDlgItem(h, ID_FILL) ? RGB(255, 255, 255) : RGB(248, 250, 253);
                SetBkColor((HDC)w, background);
                SetTextColor((HDC)w, GetSysColor(COLOR_WINDOWTEXT));
                SetDCBrushColor((HDC)w, background);
                return (LRESULT)GetStockObject(DC_BRUSH);
            }
            break;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO *)l)->ptMinTrackSize = {720, 480};
            return 0;
        case WM_DRAWITEM:
            if ((w >= ID_TOOL && w <= ID_TOOL + (UINT)Tool::PixelSelect) ||
                (w >= ID_PALETTE && w < ID_PALETTE + PaletteCount) || w == ID_COLOR) {
                a->paintToolbarButton((DRAWITEMSTRUCT *)l);
                return TRUE;
            }
            if (w == ID_LIST) {
                a->paintList((DRAWITEMSTRUCT *)l);
                return TRUE;
            } else {
                a->paintActionButton((DRAWITEMSTRUCT *)l);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(w) == ID_LIST && HIWORD(w) == LBN_SELCHANGE) {
                int i = SendMessageW(a->list, LB_GETCURSEL, 0, 0);
                a->lastExport.clear();
                a->select(i);
            } else if (LOWORD(w) == 181 && HIWORD(w) == CBN_SELCHANGE) {
                int values[] = {1, 2, 3, 5, 8, 12, 20};
                int i = SendMessageW(a->widthControl, CB_GETCURSEL, 0, 0);
                if (i >= 0 && i < 7)
                    a->stroke = values[i];
            } else
                a->command(LOWORD(w));
            return 0;
        case WM_HOTKEY:
            if (w == 66 && a->scrollBar) {
                a->stopScroll(true);
                return 0;
            }
            if ((UINT)w == a->hotId) {
                if (a->busy) {
                    if (a->scrollBar)
                        a->stopScroll(true, false);
                    else
                        a->cancelCapture(false);
                    a->statusText(L"캡처를 취소했습니다.");
                } else
                    a->begin(Mode::Rectangle);
            }
            return 0;
        case WM_SECOND:
            a->show();
            return 0;
        case WM_TRAY:
            if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == NIN_SELECT)
                a->show();
            else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU)
                a->trayMenu();
            return 0;
        case WM_TIMER:
            if (w == 3 || w == 5)
                a->pollCaptureStart();
            else if (w == 4)
                a->scrollTick();
            else if (w == 99) {
                KillTimer(h, 99);
                a->quit = true;
                DestroyWindow(h);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            if (a->overlay)
                a->cancelCapture();
            return 0;
        case WM_DPICHANGED: {
            auto r = (RECT *)l;
            if (a->uiFont)
                DeleteObject(a->uiFont);
            a->uiFont = CreateFontW(-MulDiv(14, HIWORD(w), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
            for (auto [id, c] : a->topControls)
                SendMessageW(c, WM_SETFONT, (WPARAM)a->uiFont, TRUE);
            SendMessageW(a->list, WM_SETFONT, (WPARAM)a->uiFont, TRUE);
            for (auto h : {a->historySave, a->historyDelete, a->historyCheckAll, a->historyCheckCount})
                SendMessageW(h, WM_SETFONT, (WPARAM)a->uiFont, TRUE);
            a->thumbs.clear();
            SetWindowPos(h, nullptr, r->left, r->top, rw(*r), rh(*r), SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_CLOSE:
            a->hide();
            return 0;
        case WM_DESTROY:
            try {
                a->saveView();
            } catch (...) {
                a->log("view save failed on exit");
            }
            KillTimer(h, 3);
            KillTimer(h, 5);
            KillTimer(h, 4);
            if (a->overlay)
                DestroyWindow(a->overlay);
            if (a->scrollBar)
                DestroyWindow(a->scrollBar);
            if (a->hotkey)
                UnregisterHotKey(h, a->hotId);
            Shell_NotifyIconW(NIM_DELETE, &a->tray);
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception &e) {
        if (a->busy) {
            if (a->scrollBar)
                a->stopScroll(true);
            else
                a->cancelCapture();
        }
        a->report(e);
    }
    return DefWindowProcW(h, m, w, l);
}

struct SettingsUI {
    App *a;
    Settings pending;
    bool done = false;
    HWND hot = nullptr;
};
static std::wstring value(HWND h, int id) {
    auto c = GetDlgItem(h, id);
    int n = GetWindowTextLengthW(c);
    std::wstring s(n + 1, 0);
    GetWindowTextW(c, s.data(), n + 1);
    s.resize(n);
    return s;
}
void App::startup(bool enabled) {
    HKEY key;
    require(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                            nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS,
            "startup registry unavailable");
    auto cmd = L"\"" + modulePath().wstring() + L"\" --tray";
    LONG result = enabled ? RegSetValueExW(key, L"LightCapture", 0, REG_SZ, (BYTE *)cmd.c_str(),
                                           (cmd.size() + 1) * sizeof(wchar_t))
                          : RegDeleteValueW(key, L"LightCapture");
    RegCloseKey(key);
    require(result == ERROR_SUCCESS || (!enabled && result == ERROR_FILE_NOT_FOUND),
            "startup setting failed");
}
LRESULT CALLBACK settingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto ui = (SettingsUI *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        ui = (SettingsUI *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)ui);
    }
    if (!ui)
        return DefWindowProcW(h, m, w, l);
    try {
        switch (m) {
        case WM_CREATE: {
            auto label = [&](const wchar_t *text, int y) {
                if (y >= 226)
                    y += 30;
                control(h, L"STATIC", text, 0, 20, y, 180, 23, 0);
            };
            auto edit = [&](int id, const std::wstring &text, int y, int width = 330) {
                if (y >= 226)
                    y += 30;
                return control(h, L"EDIT", text.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 200, y - 3, width, 26,
                               id);
            };
            label(L"영역 캡처 전역 단축키", 24);
            ui->hot = control(h, HOTKEY_CLASSW, L"", WS_TABSTOP, 200, 20, 330, 28, 10);
            BYTE mods = 0;
            if (ui->pending.modifiers & MOD_CONTROL)
                mods |= HOTKEYF_CONTROL;
            if (ui->pending.modifiers & MOD_SHIFT)
                mods |= HOTKEYF_SHIFT;
            if (ui->pending.modifiers & MOD_ALT)
                mods |= HOTKEYF_ALT;
            SendMessageW(ui->hot, HKM_SETHOTKEY, MAKEWORD(ui->pending.key, mods), 0);
            control(h, L"STATIC", L"입력란에서 원하는 키 조합을 누르세요. 기본: Ctrl + Shift + X", 0, 20, 58,
                    550, 23, 0);
            auto check = [&](UINT id, const wchar_t *text, bool on, int y) {
                auto c = control(h, L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, 20, y, 520, 24, id);
                SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            };

            control(h, L"STATIC", L"저장 버튼을 누를 때마다 저장 위치를 선택합니다.", 0, 20, 124, 520, 24, 0);
            check(22, L"마우스 포인터 포함", ui->pending.cursor, 154);
            check(23, L"Windows 로그인 시 트레이에서 자동 시작", ui->pending.startup, 184);
            check(24, L"사각형 영역을 조정한 뒤 Enter로 확정", ui->pending.confirmRegion, 214);
            label(L"내보내기 폴더", 226);
            edit(30, ui->pending.folder, 226, 280);
            control(h, L"BUTTON", L"찾기", WS_TABSTOP, 488, 252, 60, 28, 40);
            label(L"외부 편집기 실행 파일", 264);
            edit(31, ui->pending.external, 264, 280);
            control(h, L"BUTTON", L"찾기", WS_TABSTOP, 488, 290, 60, 28, 41);
            label(L"크기 지정 폭 × 높이", 302);
            edit(32, std::to_wstring(ui->pending.fixedW), 302, 100);
            edit(33, std::to_wstring(ui->pending.fixedH), 302, 100);
            {
                auto dpi = GetDpiForWindow(h);
                SetWindowPos(GetDlgItem(h, 33), nullptr, MulDiv(320, dpi, 96), MulDiv(329, dpi, 96),
                             MulDiv(100, dpi, 96), MulDiv(26, dpi, 96), SWP_NOZORDER);
            }
            label(L"지연 캡처 (초, 0~30)", 340);
            edit(34, std::to_wstring(ui->pending.delay), 340, 100);
            label(L"최근 목록 (100~1000)", 378);
            edit(35, std::to_wstring(ui->pending.history), 378, 100);
            label(L"JPEG 품질 (1~100)", 416);
            edit(36, std::to_wstring(ui->pending.jpeg), 416, 100);
            label(L"기본 저장 형식", 454);
            {
                auto c = control(h, WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, 200, 481, 200, 160, 37);
                for (auto f : {L"PNG", L"JPEG", L"BMP", L"GIF", L"TIFF"})
                    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)f);
                SendMessageW(c, CB_SETCURSEL, ui->pending.format, 0);
            }
            label(L"파일명 패턴", 492);
            edit(38, ui->pending.pattern, 492, 330);
            control(h, L"STATIC",
                    L"파일명: %Y 연도 · %m 월 · %d 일 · %H 시 · %M 분 · %S 초\n내부 보관함: "
                    L"%LOCALAPPDATA%\\LightCapture\n창 닫기: 트레이 대기 / 파일 메뉴: 완전 종료",
                    0, 20, 560, 530, 58, 0);
            control(h, L"BUTTON", L"적용", WS_TABSTOP | BS_DEFPUSHBUTTON, 350, 630, 90, 32, IDOK);
            control(h, L"BUTTON", L"취소", WS_TABSTOP, 452, 630, 90, 32, IDCANCEL);
            return 0;
        }
        case WM_SIZE: {
            RECT r;
            GetClientRect(h, &r);
            SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE};
            si.nMax = MulDiv(680, GetDpiForWindow(h), 96);
            si.nPage = rh(r);
            SetScrollInfo(h, SB_VERT, &si, TRUE);
            return 0;
        }
        case WM_VSCROLL:
        case WM_MOUSEWHEEL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(h, SB_VERT, &si);
            int old = si.nPos;
            if (m == WM_MOUSEWHEEL)
                si.nPos -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 60;
            else
                switch (LOWORD(w)) {
                case SB_LINEUP:
                    si.nPos -= 36;
                    break;
                case SB_LINEDOWN:
                    si.nPos += 36;
                    break;
                case SB_PAGEUP:
                    si.nPos -= si.nPage;
                    break;
                case SB_PAGEDOWN:
                    si.nPos += si.nPage;
                    break;
                case SB_THUMBTRACK:
                    si.nPos = si.nTrackPos;
                    break;
                }
            si.fMask = SIF_POS;
            SetScrollInfo(h, SB_VERT, &si, TRUE);
            GetScrollInfo(h, SB_VERT, &si);
            ScrollWindowEx(h, 0, old - si.nPos, nullptr, nullptr, nullptr, nullptr,
                           SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDCANCEL) {
                ui->done = true;
                return 0;
            }
            if (LOWORD(w) == 40) {
                auto folder = chooseFolder(h, value(h, 30), L"내보내기 폴더를 선택하세요");
                if (!folder.empty())
                    SetDlgItemTextW(h, 30, folder.c_str());
                return 0;
            }
            if (LOWORD(w) == 41) {
                wchar_t buf[32768]{};
                OPENFILENAMEW ofn{sizeof(ofn)};
                ofn.hwndOwner = h;
                ofn.lpstrFilter = L"실행 파일\0*.exe\0\0";
                ofn.lpstrFile = buf;
                ofn.nMaxFile = 32768;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn))
                    SetDlgItemTextW(h, 31, buf);
                return 0;
            }
            if (LOWORD(w) == IDOK) {
                auto v = SendMessageW(ui->hot, HKM_GETHOTKEY, 0, 0);
                UINT vk = LOBYTE(v), hm = HIBYTE(v), mods = 0;
                if (hm & HOTKEYF_CONTROL)
                    mods |= MOD_CONTROL;
                if (hm & HOTKEYF_SHIFT)
                    mods |= MOD_SHIFT;
                if (hm & HOTKEYF_ALT)
                    mods |= MOD_ALT;
                if (!vk || vk == VK_F12 || !mods) {
                    message(h,
                            L"Ctrl·Shift·Alt 중 하나 이상과 함께 사용할 키를 지정하세요. F12는 사용할 수 "
                            L"없습니다.",
                            true);
                    return 0;
                }
                auto number = [&](int id, int low, int high) {
                    auto s = value(h, id);
                    size_t pos = 0;
                    int n = std::stoi(s, &pos);
                    require(pos == s.size() && n >= low && n <= high, "invalid numeric setting");
                    return n;
                };
                Settings next = ui->pending;
                next.key = vk;
                next.modifiers = mods;
                next.fixedW = number(32, 1, 32000);
                next.fixedH = number(33, 1, 32000);
                next.delay = number(34, 0, 30);
                next.history = number(35, 100, 1000);
                next.jpeg = number(36, 1, 100);
                next.format = SendMessageW(GetDlgItem(h, 37), CB_GETCURSEL, 0, 0);
                next.pattern = value(h, 38);
                require(!next.pattern.empty() && next.pattern.size() < 180, "invalid filename pattern");
                next.autoCopy = false;
                next.autoExport = false;
                next.cursor = IsDlgButtonChecked(h, 22) == BST_CHECKED;
                next.startup = IsDlgButtonChecked(h, 23) == BST_CHECKED;
                next.confirmRegion = IsDlgButtonChecked(h, 24) == BST_CHECKED;
                next.folder = value(h, 30);
                next.external = value(h, 31);
                require(!next.folder.empty(), "export folder is empty");
                auto old = ui->a->store.settings;
                bool changed = next.key != old.key || next.modifiers != old.modifiers || !ui->a->hotkey;
                if (changed && !ui->a->registerKey(mods, vk, true))
                    return 0;
                try {
                    ui->a->store.settings = next;
                    ui->a->store.saveSettings();
                    if (next.startup != old.startup)
                        ui->a->startup(next.startup);
                } catch (...) {
                    ui->a->store.settings = old;
                    if (changed)
                        ui->a->registerKey(old.modifiers, old.key, false);
                    try {
                        ui->a->store.saveSettings();
                    } catch (...) {
                    }
                    throw;
                }
                ui->a->updateTitle();
                Shell_NotifyIconW(NIM_DELETE, &ui->a->tray);
                ui->a->notifyTray();
                ui->a->statusText(L"설정 저장 완료 · " + hotkeyName(mods, vk));
                ui->done = true;
                return 0;
            }
            break;
        case WM_CLOSE:
            ui->done = true;
            return 0;
        }
    } catch (const std::exception &e) {
        std::string err = e.what();
        message(h,
                L"설정을 적용하지 못했습니다. 숫자 범위·경로·권한을 확인하세요.\n" +
                    std::wstring(err.begin(), err.end()),
                true);
    }
    return DefWindowProcW(h, m, w, l);
}
void App::settingsDialog() {
    if (busy) {
        statusText(L"캡처를 먼저 완료하거나 취소하세요.");
        return;
    }
    saveView();
    SettingsUI ui{this, store.settings};
    RECT r;
    GetWindowRect(main, &r);
    UINT dpi = GetDpiForWindow(main);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &mi);
    int width = std::min(MulDiv(590, dpi, 96), rw(mi.rcWork)),
        height = std::min(MulDiv(690, dpi, 96), rh(mi.rcWork) - 24);
    auto h = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"LCSettings", L"LightCapture 환경설정", WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
        mi.rcWork.left + (rw(mi.rcWork) - width) / 2, mi.rcWork.top + (rh(mi.rcWork) - height) / 2, width,
        height, main, nullptr, instance, &ui);
    EnableWindow(main, FALSE);
    ShowWindow(h, SW_SHOW);
    SetFocus(ui.hot);
    MSG msg;
    bool captureRequested = false;
    while (!ui.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            ui.done = true;
            continue;
        }
        if (msg.message == WM_HOTKEY) {
            if ((UINT)msg.wParam == hotId && GetFocus() != ui.hot) {
                captureRequested = true;
                ui.done = true;
            }
            continue;
        }
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyWindow(h);
    EnableWindow(main, TRUE);
    if (IsWindowVisible(main))
        SetActiveWindow(main);
    if (captureRequested)
        PostMessageW(main, WM_COMMAND, ID_CAPTURE, 0);
}
} // namespace lc
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    using namespace lc;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&ic);
    int argc;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool hidden = false, smoke = false, verify = false, customData = false;
    fs::path root = defaultData();
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--tray") == 0)
            hidden = true;
        else if (wcscmp(argv[i], L"--smoke") == 0) {
            smoke = true;
            hidden = true;
        } else if (wcscmp(argv[i], L"--verify") == 0) {
            verify = true;
            smoke = true;
            hidden = true;
        } else if (wcscmp(argv[i], L"--data") == 0 && i + 1 < argc) {
            customData = true;
            root = argv[++i];
        }
    }
    LocalFree(argv);
    if (smoke && !customData) {
        wchar_t temp[32768]{};
        GetTempPathW(32768, temp);
        root = fs::path(temp) / (L"LightCapture-verification-" + std::to_wstring(GetTickCount64()));
    }
    HANDLE mutex =
        CreateMutexW(nullptr, FALSE,
                     smoke ? L"Local\\LightCapture.Smoke.Instance" : L"Local\\LightCapture.Desktop.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"LCMain", nullptr);
        if (existing)
            PostMessageW(existing, WM_SECOND, 0, 0);
        CloseHandle(mutex);
        Gdiplus::GdiplusShutdown(token);
        CoUninitialize();
        return 0;
    }
    int result = 0;
    {
        try {
            App instance(root);
            app = &instance;
            instance.testMode = smoke;
            instance.create(inst, hidden);
            if (verify) {
                instance.verify();
                PostMessageW(instance.main, WM_COMMAND, ID_QUIT, 0);
            }
            if (smoke)
                SetTimer(instance.main, 99, 15000, nullptr);
            std::vector<ACCEL> keys = {{FVIRTKEY | FCONTROL, 'C', ID_COPY},
                                       {FVIRTKEY | FCONTROL, 'Z', ID_UNDO},
                                       {FVIRTKEY | FCONTROL, 'S', ID_SAVE},
                                       {FVIRTKEY | FCONTROL | FSHIFT, 'S', ID_SAVEAS},
                                       {FVIRTKEY | FCONTROL, 'O', ID_OPEN},
                                       {FVIRTKEY | FCONTROL, 'P', ID_PRINT},
                                       {FVIRTKEY | FCONTROL, 'D', ID_DUPLICATE},
                                       {FVIRTKEY, VK_DELETE, ID_DELETE},
                                       {FVIRTKEY, VK_F4, ID_SETTINGS}};
            HACCEL accel = CreateAcceleratorTableW(keys.data(), keys.size());
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (instance.quickBar && IsDialogMessageW(instance.quickBar, &msg))
                    continue;
                if (GetFocus() == instance.textInput || !TranslateAcceleratorW(instance.main, accel, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            DestroyAcceleratorTable(accel);
            if (instance.uiFont)
                DeleteObject(instance.uiFont);
            app = nullptr;
        } catch (const std::exception &ex) {
            std::string s = ex.what();
            if (smoke) {
                std::ofstream diagnostic(root / L"verification-error.txt");
                diagnostic << s << "\n";
            }
            if (!smoke)
                message(nullptr, L"LightCapture를 시작하지 못했습니다.\n" + std::wstring(s.begin(), s.end()),
                        true);
            result = 1;
        }
    }
    CloseHandle(mutex);
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return result;
}
