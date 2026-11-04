#include "app.h"
#include "toolbar.h"
#include "theme.h"
namespace lc {
void App::createToolbar() {
    tooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0,
                        0, 0, 0, main, nullptr, instance, nullptr);
    auto tip = [&](HWND h, const wchar_t *text) {
        TOOLINFOW ti{sizeof(ti)};
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = main;
        ti.uId = (UINT_PTR)h;
        ti.lpszText = (wchar_t *)text;
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    };
    auto button = [&](UINT id, const wchar_t *text, int width, bool /*icon*/ = false) {
        auto h = control(main, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, 0, 0, width, 30, id);
        topControls.push_back({id, h});
        SetWindowSubclass(h, toolbarProc, 1, (DWORD_PTR)this);
        tip(h, text);
        return h;
    };
    button(ID_CAPTURE, L"새 캡처", 108);
    button(ID_NEW_CANVAS, L"그림판", 80);
    button(ID_WINDOW, L"창", 56);
    button(ID_FULL, L"전체 화면", 88);
    button(ID_SCROLL, L"스크롤", 68);
    for (auto value : ToolbarTools)
        button(ID_TOOL + (int)value, ToolNames[(int)value], 32, true);
    auto current = control(main, L"STATIC", L"현재 색상", SS_OWNERDRAW, 0, 0, 36, 36, ID_COLOR);
    topControls.push_back({ID_COLOR, current});
    for (int i = 0; i < PaletteCount; ++i)
        button(ID_PALETTE + i, Palette[i].name, 22, true);
    widthTitle = control(main, L"STATIC", L"굵기", SS_CENTER, 0, 0, 56, 24, ID_PEN_WIDTH_TITLE);
    widthLabel = control(main, L"STATIC", L"", SS_CENTER, 0, 0, 56, 24, ID_PEN_WIDTH_LABEL);
    widthControl = control(main, TRACKBAR_CLASSW, L"펜 굵기", WS_TABSTOP | TBS_VERT | TBS_NOTICKS, 0, 0, 40,
                           200, ID_PEN_WIDTH);
    SendMessageW(widthControl, TBM_SETRANGE, FALSE, MAKELPARAM(1, 80));
    SendMessageW(widthControl, TBM_SETPAGESIZE, 0, 5);
    SetWindowSubclass(widthControl, penWidthProc, 1, (DWORD_PTR)this);
    tip(widthControl, L"펜 굵기: 위로 굵게, 아래로 얇게. 드래그 또는 마우스 휠로 조절합니다.");
    setPenWidth((int)stroke);
    auto fillButton = control(main, L"BUTTON", L"채움", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 60, 28, ID_FILL);
    SendMessageW(fillButton, BM_SETCHECK, fill ? BST_CHECKED : BST_UNCHECKED, 0);
    topControls.push_back({ID_FILL, fillButton});
    button(ID_FONT, L"글꼴", 58);
}
int App::layoutToolbar(int width, UINT dpi) {
    auto s = [&](int v) { return MulDiv(v, dpi, 96); };
    int x = s(16), y = s(12), row = s(48);
    toolbarDividers.clear();
    auto group = [&](int logicalWidth) {
        int groupWidth = s(logicalWidth);
        if (x + groupWidth > width - s(16)) {
            x = s(16);
            y += row;
        } else if (x > s(16)) {
            toolbarDividers.push_back({x + s(7), y + s(6), x + s(7), y + s(30)});
            x += s(20);
            if (x + groupWidth > width - s(16)) {
                toolbarDividers.pop_back();
                x = s(16);
                y += row;
            }
        }
    };
    group(430);
    for (UINT id : {(UINT)ID_CAPTURE, (UINT)ID_NEW_CANVAS, (UINT)ID_WINDOW, (UINT)ID_FULL, (UINT)ID_SCROLL}) {
        HWND h = GetDlgItem(main, id);
        int w = s((int)(INT_PTR)GetPropW(h, L"LCBaseWidth"));
        MoveWindow(h, x, y, w, s(36), TRUE);
        x += w + s(6);
    }
    group(432);
    for (auto value : ToolbarTools) {
        MoveWindow(GetDlgItem(main, ID_TOOL + (int)value), x, y, s(34), s(36), TRUE);
        x += s(36);
    }
    group(200);
    MoveWindow(GetDlgItem(main, ID_COLOR), x, y + s(2), s(30), s(32), TRUE);
    x += s(40);
    for (int i = 0; i < PaletteCount; ++i)
        MoveWindow(GetDlgItem(main, ID_PALETTE + i), x + s((i % 10) * 16), y + s((i / 10) * 18), s(15), s(16),
                   TRUE);
    x += s(160);
    group(130);
    for (UINT id : {(UINT)ID_FILL, (UINT)ID_FONT}) {
        HWND h = GetDlgItem(main, id);
        int w = s((int)(INT_PTR)GetPropW(h, L"LCBaseWidth"));
        MoveWindow(h, x, y, w, s(36), TRUE);
        x += w + s(6);
    }
    toolbarBottom = y + s(48);
    InvalidateRect(main, nullptr, FALSE);
    return toolbarBottom;
}
void App::refreshToolbar() {
    for (auto [id, h] : topControls)
        if ((id >= ID_TOOL && id <= ID_TOOL + (UINT)Tool::PixelSelect) ||
            (id >= ID_PALETTE && id < ID_PALETTE + PaletteCount) || id == ID_COLOR)
            InvalidateRect(h, nullptr, FALSE);
}
void App::setTool(Tool next) {
    bool hadPenWidth = penWidthVisible();
    if (next != tool)
        finishText();
    if (next != tool)
        clearPixelSelection();
    tool = next;
    refreshToolbar();
    if (hadPenWidth != penWidthVisible())
        layout();
    if (tool == Tool::Text)
        statusText(L"이미지 위에 텍스트 영역을 드래그하세요. Enter 줄바꿈 · Ctrl+Enter 확정 · Esc 취소");
    else if (tool == Tool::PixelSelect)
        statusText(L"사각형 선택 후 테두리·모서리는 크기 조절, 안쪽은 이동합니다. Delete 영역 삭제 · Ctrl+C "
                   L"영역 복사 · Esc 해제");
}
void App::setPenWidth(int value) {
    stroke = (float)std::clamp(value, 1, 80);
    if (widthControl)
        SendMessageW(widthControl, TBM_SETPOS, TRUE, 81 - (int)stroke);
    if (widthLabel)
        SetWindowTextW(widthLabel, (std::to_wstring((int)stroke) + L" px").c_str());
}
LRESULT CALLBACK App::penWidthProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto a = (App *)data;
    if (m == WM_MOUSEWHEEL) {
        a->setPenWidth((int)a->stroke + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1));
        return 0;
    }
    if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, penWidthProc, id);
    return DefSubclassProc(h, m, w, l);
}
void App::setColor(uint32_t next) {
    color = next;
    if (textInput) {
        textDraft.color = color;
        InvalidateRect(textInput, nullptr, TRUE);
    }
    refreshToolbar();
    if (textInput && !testMode)
        SetFocus(textInput);
}
void App::paintToolbarButton(DRAWITEMSTRUCT *item) {
    UINT id = item->CtlID;
    bool palette = id >= ID_PALETTE && id < ID_PALETTE + PaletteCount;
    bool current = id == ID_COLOR;
    bool selected = palette ? color == Palette[id - ID_PALETTE].value
                            : !current && id >= ID_TOOL && id <= ID_TOOL + (UINT)Tool::PixelSelect &&
                                  (UINT)tool == id - ID_TOOL;
    bool hover = hoveredButton == item->hwndItem, pressed = (item->itemState & ODS_SELECTED) != 0;
    Graphics g(item->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    auto r = item->rcItem;
    float w = rw(r), h = rh(r);
    Gdiplus::SolidBrush background(Color(selected             ? 0xffdbeafe
                                         : (hover || pressed) ? 0xffedf2f7
                                                              : 0xffffffff));
    g.Clear(Color(theme::Window));
    theme::rounded(g, {r.left + 1.f, r.top + 1.f, w - 2, h - 2}, 5.f,
                   selected             ? theme::Tint
                   : (hover || pressed) ? 0xfff0f4fa
                                        : theme::Window);
    if (palette || current) {
        Gdiplus::SolidBrush paint(Color(current ? color : Palette[id - ID_PALETTE].value));
        float inset = current ? 4 : 2;
        g.FillRectangle(&paint, r.left + inset, r.top + inset, w - inset * 2, h - inset * 2);
        Gdiplus::Pen edge(Color(0xff94a3b8), 1);
        g.DrawRectangle(&edge, r.left + inset, r.top + inset, w - inset * 2 - 1, h - inset * 2 - 1);
    } else {
        auto state = g.Save();
        g.TranslateTransform(r.left + (w - 24 * (w / 32)) / 2, r.top + (h - 24 * (h / 32)) / 2);
        g.ScaleTransform(w / 32, h / 32);
        Gdiplus::Pen pen(Color(selected ? 0xff1d4ed8 : 0xff334155), 1.7f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush ink(Color(selected ? 0xff1d4ed8 : 0xff334155));
        auto line = [&](float x, float y, float x2, float y2) { g.DrawLine(&pen, x, y, x2, y2); };
        switch ((Tool)(id - ID_TOOL)) {
        case Tool::PixelSelect:
            pen.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawRectangle(&pen, 3, 3, 18, 18);
            break;
        case Tool::Select: {
            Gdiplus::PointF p[] = {{4, 2}, {4, 20}, {9, 15}, {13, 22}, {16, 20}, {12, 13}, {19, 12}};
            g.DrawPolygon(&pen, p, 7);
            break;
        }
        case Tool::Pen:
            line(4, 20, 8, 12);
            line(8, 12, 18, 2);
            line(18, 2, 22, 6);
            line(22, 6, 12, 16);
            line(12, 16, 4, 20);
            line(8, 12, 12, 16);
            break;
        case Tool::Line:
            line(3, 21, 21, 3);
            break;
        case Tool::Arrow:
            line(3, 21, 21, 3);
            line(12, 3, 21, 3);
            line(21, 3, 21, 12);
            break;
        case Tool::Rectangle:
            g.DrawRectangle(&pen, 3, 5, 18, 14);
            break;
        case Tool::Ellipse:
            g.DrawEllipse(&pen, 3, 4, 18, 16);
            break;
        case Tool::Highlight: {
            Gdiplus::SolidBrush yellow(Color(0xffffd600));
            g.FillRectangle(&yellow, 2, 19, 20, 4);
            line(5, 17, 8, 11);
            line(8, 11, 17, 2);
            line(17, 2, 22, 7);
            line(22, 7, 13, 16);
            line(13, 16, 5, 17);
            line(8, 11, 13, 16);
            break;
        }
        case Tool::Text:
            line(4, 21, 12, 3);
            line(12, 3, 20, 21);
            line(7, 15, 17, 15);
            break;
        case Tool::Mosaic:
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x) {
                    Gdiplus::SolidBrush b(Color((x + y) % 2 ? 0xff94a3b8 : 0xff334155));
                    g.FillRectangle(&b, 3 + x * 7, 3 + y * 7, 5, 5);
                }
            break;
        case Tool::Crop:
            line(7, 2, 7, 18);
            line(7, 18, 22, 18);
            line(2, 7, 18, 7);
            line(18, 7, 18, 22);
            break;
        case Tool::Erase: {
            Gdiplus::PointF p[] = {{3, 15}, {14, 4}, {22, 12}, {13, 21}, {9, 21}};
            g.DrawPolygon(&pen, p, 5);
            line(8, 10, 17, 17);
            line(9, 21, 23, 21);
            break;
        }
        default:
            break;
        }
        g.Restore(state);
    }
    if (selected || hover || pressed) {
        Gdiplus::Pen edge(Color(selected ? 0xff2563eb : 0xff94a3b8), 1.2f);
        g.DrawRectangle(&edge, r.left + 1.f, r.top + 1.f, w - 2, h - 2);
    }
    if (item->itemState & ODS_FOCUS) {
        RECT f = r;
        InflateRect(&f, -2, -2);
        DrawFocusRect(item->hDC, &f);
    }
}
LRESULT CALLBACK App::toolbarProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto a = (App *)data;
    if (m == WM_MOUSEMOVE && a->hoveredButton != h) {
        if (a->hoveredButton)
            InvalidateRect(a->hoveredButton, nullptr, FALSE);
        a->hoveredButton = h;
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, h, 0};
        TrackMouseEvent(&tracking);
        InvalidateRect(h, nullptr, FALSE);
    } else if (m == WM_MOUSELEAVE) {
        if (a->hoveredButton == h)
            a->hoveredButton = nullptr;
        InvalidateRect(h, nullptr, FALSE);
    } else if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, toolbarProc, id);
    return DefSubclassProc(h, m, w, l);
}
} // namespace lc
