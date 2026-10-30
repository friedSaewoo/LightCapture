#include "app.h"
#include "theme.h"
namespace lc {
void App::paintActionButton(DRAWITEMSTRUCT *item) {
    HDC dc = item->hDC;
    RECT r = item->rcItem;
    bool enabled = IsWindowEnabled(item->hwndItem);
    bool hover = hoveredButton == item->hwndItem;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool save = item->CtlID == ID_SAVE;
    bool capture = item->CtlID == ID_CAPTURE;
    auto dpi = GetDpiForWindow(item->hwndItem);
    auto scale = [&](int value) { return MulDiv(value, dpi ? dpi : 96, 96); };
    Graphics g(dc);
    g.Clear(Color(save || item->CtlID == ID_HISTORY_DELETE ? theme::Sidebar : theme::Window));
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    uint32_t background = !enabled             ? 0xfff1f4f8
                          : save               ? ((hover || pressed) ? theme::BlueHover : theme::Blue)
                          : capture            ? ((hover || pressed) ? 0xffdceaff : theme::Tint)
                          : (hover || pressed) ? 0xffedf2f8
                                               : theme::Window;
    uint32_t edge = !enabled ? theme::Line : save ? background : capture ? 0xffbed3f5 : theme::Line;
    theme::rounded(g, {1.f, 1.f, (float)rw(r) - 2, (float)rh(r) - 2}, (float)scale(6), background, edge);
    RECT text = r;
    if (capture) {
        Gdiplus::Pen pen(Color(theme::Blue), (float)scale(2));
        int x = scale(12), y = rh(r) / 2 - scale(7);
        g.DrawRectangle(&pen, x, y, scale(18), scale(13));
        g.DrawEllipse(&pen, x + scale(6), y + scale(3), scale(6), scale(6));
        g.DrawLine(&pen, x + scale(5), y - scale(3), x + scale(12), y - scale(3));
        text.left += scale(27);
    }
    uint32_t foreground = !enabled ? 0xff6f7b8c : save ? 0xffffffff : capture ? theme::Blue : theme::Ink;
    wchar_t label[128]{};
    GetWindowTextW(item->hwndItem, label, 128);
    Gdiplus::Font font(dc, uiFont);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush ink{Color(foreground)};
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.DrawString(label, -1, &font,
                 Gdiplus::RectF((float)text.left, (float)text.top, (float)rw(text), (float)rh(text)), &format,
                 &ink);
    if (item->itemState & ODS_FOCUS) {
        Gdiplus::Pen focus(Color(save ? 0xffffffff : theme::Blue), 1);
        focus.SetDashStyle(Gdiplus::DashStyleDot);
        g.DrawRectangle(&focus, scale(4), scale(4), rw(r) - scale(8), rh(r) - scale(8));
    }
}
void App::paintChrome(HDC dc) {
    RECT r{};
    GetClientRect(main, &r);
    Graphics g(dc);
    g.Clear(Color(theme::Window));
    Gdiplus::Pen line(Color(theme::Line), 1);
    g.DrawLine(&line, 0, toolbarBottom - 1, rw(r), toolbarBottom - 1);
    if (penWidthVisible()) {
        int edge = MulDiv(64, GetDpiForWindow(main), 96);
        g.DrawLine(&line, edge - 1, toolbarBottom, edge - 1, rh(r));
    }
    if (!list)
        return;
    RECT sidebar{};
    GetWindowRect(list, &sidebar);
    MapWindowPoints(nullptr, main, (POINT *)&sidebar, 2);
    sidebar.left -= MulDiv(8, GetDpiForWindow(main), 96);
    Gdiplus::SolidBrush background(Color(theme::Sidebar));
    g.FillRectangle(&background, (int)sidebar.left, toolbarBottom, rw(r) - sidebar.left,
                    std::max(0, rh(r) - toolbarBottom));
    g.DrawLine(&line, (int)sidebar.left, toolbarBottom, (int)sidebar.left, rh(r));
    for (auto divider : toolbarDividers)
        g.DrawLine(&line, (int)divider.left, (int)divider.top, (int)divider.left, (int)divider.bottom);
}
} // namespace lc
