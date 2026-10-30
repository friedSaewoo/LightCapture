#include "app.h"
namespace lc {
int App::canvasEdgeHit(POINT p) {
    if (!doc() || !rendered || textInput || pixelReady || dragging || panning || busy)
        return 0;
    if (tool == Tool::Select && selectedObject >= 0 && selectedObject < (int)doc()->cursor) {
        auto &c = doc()->commands[selectedObject];
        if (c.tool != Tool::Crop && c.tool != Tool::CanvasSize && c.tool != Tool::CanvasPad &&
            c.tool != Tool::Reset && c.tool != Tool::Erase)
            return 0;
    }
    auto b = imageBounds();
    int tolerance = MulDiv(6, GetDpiForWindow(canvas), 96);
    bool right = p.x >= b.right && std::abs(p.x - b.right) <= tolerance && p.y >= b.top - tolerance &&
                 p.y <= b.bottom + tolerance;
    bool bottom = p.y >= b.bottom && std::abs(p.y - b.bottom) <= tolerance && p.x >= b.left - tolerance &&
                  p.x <= b.right + tolerance;
    bool left = p.x < b.left && std::abs(p.x - b.left) <= tolerance && p.y >= b.top - tolerance &&
                p.y <= b.bottom + tolerance;
    bool top = p.y < b.top && std::abs(p.y - b.top) <= tolerance && p.x >= b.left - tolerance &&
               p.x <= b.right + tolerance;
    return (left ? 1 : right ? 2 : 0) | (top ? 4 : bottom ? 8 : 0);
}
void App::growCanvas(POINT p) {
    if (!canvasGrowing)
        return;
    canvasGrowRect = canvasBeforeView;
    if (canvasGrowHit & 1)
        canvasGrowRect.left =
            std::clamp<LONG>(canvasBeforeView.left + (LONG)std::lround((p.x - canvasStartBounds.left) / zoom),
                             canvasBeforeView.right - 32000, canvasBeforeView.left);
    if (canvasGrowHit & 4)
        canvasGrowRect.top =
            std::clamp<LONG>(canvasBeforeView.top + (LONG)std::lround((p.y - canvasStartBounds.top) / zoom),
                             canvasBeforeView.bottom - 32000, canvasBeforeView.top);
    if (canvasGrowHit & 2)
        canvasGrowRect.right =
            std::clamp<LONG>(canvasBeforeView.left + (LONG)std::lround((p.x - canvasStartBounds.left) / zoom),
                             canvasBeforeView.right, 32000);
    if (canvasGrowHit & 8)
        canvasGrowRect.bottom =
            std::clamp<LONG>(canvasBeforeView.top + (LONG)std::lround((p.y - canvasStartBounds.top) / zoom),
                             canvasBeforeView.bottom, 32000);
    if (!validCanvasSize(rw(canvasGrowRect), rh(canvasGrowRect))) {
        canvasGrowRect = canvasBeforeView;
    }
    statusText(L"편집 공간 확장: " + std::to_wstring(rw(canvasGrowRect)) + L" × " +
               std::to_wstring(rh(canvasGrowRect)) + L" · Esc: 취소");
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::finishCanvasGrow(bool cancel) {
    if (!canvasGrowing)
        return;
    canvasGrowing = false;
    auto next = canvasGrowRect, old = canvasBeforeView, anchor = canvasStartBounds;
    canvasGrowHit = canvasEdgeHover = 0;
    if (GetCapture() == canvas)
        ReleaseCapture();
    if (!cancel && !EqualRect(&next, &old)) {
        Command c;
        c.tool = Tool::CanvasSize;
        c.rect = next;
        c.points = {{old.left, old.top}, {old.right, old.bottom}};
        LONG left = old.left - next.left, top = old.top - next.top;
        if (left || top) {
            c.tool = Tool::CanvasPad;
            c.rect = {0, 0, rw(next), rh(next)};
            c.points.push_back({left, top});
        }
        edit(std::move(c));
        auto b = imageBounds();
        panX += anchor.left - b.left - (LONG)std::lround(left * zoom);
        panY += anchor.top - b.top - (LONG)std::lround(top * zoom);
        statusText(L"편집 공간을 넓혔습니다. 흰색 여백에도 그릴 수 있습니다. Ctrl+Z: 되돌리기");
    }
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::pasteImage() {
    if (busy)
        return;
    auto image = (testMode && clipboardReader) ? clipboardReader() : readClipboardImage(main);
    if (!image) {
        statusText(L"클립보드에 이미지가 없거나 읽을 수 없습니다. 이미지를 복사한 뒤 Ctrl+V를 누르세요.");
        return;
    }
    require(validCanvasSize(image->GetWidth(), image->GetHeight()), "pasted image exceeds canvas limit");
    finishText();
    clearPixelSelection();
    finishCanvasGrow(true);
    if (!doc()) {
        add(std::move(image), L"붙여넣기");
        setTool(Tool::Select);
        return;
    }
    auto view = viewRect(*doc());
    LONG x = view.left, y = view.top;
    bool padded = (LONG)image->GetWidth() + 64 > rw(view) || (LONG)image->GetHeight() + 64 > rh(view);
    x += padded ? 32 : 16;
    y += padded ? 32 : 16;
    RECT destination{x, y, x + (LONG)image->GetWidth(), y + (LONG)image->GetHeight()};
    require(validCanvasSize(std::max(view.right, destination.right + (padded ? 32 : 0)),
                            std::max(view.bottom, destination.bottom + (padded ? 32 : 0))),
            "pasted image and canvas exceed size limit");
    Command c;
    c.tool = Tool::PasteImage;
    c.rect = destination;
    c.fill = padded;
    c.fontSize = 32;
    c.imageData = encodeImageBytes(*image);
    std::set<const std::vector<BYTE> *> unique;
    uint64_t total = c.imageData->size();
    for (size_t i = 0; i < doc()->cursor; ++i) {
        auto &data = doc()->commands[i].imageData;
        if (data && unique.insert(data.get()).second)
            total += data->size();
    }
    require(total <= 128ull * 1024 * 1024, "embedded images exceed document limit");
    setTool(Tool::Select);
    edit(std::move(c));
    auto extent = canvasExtent(doc()->commands.back());
    if (extent.right > view.right || extent.bottom > view.bottom) {
        RECT client{};
        GetClientRect(canvas, &client);
        auto next = viewRect(*doc());
        zoom = std::max(
            .02f, std::min(1.f, std::min((rw(client) - 48.f) / rw(next), (rh(client) - 48.f) / rh(next))));
        panX = panY = 0;
    }
    statusText(
        L"이미지를 붙여넣었습니다. 안쪽 드래그: 이동 · 오른쪽 아래 모서리/Shift 드래그: 크기 · Ctrl+Z: 취소");
    InvalidateRect(canvas, nullptr, FALSE);
}
} // namespace lc
