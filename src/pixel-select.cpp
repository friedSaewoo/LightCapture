#include "app.h"
namespace lc {
void App::clearPixelSelection() {
    clearDisplayCache();
    if ((pixelSelecting || pixelMoving) && GetCapture() == canvas)
        ReleaseCapture();
    pixelReady = pixelSelecting = pixelMoving = false;
    pixelResize = 0;
    pixelRect = pixelSource = {};
    pixelPatch.reset();
    pixelBackdrop.reset();
    if (canvas)
        InvalidateRect(canvas, nullptr, FALSE);
}
int App::pixelHit(POINT q) {
    if (!pixelReady)
        return 0;
    // Keep the border target the same size on screen while zooming.
    float tolerance = MulDiv(4, GetDpiForWindow(canvas), 96) / zoom;
    if (q.x < pixelRect.left - tolerance || q.x > pixelRect.right + tolerance ||
        q.y < pixelRect.top - tolerance || q.y > pixelRect.bottom + tolerance)
        return 0;
    int hit = 0;
    if (std::min(std::abs(q.x - pixelRect.left), std::abs(q.x - pixelRect.right)) <= tolerance)
        hit |= std::abs(q.x - pixelRect.left) <= std::abs(q.x - pixelRect.right) ? 1 : 2;
    if (std::min(std::abs(q.y - pixelRect.top), std::abs(q.y - pixelRect.bottom)) <= tolerance)
        hit |= std::abs(q.y - pixelRect.top) <= std::abs(q.y - pixelRect.bottom) ? 4 : 8;
    return hit ? hit : inside(pixelRect, q) ? 16 : 0;
}
void App::pixelDown(POINT q) {
    int hit = pixelHit(q);
    if (hit) {
        pixelPatch = crop(*rendered, pixelRect);
        pixelBackdrop = clone(*rendered);
        {
            Graphics g(pixelBackdrop.get());
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            Gdiplus::SolidBrush white(Color::White);
            g.FillRectangle(&white, (int)pixelRect.left, (int)pixelRect.top, rw(pixelRect), rh(pixelRect));
        }
        pixelSource = pixelRect;
        pixelStart = q;
        pixelMoving = true;
        pixelResize = hit == 16 ? 0 : hit;
    } else {
        clearPixelSelection();
        pixelRect = {q.x, q.y, q.x, q.y};
        pixelStart = q;
        pixelSelecting = true;
    }
    selectedObject = -1;
    SetCapture(canvas);
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::pixelMove(POINT q) {
    auto view = viewRect(*doc());
    q.x = std::clamp(q.x, view.left, view.right);
    q.y = std::clamp(q.y, view.top, view.bottom);
    if (pixelSelecting)
        pixelRect = bounded(normalized(pixelStart, q), view);
    else if (pixelMoving) {
        pixelRect = pixelSource;
        int dx = q.x - pixelStart.x, dy = q.y - pixelStart.y;
        if (pixelResize) {
            if (pixelResize & 1)
                pixelRect.left = std::clamp(pixelSource.left + dx, view.left, pixelSource.right - 1);
            if (pixelResize & 2)
                pixelRect.right = std::clamp(pixelSource.right + dx, pixelSource.left + 1, view.right);
            if (pixelResize & 4)
                pixelRect.top = std::clamp(pixelSource.top + dy, view.top, pixelSource.bottom - 1);
            if (pixelResize & 8)
                pixelRect.bottom = std::clamp(pixelSource.bottom + dy, pixelSource.top + 1, view.bottom);
        } else {
            dx = std::clamp(dx, (int)(view.left - pixelSource.left), (int)(view.right - pixelSource.right));
            dy = std::clamp(dy, (int)(view.top - pixelSource.top), (int)(view.bottom - pixelSource.bottom));
            OffsetRect(&pixelRect, dx, dy);
        }
    }
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::pixelUp() {
    clearDisplayCache();
    bool moved = pixelMoving && !EqualRect(&pixelRect, &pixelSource);
    auto source = pixelSource, destination = pixelRect;
    pixelReady = rw(destination) > 0 && rh(destination) > 0;
    pixelSelecting = pixelMoving = false;
    pixelResize = 0;
    if (GetCapture() == canvas)
        ReleaseCapture();
    pixelPatch.reset();
    pixelBackdrop.reset();
    if (moved) {
        Command move;
        move.tool = Tool::PixelSelect;
        move.rect = source;
        move.points = {{destination.left, destination.top}};
        if (rw(source) != rw(destination) || rh(source) != rh(destination))
            move.points.push_back({destination.right, destination.bottom});
        edit(std::move(move));
        selectedObject = -1;
        pixelRect = destination;
        pixelReady = true;
    }
    statusText(pixelReady ? L"테두리·모서리: 크기 조절 · 안쪽: 이동 · Ctrl+C: 영역 복사 · Esc: 선택 해제"
                          : L"이미지에서 사각형 영역을 드래그해 선택하세요.");
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::paintPixelSelection(Graphics &g, RECT bounds, RECT view) {
    if ((!pixelReady && !pixelSelecting) || rw(pixelRect) <= 0 || rh(pixelRect) <= 0)
        return;
    auto state = g.Save();
    g.SetClip(Gdiplus::Rect(bounds.left, bounds.top, rw(bounds), rh(bounds)));
    if (pixelMoving && pixelPatch) {
        drawPixelPatch(g, *pixelPatch,
                       Gdiplus::RectF(bounds.left + (pixelRect.left - view.left) * zoom,
                                      bounds.top + (pixelRect.top - view.top) * zoom, rw(pixelRect) * zoom,
                                      rh(pixelRect) * zoom));
    }
    float x = bounds.left + (pixelRect.left - view.left) * zoom;
    float y = bounds.top + (pixelRect.top - view.top) * zoom;
    float w = rw(pixelRect) * zoom, h = rh(pixelRect) * zoom;
    Gdiplus::Pen white(Color::White, 2), dashed(Color(0xff1d4ed8), 1);
    dashed.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&white, x, y, w, h);
    g.DrawRectangle(&dashed, x, y, w, h);
    if (pixelReady) {
        float size = (float)MulDiv(6, GetDpiForWindow(canvas), 96);
        Gdiplus::SolidBrush fill(Color::White);
        Gdiplus::Pen outline(Color(0xff1761d7), 1);
        for (auto p :
             {Gdiplus::PointF(x, y), Gdiplus::PointF(x + w / 2, y), Gdiplus::PointF(x + w, y),
              Gdiplus::PointF(x, y + h / 2), Gdiplus::PointF(x + w, y + h / 2), Gdiplus::PointF(x, y + h),
              Gdiplus::PointF(x + w / 2, y + h), Gdiplus::PointF(x + w, y + h)}) {
            g.FillRectangle(&fill, p.X - size / 2, p.Y - size / 2, size, size);
            g.DrawRectangle(&outline, p.X - size / 2, p.Y - size / 2, size, size);
        }
    }
    g.Restore(state);
}
} // namespace lc
