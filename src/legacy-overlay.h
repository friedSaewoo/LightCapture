// Benchmark only: paintOverlay extracted from the shipped v0.1.0 source.
// Window bounds and rectangle mode supplied as parameters; drawing body preserved.
using namespace lc;
static void legacyPaint(HDC dc, lc::Bitmap *snapshot, int width, int height, RECT selection, POINT mouse) {
    if (!snapshot)
        return;
    RECT client{0, 0, width, height};
    auto mode = lc::Mode::Rectangle;
    std::vector<POINT> polygon;
    RECT virtualRect{};
    bool selectionReady = false;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP hb = CreateCompatibleBitmap(dc, rw(client), rh(client));
    auto old = SelectObject(mem, hb);
    {
        Graphics g(mem);
        g.DrawImage(snapshot, 0, 0);
        Gdiplus::SolidBrush dark(Color(110, 0, 0, 0));
        g.FillRectangle(&dark, 0, 0, rw(client), rh(client));
        RECT r = bounded(selection, {0, 0, rw(client), rh(client)});
        if (rw(r) > 0 && rh(r) > 0) {
            if (mode == Mode::Freeform && polygon.size() > 2) {
                Gdiplus::GraphicsPath path;
                std::vector<Gdiplus::Point> points;
                for (auto p : polygon)
                    points.emplace_back(p.x, p.y);
                path.AddPolygon(points.data(), points.size());
                auto state = g.Save();
                g.SetClip(&path);
                g.DrawImage(snapshot, 0, 0);
                g.Restore(state);
            } else
                g.DrawImage(snapshot, Gdiplus::Rect(r.left, r.top, rw(r), rh(r)), r.left, r.top, rw(r),
                            rh(r), Gdiplus::UnitPixel);
            Gdiplus::Pen line(Color(255, 38, 121, 255), 2);
            g.DrawRectangle(&line, r.left, r.top, std::max(0, rw(r) - 1), std::max(0, rh(r) - 1));
        }
        Gdiplus::Pen cross(Color(160, 255, 255, 255), 1);
        g.DrawLine(&cross, mouse.x, 0, mouse.x, rh(client));
        g.DrawLine(&cross, 0, mouse.y, rw(client), mouse.y);
        int mx = std::clamp((int)mouse.x + 24, 0, std::max(0, rw(client) - 235)),
            my = std::clamp((int)mouse.y + 24, 0, std::max(0, rh(client) - 140));
        Gdiplus::SolidBrush label(Color(245, 24, 31, 43));
        g.FillRectangle(&label, mx, my, 228, 128);
        auto interp = g.GetInterpolationMode();
        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        int sx = std::clamp((int)mouse.x - 12, 0, std::max(0, rw(client) - 25)),
            sy = std::clamp((int)mouse.y - 8, 0, std::max(0, rh(client) - 17));
        g.DrawImage(snapshot, Gdiplus::Rect(mx + 8, my + 8, 100, 68), sx, sy, 25, 17,
                    Gdiplus::UnitPixel);
        g.SetInterpolationMode(interp);
        Gdiplus::Font font(L"맑은 고딕", 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush white(Color::White);
        std::wstring text = L"X " + std::to_wstring(mouse.x + virtualRect.left) + L"  Y " +
                            std::to_wstring(mouse.y + virtualRect.top) + L"\n" + std::to_wstring(rw(r)) +
                            L" × " + std::to_wstring(rh(r)) + L" px";
        g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(mx + 116, my + 12), &white);
        std::wstring help =
            selectionReady ? L"Enter 캡처 · 방향키 이동 · Shift 크기" : L"드래그로 선택 · Esc / 우클릭 취소";
        if (mode == Mode::Window || mode == Mode::Unit || mode == Mode::Fixed)
            help = L"클릭으로 캡처 · Esc 취소";
        g.DrawString(help.c_str(), -1, &font, Gdiplus::PointF(mx + 8, my + 87), &white);
    }
    BitBlt(dc, 0, 0, rw(client), rh(client), mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(hb);
    DeleteDC(mem);
}
