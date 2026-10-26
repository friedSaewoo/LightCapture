#include "app.h"
#include "toolbar.h"
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
    auto button = [&](UINT id, const wchar_t *text, int width, bool icon = false) {
        auto h = control(main, L"BUTTON", text, WS_TABSTOP | (icon ? BS_OWNERDRAW : BS_PUSHBUTTON), 0, 0,
                         width, 30, id);
        topControls.push_back({id, h});
        if (icon)
            SetWindowSubclass(h, toolbarProc, 1, (DWORD_PTR)this);
        tip(h, text);
        return h;
    };
    button(ID_CAPTURE, L"새 캡처", 86);
    button(ID_WINDOW, L"창", 46);
    button(ID_FULL, L"전체 화면", 80);
    button(ID_SCROLL, L"스크롤", 68);
    for (auto value : ToolbarTools)
        button(ID_TOOL + (int)value, ToolNames[(int)value], 32, true);
    auto current = control(main, L"STATIC", L"현재 색상", SS_OWNERDRAW, 0, 0, 36, 36, ID_COLOR);
    topControls.push_back({ID_COLOR, current});
    for (int i = 0; i < PaletteCount; ++i)
        button(ID_PALETTE + i, Palette[i].name, 22, true);
    widthControl = button(181, L"선 굵기", 58);
    SetWindowTextW(widthControl, (std::to_wstring((int)stroke) + L" px").c_str());
    tip(widthControl, L"선 굵기");
    auto fillButton = control(main, L"BUTTON", L"채움", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 60, 28, ID_FILL);
    SendMessageW(fillButton, BM_SETCHECK, fill ? BST_CHECKED : BST_UNCHECKED, 0);
    topControls.push_back({ID_FILL, fillButton});
    button(ID_FONT, L"글꼴", 58);
    button(ID_FIT, L"맞춤", 58);
}
int App::layoutToolbar(int width, UINT dpi) {
    auto s = [&](int v) { return MulDiv(v, dpi, 96); };
    int x = s(10), y = s(8);
    for (auto [id, h] : topControls)
        if (id < ID_COPY || id == ID_SAVE) {
            int w = s((int)(INT_PTR)GetPropW(h, L"LCBaseWidth"));
            if (x + w > width - s(10)) {
                x = s(10);
                y += s(34);
            }
            MoveWindow(h, x, y, w, s(28), TRUE);
            x += w + s(6);
        }
    y += s(38);
    x = s(10);
    for (auto value : ToolbarTools) {
        MoveWindow(GetDlgItem(main, ID_TOOL + (int)value), x, y + s(6), s(32), s(32), TRUE);
        x += s(33);
    }
    int bottom = y + s(48), paletteWidth = s(290);
    if (x + s(14) + paletteWidth > width - s(10)) {
        x = s(10);
        y = bottom + s(8);
        bottom = y + s(48);
    } else
        x += s(14);
    MoveWindow(GetDlgItem(main, ID_COLOR), x, y + s(4), s(36), s(36), TRUE);
    x += s(46);
    for (int i = 0; i < PaletteCount; ++i)
        MoveWindow(GetDlgItem(main, ID_PALETTE + i), x + s((i % 10) * 24), y + s((i / 10) * 24), s(22), s(22),
                   TRUE);
    x += s(240) + s(16);
    int options = s(58 + 60 + 58 + 58 + 18);
    if (x + options > width - s(10)) {
        x = s(10);
        y = bottom + s(6);
        bottom = y + s(30);
    }
    for (UINT id : {181u, (UINT)ID_FILL, (UINT)ID_FONT, (UINT)ID_FIT}) {
        HWND h = GetDlgItem(main, id);
        int w = s((int)(INT_PTR)GetPropW(h, L"LCBaseWidth"));
        MoveWindow(h, x, y + s(6), w, s(28), TRUE);
        x += w + s(6);
    }
    return std::max(bottom, y + s(34)) + s(8);
}
void App::refreshToolbar() {
    for (auto [id, h] : topControls)
        if ((id >= ID_TOOL && id <= ID_TOOL + (UINT)Tool::PixelSelect) ||
            (id >= ID_PALETTE && id < ID_PALETTE + PaletteCount) || id == ID_COLOR)
            InvalidateRect(h, nullptr, FALSE);
}
void App::setTool(Tool next) {
    if (next != tool)
        finishText();
    if (next != tool)
        clearPixelSelection();
    tool = next;
    refreshToolbar();
    if (tool == Tool::Text)
        statusText(L"이미지 위에 텍스트 영역을 드래그하세요. Enter 줄바꿈 · Ctrl+Enter 확정 · Esc 취소");
    else if (tool == Tool::PixelSelect)
        statusText(L"사각형으로 선택한 뒤 안쪽을 드래그해 이동하세요. Ctrl+C 영역 복사 · Esc 선택 해제");
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
    g.FillRectangle(&background, (int)r.left, (int)r.top, (int)w, (int)h);
    if (palette || current) {
        Gdiplus::SolidBrush paint(Color(current ? color : Palette[id - ID_PALETTE].value));
        float inset = current ? 3 : 4;
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
