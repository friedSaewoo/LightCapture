#include "overlay.h"
namespace lc {
void OverlayRenderer::Surface::create(int w, int h) {
    dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
    require(dc && bitmap && pixels, "selector buffer allocation failed");
    old = SelectObject(dc, bitmap);
}
void OverlayRenderer::Surface::clear() {
    if (dc && old)
        SelectObject(dc, old);
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    dc = nullptr;
    bitmap = nullptr;
    old = nullptr;
    pixels = nullptr;
}
void OverlayRenderer::clear() {
    source.clear();
    dimmed.clear();
    frame.clear();
    if (border)
        DeleteObject(border);
    if (cross)
        DeleteObject(cross);
    if (panelBrush)
        DeleteObject(panelBrush);
    if (font)
        DeleteObject(font);
    border = cross = nullptr;
    panelBrush = nullptr;
    font = nullptr;
    width = height = 0;
}
void OverlayRenderer::prepare(Bitmap &image) {
    clear();
    try {
        width = image.GetWidth();
        height = image.GetHeight();
        source.create(width, height);
        dimmed.create(width, height);
        frame.create(width, height);
        Gdiplus::Rect r(0, 0, width, height);
        Gdiplus::BitmapData data{};
        require(image.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok,
                "selector pixels unavailable");
        for (int y = 0; y < height; ++y) {
            const auto row = (const uint32_t *)((const BYTE *)data.Scan0 + (ptrdiff_t)y * data.Stride);
            auto dst = source.pixels + (size_t)y * width;
            auto dark = dimmed.pixels + (size_t)y * width;
            for (int x = 0; x < width; ++x) {
                auto p = row[x];
                dst[x] = p;
                dark[x] = 0xff000000 | ((((p >> 16) & 255) * 145 / 255) << 16) |
                          ((((p >> 8) & 255) * 145 / 255) << 8) | ((p & 255) * 145 / 255);
            }
        }
        image.UnlockBits(&data);
        border = CreatePen(PS_SOLID, 2, RGB(38, 121, 255));
        cross = CreatePen(PS_SOLID, 1, RGB(220, 228, 240));
        panelBrush = CreateSolidBrush(RGB(24, 31, 43));
        font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
        require(border && cross && panelBrush && font, "selector drawing resource allocation failed");
    } catch (...) {
        clear();
        throw;
    }
}
RECT OverlayRenderer::panel(POINT mouse) const {
    LONG x = std::clamp<LONG>(mouse.x + 24, 0, std::max(0, width - 235));
    LONG y = std::clamp<LONG>(mouse.y + 24, 0, std::max(0, height - 140));
    return {x, y, x + 228, y + 128};
}
HRGN OverlayRenderer::damage(RECT before, POINT oldMouse, RECT after, POINT mouse, bool freeform) const {
    auto dirty = CreateRectRgn(0, 0, 0, 0);
    auto add = [&](RECT r) {
        auto part = CreateRectRgnIndirect(&r);
        CombineRgn(dirty, dirty, part, RGN_OR);
        DeleteObject(part);
    };
    auto a = CreateRectRgnIndirect(&before), b = CreateRectRgnIndirect(&after);
    CombineRgn(dirty, a, b, freeform ? RGN_OR : RGN_XOR);
    DeleteObject(a);
    DeleteObject(b);
    // Include both old and new outlines, including the pen's outer pixel.
    for (auto r : {before, after}) {
        if (rw(r) <= 0 || rh(r) <= 0)
            continue;
        add({r.left - 2, r.top - 2, r.right + 2, r.top + 2});
        add({r.left - 2, r.bottom - 2, r.right + 2, r.bottom + 2});
        add({r.left - 2, r.top - 2, r.left + 2, r.bottom + 2});
        add({r.right - 2, r.top - 2, r.right + 2, r.bottom + 2});
    }
    for (auto p : {oldMouse, mouse}) {
        add({p.x - 1, 0, p.x + 2, height});
        add({0, p.y - 1, width, p.y + 2});
        add(panel(p));
    }
    auto bounds = CreateRectRgn(0, 0, width, height);
    CombineRgn(dirty, dirty, bounds, RGN_AND);
    DeleteObject(bounds);
    return dirty;
}
void OverlayRenderer::paint(HDC dc, HRGN dirty, RECT selection, POINT mouse, POINT origin, Mode mode,
                            const std::vector<POINT> &polygon, bool ready) {
    if (!frame.dc)
        return;
    int state = SaveDC(frame.dc);
    SelectClipRgn(frame.dc, dirty);
    BitBlt(frame.dc, 0, 0, width, height, dimmed.dc, 0, 0, SRCCOPY);
    auto r = bounded(selection, {0, 0, width, height});
    if (rw(r) > 0 && rh(r) > 0) {
        if (mode == Mode::Freeform && polygon.size() > 2) {
            auto region = CreatePolygonRgn(polygon.data(), (int)polygon.size(), ALTERNATE);
            ExtSelectClipRgn(frame.dc, region, RGN_AND);
            BitBlt(frame.dc, r.left, r.top, rw(r), rh(r), source.dc, r.left, r.top, SRCCOPY);
            SelectClipRgn(frame.dc, dirty);
            DeleteObject(region);
        } else
            BitBlt(frame.dc, r.left, r.top, rw(r), rh(r), source.dc, r.left, r.top, SRCCOPY);
        SelectObject(frame.dc, border);
        SelectObject(frame.dc, GetStockObject(NULL_BRUSH));
        Rectangle(frame.dc, r.left, r.top, r.right, r.bottom);
    }
    SelectObject(frame.dc, cross);
    MoveToEx(frame.dc, mouse.x, 0, nullptr);
    LineTo(frame.dc, mouse.x, height);
    MoveToEx(frame.dc, 0, mouse.y, nullptr);
    LineTo(frame.dc, width, mouse.y);
    auto box = panel(mouse);
    FillRect(frame.dc, &box, panelBrush);
    int sx = std::clamp((int)mouse.x - 12, 0, std::max(0, width - 25));
    int sy = std::clamp((int)mouse.y - 8, 0, std::max(0, height - 17));
    SetStretchBltMode(frame.dc, COLORONCOLOR);
    StretchBlt(frame.dc, box.left + 8, box.top + 8, 100, 68, source.dc, sx, sy, std::min(25, width),
               std::min(17, height), SRCCOPY);
    SelectObject(frame.dc, font);
    SetBkMode(frame.dc, TRANSPARENT);
    SetTextColor(frame.dc, RGB(255, 255, 255));
    std::wstring text = L"X " + std::to_wstring(mouse.x + origin.x) + L"  Y " +
                        std::to_wstring(mouse.y + origin.y) + L"\n" + std::to_wstring(rw(r)) + L" × " +
                        std::to_wstring(rh(r)) + L" px";
    RECT label{box.left + 116, box.top + 12, box.right - 4, box.top + 80};
    DrawTextW(frame.dc, text.c_str(), -1, &label, DT_LEFT | DT_NOPREFIX);
    const wchar_t *help =
        ready ? L"Enter 캡처 · 방향키 이동 · Shift 크기" : L"드래그로 선택 · Esc / 우클릭 취소";
    if (mode == Mode::Window || mode == Mode::Unit || mode == Mode::Fixed)
        help = L"클릭으로 캡처 · Esc 취소";
    label = {box.left + 8, box.top + 87, box.right - 4, box.bottom - 4};
    DrawTextW(frame.dc, help, -1, &label, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    RECT area{0, 0, width, height};
    if (dirty)
        GetRgnBox(dirty, &area);
    int targetState = SaveDC(dc);
    if (dirty)
        ExtSelectClipRgn(dc, dirty, RGN_AND);
    BitBlt(dc, area.left, area.top, rw(area), rh(area), frame.dc, area.left, area.top, SRCCOPY);
    RestoreDC(dc, targetState);
    RestoreDC(frame.dc, state);
}
} // namespace lc
