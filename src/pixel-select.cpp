#include "app.h"
namespace lc {
void App::clearPixelSelection() {
    if ((pixelSelecting || pixelMoving) && GetCapture() == canvas)
        ReleaseCapture();
    pixelReady = pixelSelecting = pixelMoving = false;
    pixelRect = pixelSource = {};
    pixelPatch.reset();
    pixelBackdrop.reset();
    if (canvas)
        InvalidateRect(canvas, nullptr, FALSE);
}
void App::pixelDown(POINT q) {
    if (pixelReady && inside(pixelRect, q)) {
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
        int dx = std::clamp(q.x - pixelStart.x, view.left - pixelSource.left, view.right - pixelSource.right);
        int dy = std::clamp(q.y - pixelStart.y, view.top - pixelSource.top, view.bottom - pixelSource.bottom);
        OffsetRect(&pixelRect, dx, dy);
    }
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::pixelUp() {
    bool moved = pixelMoving && (pixelRect.left != pixelSource.left || pixelRect.top != pixelSource.top);
    auto source = pixelSource, destination = pixelRect;
    pixelReady = rw(destination) > 0 && rh(destination) > 0;
    pixelSelecting = pixelMoving = false;
    if (GetCapture() == canvas)
        ReleaseCapture();
    pixelPatch.reset();
    pixelBackdrop.reset();
    if (moved) {
        Command move;
        move.tool = Tool::PixelSelect;
        move.rect = source;
        move.points = {{destination.left, destination.top}};
        edit(std::move(move));
        selectedObject = -1;
        pixelRect = destination;
        pixelReady = true;
    }
    statusText(pixelReady ? L"선택 영역 안쪽 드래그: 이동 · Ctrl+C: 영역 복사 · Esc 또는 바깥 클릭: 선택 해제"
                          : L"이미지에서 사각형 영역을 드래그해 선택하세요.");
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::paintPixelSelection(Graphics &g, RECT bounds, RECT view) {
    if ((!pixelReady && !pixelSelecting) || rw(pixelRect) <= 0 || rh(pixelRect) <= 0)
        return;
    auto state = g.Save();
    g.SetClip(Gdiplus::Rect(bounds.left, bounds.top, rw(bounds), rh(bounds)));
    if (pixelMoving && pixelPatch) {
        g.DrawImage(pixelPatch.get(),
                    Gdiplus::RectF(bounds.left + (pixelRect.left - view.left) * zoom,
                                   bounds.top + (pixelRect.top - view.top) * zoom, rw(pixelRect) * zoom,
                                   rh(pixelRect) * zoom),
                    0.f, 0.f, (float)pixelPatch->GetWidth(), (float)pixelPatch->GetHeight(),
                    Gdiplus::UnitPixel);
    }
    float x = bounds.left + (pixelRect.left - view.left) * zoom;
    float y = bounds.top + (pixelRect.top - view.top) * zoom;
    float w = rw(pixelRect) * zoom, h = rh(pixelRect) * zoom;
    Gdiplus::Pen white(Color::White, 2), dashed(Color(0xff1d4ed8), 1);
    dashed.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&white, x, y, w, h);
    g.DrawRectangle(&dashed, x, y, w, h);
    g.Restore(state);
}
} // namespace lc
