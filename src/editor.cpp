#include "app.h"
#include "theme.h"
namespace lc {
void App::clearDisplayCache() {
    displayCache.reset();
    displayCacheSource = nullptr;
    displayCacheView = {};
}
Bitmap *App::scaledDisplay(Bitmap &source, RECT view, int width, int height) {
    if (width <= 0 || height <= 0 || (uint64_t)width * height > 4ull * 1024 * 1024)
        return nullptr;
    if (displayCache && displayCacheSource == &source && EqualRect(&view, &displayCacheView) &&
        displayCache->GetWidth() == (UINT)width && displayCache->GetHeight() == (UINT)height)
        return displayCache.get();
    clearDisplayCache();
    auto next = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
    require(next->GetLastStatus() == Gdiplus::Ok, "display cache allocation failed");
    {
        Graphics g(next.get());
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        g.SetInterpolationMode(width >= rw(view) ? Gdiplus::InterpolationModeNearestNeighbor
                                                 : Gdiplus::InterpolationModeHighQualityBicubic);
        require(g.DrawImage(&source, Gdiplus::Rect(0, 0, width, height), view.left, view.top, rw(view),
                            rh(view), Gdiplus::UnitPixel) == Gdiplus::Ok,
                "display cache rendering failed");
    }
    displayCache = std::move(next);
    displayCacheSource = &source;
    displayCacheView = view;
    return displayCache.get();
}
HCURSOR App::canvasCursor(UINT dpi) {
    int canvasHit = canvasGrowing ? canvasGrowHit : canvasEdgeHit(canvasMouse);
    if (canvasHit)
        return LoadCursor(nullptr, canvasHit == 5 || canvasHit == 10  ? IDC_SIZENWSE
                                   : canvasHit == 6 || canvasHit == 9 ? IDC_SIZENESW
                                   : canvasHit & 3                    ? IDC_SIZEWE
                                                                      : IDC_SIZENS);
    int textHandle = textMoving ? (textResize ? textResize : 16) : textHit(canvasMouse);
    if (textHandle) {
        auto cursor = textHandle == 5 || textHandle == 10  ? IDC_SIZENWSE
                      : textHandle == 6 || textHandle == 9 ? IDC_SIZENESW
                      : textHandle & 3                     ? IDC_SIZEWE
                      : textHandle & 12                    ? IDC_SIZENS
                                                           : IDC_SIZEALL;
        return LoadCursor(nullptr, cursor);
    }
    if (panning)
        return LoadCursor(nullptr, IDC_SIZEALL);
    if (tool == Tool::PixelSelect) {
        int hit = pixelMoving ? (pixelResize ? pixelResize : 16) : pixelHit(toImage(canvasMouse));
        auto cursor = hit == 5 || hit == 10  ? IDC_SIZENWSE
                      : hit == 6 || hit == 9 ? IDC_SIZENESW
                      : hit & 3              ? IDC_SIZEWE
                      : hit & 12             ? IDC_SIZENS
                      : hit == 16            ? IDC_SIZEALL
                                             : IDC_CROSS;
        return LoadCursor(nullptr, cursor);
    }
    if (tool == Tool::Text)
        return LoadCursor(nullptr, IDC_IBEAM);
    if (tool == Tool::Select) {
        if (dragging)
            return LoadCursor(nullptr, resizing ? IDC_SIZENWSE : IDC_SIZEALL);
        if (doc() && selectedObject >= 0 && selectedObject < (int)doc()->cursor) {
            auto q = toImage(canvasMouse);
            auto r = visibleCommand(*doc(), selectedObject).rect;
            if (inside(r, q))
                return LoadCursor(nullptr, (std::abs(q.x - r.right) < 10 && std::abs(q.y - r.bottom) < 10)
                                               ? IDC_SIZENWSE
                                               : IDC_SIZEALL);
        }
        return LoadCursor(nullptr, IDC_ARROW);
    }
    UINT resource = tool == Tool::Pen         ? 3
                    : tool == Tool::Erase     ? 4
                    : tool == Tool::Highlight ? 5
                    : tool == Tool::Mosaic    ? 6
                                              : 0;
    if (!resource)
        return LoadCursor(nullptr, IDC_CROSS);
    if (!dpi)
        dpi = 96;
    UINT key = resource * 10000 + dpi;
    auto found = penCursors.find(key);
    if (found != penCursors.end())
        return found->second;
    auto cursor = (HCURSOR)LoadImageW(instance, MAKEINTRESOURCEW(resource), IMAGE_CURSOR,
                                      GetSystemMetricsForDpi(SM_CXCURSOR, dpi),
                                      GetSystemMetricsForDpi(SM_CYCURSOR, dpi), LR_DEFAULTCOLOR);
    require(cursor != nullptr, "tool cursor resource could not be loaded");
    penCursors.emplace(key, cursor);
    return cursor;
}
void App::refreshList() {
    refreshHistoryActions();
    int top = (int)SendMessageW(list, LB_GETTOPINDEX, 0, 0);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (auto &d : store.docs) {
        auto size = L"  " + std::to_wstring(d.width) + L" × " + std::to_wstring(d.height);
        SendMessageW(list, LB_ADDSTRING, 0,
                     (LPARAM)(d.name + size + (checkedHistory.count(d.id) ? L" · 체크됨" : L"")).c_str());
    }
    if (selected >= 0)
        SendMessageW(list, LB_SETCURSEL, selected, 0);
    SendMessageW(list, LB_SETTOPINDEX, std::max(0, top), 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, FALSE);
    updateTitle();
    if (widthControl &&
        (((GetWindowLongPtrW(widthControl, GWL_STYLE) & WS_VISIBLE) != 0) != penWidthVisible()))
        layout();
}
void App::select(int index) {
    finishCanvasGrow(true);
    objectPreview.reset();
    objectBackdrop.reset();
    finishText();
    if (index < 0 || index >= (int)store.docs.size())
        return;
    clearPixelSelection();
    store.ensure(store.docs[index]);
    if (selected != index && doc()) {
        doc()->commands.clear();
        doc()->commands.shrink_to_fit();
        doc()->loaded = false;
    }
    if (selected != index || !original)
        original = loadImage(store.png(store.docs[index]));
    selected = index;
    selectedObject = -1;
    lastExport.clear();
    rebuild();
    zoom = 1;
    panX = panY = 0;
    RECT c;
    GetClientRect(canvas, &c);
    auto r = viewRect(*doc());
    zoom = std::min(1.0f, std::min((rw(c) - 48.f) / std::max(1, rw(r)), (rh(c) - 48.f) / std::max(1, rh(r))));
    zoom = std::max(.02f, zoom);
    InvalidateRect(canvas, nullptr, FALSE);
    InvalidateRect(list, nullptr, FALSE);
    updateTitle();
}
void App::rebuild() {
    clearDisplayCache();
    if (doc() && original)
        rendered = render(*original, *doc());
    else
        rendered.reset();
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::persist() {
    if (doc()) {
        store.save(*doc());
        thumbs.erase(doc()->id);
        InvalidateRect(list, nullptr, FALSE);
    }
}
void App::add(Image image, const std::wstring &name) {
    clearDisplayCache();
    finishText();
    require(image != nullptr, "empty image");
    store.add(*image, name);
    selected = 0;
    original = std::move(image);
    rendered = clone(*original);
    refreshList();
    if (!testMode)
        show();
    select(0);
    statusText(L"캡처 완료 · 편집 후 저장 버튼으로 저장 위치를 선택하세요.");
}
void App::newCanvas() {
    if (busy)
        return;
    auto image = std::make_unique<Bitmap>(1280, 720, PixelFormat32bppARGB);
    require(image->GetLastStatus() == Gdiplus::Ok, "blank canvas allocation failed");
    {
        Graphics g(image.get());
        require(g.Clear(Color::White) == Gdiplus::Ok, "blank canvas initialization failed");
    }
    setTool(Tool::Pen);
    add(std::move(image), L"그림판");
    SendMessageW(list, LB_SETTOPINDEX, 0, 0);
    if (!testMode)
        SetFocus(canvas);
    statusText(L"새 그림판 1280 × 720 · 펜으로 그리세요 · 왼쪽 막대: 굵기 · Ctrl+S: 저장");
}
void App::edit(Command c) {
    if (!doc())
        return;
    clearPixelSelection();
    commit(*doc(), std::move(c));
    selectedObject = (int)doc()->cursor - 1;
    if (doc()->commands.back().tool == Tool::CanvasSize || doc()->commands.back().tool == Tool::CanvasPad)
        selectedObject = -1;
    persist();
    rebuild();
}
void App::undo(bool redo) {
    if (!doc())
        return;
    clearPixelSelection();
    if (redo) {
        if (doc()->cursor < doc()->commands.size())
            doc()->cursor++;
    } else if (doc()->cursor)
        doc()->cursor--;
    selectedObject = -1;
    persist();
    rebuild();
    updateTitle();
}
RECT App::imageBounds() {
    if (canvasGrowing)
        return {
            canvasStartBounds.left + (LONG)std::lround((canvasGrowRect.left - canvasBeforeView.left) * zoom),
            canvasStartBounds.top + (LONG)std::lround((canvasGrowRect.top - canvasBeforeView.top) * zoom),
            canvasStartBounds.left + (LONG)std::lround((canvasGrowRect.right - canvasBeforeView.left) * zoom),
            canvasStartBounds.top + (LONG)std::lround((canvasGrowRect.bottom - canvasBeforeView.top) * zoom)};
    if (!doc())
        return {};
    RECT c;
    GetClientRect(canvas, &c);
    auto r = viewRect(*doc());
    int w = std::lround(rw(r) * zoom), h = std::lround(rh(r) * zoom);
    int x = std::max(24, (rw(c) - w) / 2) + panX, y = std::max(24, (rh(c) - h) / 2) + panY;
    return {x, y, x + w, y + h};
}
POINT App::toImage(POINT p) {
    auto b = imageBounds(), r = doc() ? viewRect(*doc()) : RECT{};
    return {LONG(std::floor((p.x - b.left) / zoom)) + r.left, LONG(std::floor((p.y - b.top) / zoom)) + r.top};
}
void App::setZoom(float factor) {
    finishText();
    zoom = std::clamp(factor, .02f, 8.f);
    panX = panY = 0;
    InvalidateRect(canvas, nullptr, FALSE);
    updateTitle();
}
void App::paintCanvas(HDC dc) {
    RECT c;
    GetClientRect(canvas, &c);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP hb = CreateCompatibleBitmap(dc, std::max(1, rw(c)), std::max(1, rh(c)));
    auto old = SelectObject(mem, hb);
    {
        Graphics g(mem);
        g.Clear(Color(theme::Canvas));
        if (rendered && doc()) {
            auto b = imageBounds(), r = viewRect(*doc());
            auto imageTarget = canvasGrowing ? canvasStartBounds : b;
            Gdiplus::SolidBrush white(Color::White);
            g.FillRectangle(&white, b.left, b.top, rw(b), rh(b));
            g.SetInterpolationMode(zoom >= 1 ? Gdiplus::InterpolationModeNearestNeighbor
                                             : Gdiplus::InterpolationModeHighQualityBicubic);
            auto source = dragging && objectBackdrop     ? objectBackdrop.get()
                          : pixelMoving && pixelBackdrop ? pixelBackdrop.get()
                          : textInput && textBackdrop    ? textBackdrop.get()
                                                         : rendered.get();
            if (auto cached = scaledDisplay(*source, r, rw(imageTarget), rh(imageTarget)))
                g.DrawImage(cached, imageTarget.left, imageTarget.top, (int)cached->GetWidth(),
                            (int)cached->GetHeight());
            else
                g.DrawImage(
                    source,
                    Gdiplus::Rect(imageTarget.left, imageTarget.top, rw(imageTarget), rh(imageTarget)),
                    r.left, r.top, rw(r), rh(r), Gdiplus::UnitPixel);
            if (dragging) {
                auto state = g.Save();
                g.SetClip(Gdiplus::Rect(b.left, b.top, rw(b), rh(b)));
                g.TranslateTransform((float)b.left, (float)b.top);
                g.ScaleTransform(zoom, zoom);
                g.TranslateTransform((float)-r.left, (float)-r.top);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                if (draft.tool == Tool::Crop || draft.tool == Tool::Mosaic || draft.tool == Tool::Text) {
                    Gdiplus::Pen p(Color(255, 37, 99, 235), 2 / zoom);
                    p.SetDashStyle(Gdiplus::DashStyleDash);
                    auto q = draft.rect;
                    g.DrawRectangle(&p, q.left, q.top, rw(q), rh(q));
                } else if (draft.tool == Tool::PasteImage && objectPreview)
                    drawPixelPatch(g, *objectPreview,
                                   {(float)draft.rect.left, (float)draft.rect.top, (float)rw(draft.rect),
                                    (float)rh(draft.rect)});
                else
                    drawCommand(g, draft);
                g.Restore(state);
            }
            if (textInput) {
                auto inner = textScreenRect(), rect = textFrameRect();
                Gdiplus::SolidBrush frame(Color(theme::Tint));
                g.FillRectangle(&frame, (int)rect.left, (int)rect.top, rw(rect), inner.top - rect.top);
                g.FillRectangle(&frame, (int)rect.left, (int)inner.bottom, rw(rect),
                                rect.bottom - inner.bottom);
                g.FillRectangle(&frame, (int)rect.left, (int)inner.top, inner.left - rect.left, rh(inner));
                g.FillRectangle(&frame, (int)inner.right, (int)inner.top, rect.right - inner.right,
                                rh(inner));
                Gdiplus::Pen outline(Color(theme::Blue), 1);
                outline.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawRectangle(&outline, (int)rect.left, (int)rect.top, rw(rect), rh(rect));
                float size = (float)MulDiv(6, GetDpiForWindow(canvas), 96);
                Gdiplus::SolidBrush handle(Color::White);
                Gdiplus::Pen edge(Color(theme::Blue), 1);
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 3; ++col) {
                        if (row == 1 && col == 1)
                            continue;
                        float x = rect.left + rw(rect) * col / 2.f, y = rect.top + rh(rect) * row / 2.f;
                        g.FillRectangle(&handle, x - size / 2, y - size / 2, size, size);
                        g.DrawRectangle(&edge, x - size / 2, y - size / 2, size, size);
                    }
            }
            Gdiplus::Pen imageEdge(Color(theme::Line), 1);
            g.DrawRectangle(&imageEdge, (int)b.left - 1, (int)b.top - 1, rw(b) + 1, rh(b) + 1);
            int edge = canvasGrowing ? canvasGrowHit : canvasEdgeHover;
            if (edge) {
                Gdiplus::Pen accent(Color(theme::Blue), 2);
                if (edge & 1)
                    g.DrawLine(&accent, (int)b.left, (int)b.top, (int)b.left, (int)b.bottom);
                if (edge & 4)
                    g.DrawLine(&accent, (int)b.left, (int)b.top, (int)b.right, (int)b.top);
                if (edge & 2)
                    g.DrawLine(&accent, (int)b.right, (int)b.top, (int)b.right, (int)b.bottom);
                if (edge & 8)
                    g.DrawLine(&accent, (int)b.left, (int)b.bottom, (int)b.right, (int)b.bottom);
                float x = edge & 1 ? (float)b.left : edge & 2 ? (float)b.right : (b.left + b.right) / 2.f;
                float y = edge & 4 ? (float)b.top : edge & 8 ? (float)b.bottom : (b.top + b.bottom) / 2.f;
                Gdiplus::SolidBrush knob(Color(theme::Blue));
                g.FillEllipse(&knob, x - 3, y - 3, 6.f, 6.f);
            }
            paintPixelSelection(g, b, r);
            if (!dragging && tool == Tool::Select && selectedObject >= 0 &&
                selectedObject < (int)doc()->cursor) {
                auto q = visibleCommand(*doc(), selectedObject).rect;
                int x = b.left + (q.left - r.left) * zoom, y = b.top + (q.top - r.top) * zoom;
                int w = std::max(1, (int)(rw(q) * zoom)), h = std::max(1, (int)(rh(q) * zoom));
                Gdiplus::Pen outline(Color(255, 37, 99, 235), 1);
                outline.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawRectangle(&outline, x, y, w, h);
                Gdiplus::SolidBrush handle(Color(255, 37, 99, 235));
                g.FillRectangle(&handle, x + w - 4, y + h - 4, 8, 8);
            }
        } else {
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, RGB(56, 65, 80));
            SelectObject(mem, uiFont);
            RECT text{32, 65, rw(c) - 32, 180};
            DrawTextW(mem,
                      L"Ctrl + Shift + X 로 캡처하거나 상단 그림판 버튼으로 그리기를 시작하세요.\n\n오른쪽 "
                      L"목록에서 이전 캡처를 선택하거나 파일을 "
                      L"열 수 있습니다.",
                      -1, &text, DT_CENTER | DT_WORDBREAK);
        }
    }
    BitBlt(dc, 0, 0, rw(c), rh(c), mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(hb);
    DeleteDC(mem);
}
void App::paintList(DRAWITEMSTRUCT *item) {
    if (item->itemID == (UINT)-1 || item->itemID >= store.docs.size())
        return;
    auto &d = store.docs[item->itemID];
    auto r = item->rcItem;
    auto dpi = GetDpiForWindow(list);
    auto scale = [&](int v) { return MulDiv(v, dpi ? dpi : 96, 96); };
    bool checked = checkedHistory.count(d.id) != 0;
    bool current = (int)item->itemID == selected;
    Graphics surface(item->hDC);
    surface.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush rowBackground(Color(theme::Sidebar));
    surface.FillRectangle(&rowBackground, (int)r.left, (int)r.top, rw(r), rh(r));
    theme::rounded(
        surface,
        {(float)r.left + scale(2), (float)r.top + scale(3), (float)rw(r) - scale(4), (float)rh(r) - scale(7)},
        (float)scale(7), checked ? theme::Tint : theme::Sidebar,
        (current || (item->itemState & ODS_FOCUS)) ? 0xff89b6fb
        : checked                                  ? 0xffc7dbfb
                                                   : 0);
    if (!thumbs.count(d.id)) {
        try {
            auto b = loadImage(store.png(d));
            auto detail = d.loaded ? d : store.read(d);
            auto v = output(*b, detail);
            int w = std::max(1, std::min(256, (int)v->GetWidth())),
                h = std::max(1, (int)(v->GetHeight() * (w / (float)v->GetWidth())));
            if (h > 144) {
                w = std::max(1, w * 144 / h);
                h = 144;
            }
            auto thumb = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
            {
                Graphics g(thumb.get());
                g.Clear(Color::White);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(v.get(), 0, 0, w, h);
            }
            thumbs[d.id] = std::move(thumb);
            if (thumbs.size() > 24) {
                auto it = thumbs.begin();
                if (it->first == d.id)
                    ++it;
                if (it != thumbs.end())
                    thumbs.erase(it);
            }
        } catch (...) {
        }
    }
    if (thumbs.count(d.id)) {
        auto t = thumbs[d.id].get();
        auto image = historyThumbnailRect(r, dpi, t->GetWidth(), t->GetHeight());
        surface.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        Gdiplus::SolidBrush white(Color::White);
        surface.FillRectangle(&white, (int)image.left, (int)image.top, rw(image), rh(image));
        surface.DrawImage(t, (int)image.left, (int)image.top, rw(image), rh(image));
        Gdiplus::Pen imageBorder(Color(0xffd2dce8), 1);
        surface.DrawRectangle(&imageBorder, (int)image.left - 1, (int)image.top - 1, rw(image) + 1,
                              rh(image) + 1);
    }
    auto checkbox = historyCheckRect(r, dpi);
    theme::rounded(
        surface, {(float)checkbox.left, (float)checkbox.top, (float)rw(checkbox), (float)rh(checkbox)},
        (float)scale(3), checked ? theme::Blue : theme::Window, checked ? theme::Blue : 0xffaab6c6);
    if (checked) {
        Gdiplus::Pen tick(Color::White, (float)scale(2));
        surface.DrawLine(&tick, (int)checkbox.left + scale(4), (int)checkbox.top + scale(9),
                         (int)checkbox.left + scale(8), (int)checkbox.top + scale(13));
        surface.DrawLine(&tick, (int)checkbox.left + scale(8), (int)checkbox.top + scale(13),
                         (int)checkbox.left + scale(14), (int)checkbox.top + scale(5));
    }
    auto close = historyCloseRect(r, dpi);
    if (pressedClose == d.id)
        theme::rounded(surface, {(float)close.left, (float)close.top, (float)rw(close), (float)rh(close)},
                       (float)scale(4), 0xfffee2e2);
    Gdiplus::Pen cross(Color(theme::Muted), 1.5f);
    int inset = scale(8);
    surface.DrawLine(&cross, (int)close.left + inset, (int)close.top + inset, (int)close.right - inset,
                     (int)close.bottom - inset);
    surface.DrawLine(&cross, (int)close.right - inset, (int)close.top + inset, (int)close.left + inset,
                     (int)close.bottom - inset);
}

RECT App::historyCloseRect(RECT row, UINT dpi) {
    int size = MulDiv(24, dpi ? dpi : 96, 96), gap = MulDiv(6, dpi ? dpi : 96, 96);
    int top = row.bottom - MulDiv(10, dpi ? dpi : 96, 96) - size;
    return {row.right - gap - size, top, row.right - gap, top + size};
}
int App::historyCloseHit(POINT p) {
    RECT client{};
    GetClientRect(list, &client);
    if (!inside(client, p))
        return -1;
    auto hit = (DWORD)SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(p.x, p.y));
    int index = LOWORD(hit);
    if (HIWORD(hit) || index >= (int)store.docs.size())
        return -1;
    RECT row{};
    if (SendMessageW(list, LB_GETITEMRECT, index, (LPARAM)&row) == LB_ERR)
        return -1;
    return inside(historyCloseRect(row, GetDpiForWindow(list)), p) ? index : -1;
}
void App::removeHistory(int index) {
    finishText();
    if (index < 0 || index >= (int)store.docs.size())
        return;
    auto id = store.docs[index].id;
    bool removingCurrent = index == selected;
    store.remove(index);
    thumbs.erase(id);
    if (removingCurrent) {
        clearPixelSelection();
        original.reset();
        rendered.reset();
        selectedObject = -1;
        lastExport.clear();
        selected = -1;
        if (!store.docs.empty())
            select(std::min(index, (int)store.docs.size() - 1));
    } else if (index < selected)
        --selected;
    refreshList();
    if (selected >= 0)
        SendMessageW(list, LB_SETCURSEL, selected, 0);
    InvalidateRect(canvas, nullptr, FALSE);
    statusText(L"목록에서 제거했습니다. 직접 저장한 이미지 파일은 유지됩니다.");
}
LRESULT CALLBACK App::listProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto a = (App *)data;
    try {
        if (msg == WM_KEYDOWN && w == VK_SPACE) {
            int index = (int)SendMessageW(h, LB_GETCURSEL, 0, 0);
            a->checkHistory(index, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        if (msg == WM_LBUTTONUP && a->swallowListUp) {
            a->swallowListUp = false;
            return 0;
        }
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
            a->swallowListUp = false;
            int index = a->historyCloseHit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            if (index >= 0) {
                a->pressedClose = a->store.docs[index].id;
                SetCapture(h);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            index = a->historyItemAt(point);
            if (index >= 0) {
                bool range = (w & MK_SHIFT) || (GetKeyState(VK_SHIFT) & 0x8000);
                bool checkbox = a->historyCheckHit(point) >= 0;
                // Range checking uses the previous anchor before changing the active photo.
                a->checkHistory(index, range);
                if (!checkbox) {
                    if (a->selected != index)
                        a->select(index);
                    SendMessageW(h, LB_SETCURSEL, index, 0);
                    SetFocus(h);
                }
                a->swallowListUp = true;
                return 0;
            }
        } else if (msg == WM_LBUTTONUP && !a->pressedClose.empty()) {
            int index = a->historyCloseHit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            auto pressed = std::move(a->pressedClose);
            a->pressedClose.clear();
            ReleaseCapture();
            if (index >= 0 && a->store.docs[index].id == pressed)
                a->removeHistory(index);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        } else if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) {
            a->pressedClose.clear();
            InvalidateRect(h, nullptr, FALSE);
        } else if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, listProc, id);
    } catch (const std::exception &e) {
        a->pressedClose.clear();
        ReleaseCapture();
        a->report(e);
    }
    return DefSubclassProc(h, msg, w, l);
}
void App::canvasDown(POINT p) {
    if (!doc() || !rendered)
        return;
    if (textFrameHit(p)) {
        textMoving = true;
        textResize = textHit(p) == 16 ? 0 : textHit(p);
        textMoveRect = textDraft.rect;
        textMoveStart = toImage(p);
        SetCapture(canvas);
        return;
    }
    finishText();
    if (!testMode)
        SetFocus(canvas);
    if (!testMode && (GetKeyState(VK_SPACE) & 0x8000)) {
        panning = true;
        dragOrigin = p;
        panOrigin = {panX, panY};
        SetCapture(canvas);
        return;
    }
    if (int edge = canvasEdgeHit(p)) {
        clearPixelSelection();
        canvasBeforeView = canvasGrowRect = viewRect(*doc());
        canvasStartBounds = imageBounds();
        canvasGrowHit = edge;
        canvasGrowing = true;
        SetCapture(canvas);
        return;
    }
    if (tool == Tool::PixelSelect && pixelHit(toImage(p))) {
        pixelDown(toImage(p));
        return;
    }
    if (!inside(imageBounds(), p)) {
        if (tool == Tool::Select) {
            selectedObject = -1;
            InvalidateRect(canvas, nullptr, FALSE);
        }
        if (tool == Tool::PixelSelect)
            clearPixelSelection();
        return;
    }
    POINT q = toImage(p);
    if (tool == Tool::PixelSelect) {
        pixelDown(q);
        return;
    }
    if (tool == Tool::Select) {
        statusText(L"드래그: 이동 · Shift+드래그 또는 오른쪽 아래 모서리: 크기 · 더블클릭: 텍스트 수정");
        selectedObject = -1;
        auto hidden = erased(*doc());
        for (int i = (int)doc()->cursor - 1; i >= 0; i--) {
            auto c = visibleCommand(*doc(), i);
            if (!hidden[i] && c.tool == Tool::PixelSelect)
                break; // Earlier annotations are now part of the raster edit.
            RECT hit = c.rect;
            InflateRect(&hit, 8, 8);
            if (!hidden[i] && c.tool != Tool::Crop && c.tool != Tool::Erase && c.tool != Tool::Reset &&
                c.tool != Tool::PixelSelect && c.tool != Tool::CanvasSize && c.tool != Tool::CanvasPad &&
                inside(hit, q)) {
                draft = c;
                draft.target = i;
                selectedObject = i;
                resizing = (!testMode && (GetKeyState(VK_SHIFT) & 0x8000)) ||
                           (std::abs(q.x - c.rect.right) < 10 && std::abs(q.y - c.rect.bottom) < 10);
                start = q;
                dragging = true;
                if (c.tool == Tool::PasteImage && c.imageData) {
                    clearDisplayCache();
                    objectPreview = decodeImageBytes(*c.imageData);
                    auto background = *doc();
                    Command erase;
                    erase.tool = Tool::Erase;
                    erase.target = i;
                    commit(background, erase);
                    objectBackdrop = render(*original, background);
                }
                SetCapture(canvas);
                break;
            }
        }
        InvalidateRect(canvas, nullptr, FALSE);
        return;
    }
    if (tool == Tool::Erase) {
        eraseAt(q);
        return;
    }
    draft = {};
    draft.tool = tool;
    draft.color = color;
    draft.width = tool == Tool::Mosaic ? std::max(8.f, stroke * 4) : stroke;
    draft.fill = fill;
    draft.font = fontName;
    draft.fontSize = fontSize;
    draft.fontStyle = textStyle;
    draft.rect = {q.x, q.y, q.x, q.y};
    draft.points = {q};
    start = q;
    dragging = true;
    SetCapture(canvas);
}
void App::canvasMove(POINT p) {
    canvasMouse = p;
    if (canvasGrowing) {
        growCanvas(p);
        return;
    }
    int hover = canvasEdgeHit(p);
    if (hover != canvasEdgeHover) {
        canvasEdgeHover = hover;
        InvalidateRect(canvas, nullptr, FALSE);
    }
    if (!testMode && GetCapture() == canvas)
        SetCursor(canvasCursor(GetDpiForWindow(canvas)));
    if (textMoving) {
        moveText(p);
        return;
    }
    if (panning) {
        panX = panOrigin.x + p.x - dragOrigin.x;
        panY = panOrigin.y + p.y - dragOrigin.y;
        positionText();
        InvalidateRect(canvas, nullptr, FALSE);
        return;
    }
    if (pixelSelecting || pixelMoving) {
        pixelMove(toImage(p));
        return;
    }
    if (!dragging)
        return;
    POINT q = toImage(p);
    auto vr = viewRect(*doc());
    q.x = std::clamp(q.x, vr.left, vr.right);
    q.y = std::clamp(q.y, vr.top, vr.bottom);
    if (tool == Tool::Select) {
        int target = draft.target;
        auto c = visibleCommand(*doc(), target);
        draft = c;
        draft.target = target;
        if (resizing) {
            draft.rect.right = std::max(draft.rect.left + 1, q.x);
            draft.rect.bottom = std::max(draft.rect.top + 1, q.y);
            float sx = rw(draft.rect) / (float)std::max(1, rw(c.rect));
            float sy = rh(draft.rect) / (float)std::max(1, rh(c.rect));
            for (auto &pt : draft.points) {
                pt.x = c.rect.left + (pt.x - c.rect.left) * sx;
                pt.y = c.rect.top + (pt.y - c.rect.top) * sy;
            }
            if (draft.tool == Tool::Text)
                draft.fontSize = std::clamp(c.fontSize * sy, 1.f, 200.f);
        } else {
            int dx = q.x - start.x, dy = q.y - start.y;
            if (c.tool == Tool::PasteImage) {
                dx = std::clamp<LONG>(dx, vr.left - c.rect.left, vr.right - c.rect.right);
                dy = std::clamp<LONG>(dy, vr.top - c.rect.top, vr.bottom - c.rect.bottom);
            }
            OffsetRect(&draft.rect, dx, dy);
            for (auto &pt : draft.points) {
                pt.x += q.x - start.x;
                pt.y += q.y - start.y;
            }
        }
    } else {
        if (tool == Tool::Pen) {
            draft.rect.left = std::min(draft.rect.left, q.x);
            draft.rect.top = std::min(draft.rect.top, q.y);
            draft.rect.right = std::max(draft.rect.right, q.x);
            draft.rect.bottom = std::max(draft.rect.bottom, q.y);
            draft.points.push_back(q);
        } else {
            draft.rect = normalized(start, q);
            draft.points = {start, q};
        }
    }
    InvalidateRect(canvas, nullptr, FALSE);
}
void App::canvasUp(POINT p) {
    if (canvasGrowing) {
        growCanvas(p);
        finishCanvasGrow();
        return;
    }
    if (textMoving) {
        moveText(p);
        endTextMove();
        return;
    }
    if (pixelSelecting || pixelMoving) {
        pixelMove(toImage(p));
        pixelUp();
        return;
    }
    if (panning) {
        panning = false;
        ReleaseCapture();
        return;
    }
    if (!dragging)
        return;
    canvasMove(p);
    dragging = false;
    ReleaseCapture();
    objectPreview.reset();
    objectBackdrop.reset();
    if (tool == Tool::Text)
        startText(draft);
    else if (tool == Tool::Select)
        edit(draft);
    else if (tool == Tool::Pen || tool == Tool::Line || tool == Tool::Arrow ||
             (rw(draft.rect) > 0 && rh(draft.rect) > 0))
        edit(draft);
    InvalidateRect(canvas, nullptr, FALSE);
    updateTitle();
}
void App::deleteObject() {
    if (!doc() || selectedObject < 0 || selectedObject >= (int)doc()->cursor)
        return;
    Command erase;
    erase.tool = Tool::Erase;
    erase.target = selectedObject;
    edit(erase);
    selectedObject = -1;
}
void App::duplicateObject() {
    if (!doc() || selectedObject < 0 || selectedObject >= (int)doc()->cursor)
        return;
    auto c = visibleCommand(*doc(), selectedObject);
    c.target = -1;
    OffsetRect(&c.rect, 16, 16);
    for (auto &p : c.points) {
        p.x += 16;
        p.y += 16;
    }
    edit(c);
}
void App::editText() {
    if (!doc() || selectedObject < 0 || selectedObject >= (int)doc()->cursor)
        return;
    auto c = visibleCommand(*doc(), selectedObject);
    if (c.tool != Tool::Text)
        return;
    c.target = selectedObject;
    startText(std::move(c));
}
void App::eraseAt(POINT p) {
    auto skip = erased(*doc());
    for (int i = (int)doc()->cursor - 1; i >= 0; i--) {
        auto c = visibleCommand(*doc(), i);
        if (!skip[i] && c.tool == Tool::PixelSelect)
            break;
        if (c.tool == Tool::Crop || c.tool == Tool::Erase || c.tool == Tool::Reset ||
            c.tool == Tool::PixelSelect || c.tool == Tool::CanvasSize || c.tool == Tool::CanvasPad || skip[i])
            continue;
        RECT hit = c.rect;
        InflateRect(&hit, 10, 10);
        if (inside(hit, p)) {
            Command erase;
            erase.tool = Tool::Erase;
            erase.target = i;
            edit(erase);
            return;
        }
    }
}
void App::deleteSelected() {
    deleteCheckedHistory();
}
void App::copy() {
    finishText();
    if (!doc()) {
        statusText(L"복사할 캡처가 없습니다.");
        return;
    }
    if (!original)
        select(selected);
    auto image =
        tool == Tool::PixelSelect && pixelReady ? crop(*rendered, pixelRect) : output(*original, *doc());
    auto formats = makeClipboardImage(*image);
    if (testMode) {
        require((bool)clipboardVerifier, "clipboard test verifier missing");
        clipboardVerifier(formats);
        return;
    }
    if (!publishClipboardImage(main, formats)) {
        statusText(L"클립보드를 다른 앱이 사용 중입니다. 잠시 후 Ctrl+C를 다시 누르세요.");
        return;
    }
    statusText(L"편집 결과를 복사했습니다. Ctrl+V로 붙여넣으세요.");
}
static std::wstring saveDialog(HWND owner, const std::wstring &proposed) {
    wchar_t path[32768]{};
    auto initial =
        fs::exists(fs::path(proposed).parent_path()) ? fs::path(proposed) : fs::path(proposed).filename();
    wcsncpy_s(path, initial.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PNG 이미지 (*.png)\0*.png\0JPEG 이미지 (*.jpg)\0*.jpg\0비트맵 (*.bmp)\0*.bmp\0GIF "
                      L"이미지 (*.gif)\0*.gif\0TIFF 이미지 (*.tiff)\0*.tiff\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    auto ext = fs::path(proposed).extension().wstring();
    for (int i = 0; i < 5; i++)
        if (ext == imageExtension(i))
            ofn.nFilterIndex = i + 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"png";
    if (!GetSaveFileNameW(&ofn))
        return L"";
    fs::path result = path;
    result.replace_extension(imageExtension((int)ofn.nFilterIndex - 1));
    if (result != fs::path(path) && fs::exists(result) &&
        MessageBoxW(owner, L"선택한 형식의 파일이 이미 있습니다. 덮어쓸까요?", AppName,
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return L"";
    return result.wstring();
}
void App::save(bool saveAs) {
    finishText();
    if (!doc() || !original)
        return;
    (void)saveAs;
    auto proposed = lastExport.empty() ? exportPath(store.settings) : lastExport;
    fs::path target = (testMode && savePathPicker) ? savePathPicker(proposed.wstring())
                                                   : saveDialog(main, proposed.wstring());
    if (target.empty())
        return;
    auto image = output(*original, *doc());
    saveImage(*image, target, store.settings.jpeg);
    lastExport = target;
    statusText(L"저장 완료: " + target.wstring());
}
void App::print() {
    if (!doc() || !original)
        return;
    PRINTDLGW pd{sizeof(pd)};
    pd.hwndOwner = main;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgW(&pd))
        return;
    auto image = output(*original, *doc());
    DOCINFOW di{sizeof(di)};
    di.lpszDocName = L"LightCapture";
    if (StartDocW(pd.hDC, &di) > 0) {
        StartPage(pd.hDC);
        {
            Graphics g(pd.hDC);
            int w = GetDeviceCaps(pd.hDC, HORZRES), h = GetDeviceCaps(pd.hDC, VERTRES);
            float ratio = std::min((float)w / image->GetWidth(), (float)h / image->GetHeight());
            int iw = image->GetWidth() * ratio, ih = image->GetHeight() * ratio;
            g.DrawImage(image.get(), (w - iw) / 2, (h - ih) / 2, iw, ih);
        }
        EndPage(pd.hDC);
        EndDoc(pd.hDC);
    }
    DeleteDC(pd.hDC);
    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);
}
void App::open() {
    wchar_t path[32768]{};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = main;
    ofn.lpstrFilter = L"이미지\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn))
        add(loadImage(path));
}
LRESULT CALLBACK App::canvasProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        a = (App *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        switch (m) {
        case WM_CTLCOLOREDIT:
            if ((HWND)l == a->textInput && a->textBrush) {
                SetBkMode((HDC)w, TRANSPARENT);
                SetTextColor((HDC)w, RGB((a->textDraft.color >> 16) & 255, (a->textDraft.color >> 8) & 255,
                                         a->textDraft.color & 255));
                POINT origin{};
                LPtoDP((HDC)w, &origin, 1);
                SetBrushOrgEx((HDC)w, origin.x, origin.y, nullptr);
                return (LRESULT)a->textBrush;
            }
            break;
        case WM_COMMAND:
            if ((HWND)l == a->textInput) {
                if (HIWORD(w) == EN_CHANGE)
                    a->growText();
                if (HIWORD(w) == EN_CHANGE || HIWORD(w) == EN_UPDATE || HIWORD(w) == EN_VSCROLL)
                    InvalidateRect(a->textInput, nullptr, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            a->paintCanvas(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            a->paintCanvas((HDC)w);
            return 0;
        case WM_LBUTTONDBLCLK:
            if (a->tool == Tool::PixelSelect)
                a->canvasDown({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            else
                a->editText();
            return 0;
        case WM_LBUTTONDOWN:
            a->canvasDown({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
        }
            a->canvasMove({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            return 0;
        case WM_MOUSELEAVE:
            a->canvasEdgeHover = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            a->canvasUp({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            return 0;
        case WM_MBUTTONDOWN:
            a->panning = true;
            a->dragOrigin = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            a->panOrigin = {a->panX, a->panY};
            SetCapture(h);
            return 0;
        case WM_MBUTTONUP:
            a->panning = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            a->finishText();
            if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL)
                a->setZoom(a->zoom * (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1.2f : 1 / 1.2f));
            else {
                if (GET_KEYSTATE_WPARAM(w) & MK_SHIFT)
                    a->panX += GET_WHEEL_DELTA_WPARAM(w) / 2;
                else
                    a->panY += GET_WHEEL_DELTA_WPARAM(w) / 2;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
            if (w == VK_DELETE && a->tool == Tool::PixelSelect) {
                a->deletePixelSelection();
                return 0;
            }
            if (w == VK_ESCAPE) {
                if (a->canvasGrowing) {
                    a->finishCanvasGrow(true);
                    return 0;
                }
                a->objectPreview.reset();
                a->objectBackdrop.reset();
                a->selectedObject = -1;
                if (a->textMoving) {
                    a->endTextMove(true);
                    return 0;
                }
                a->clearPixelSelection();
                a->dragging = false;
                a->panning = false;
                ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            a->finishCanvasGrow(true);
            if (a->dragging && a->objectPreview) {
                a->dragging = false;
                a->objectPreview.reset();
                a->objectBackdrop.reset();
                InvalidateRect(h, nullptr, FALSE);
            }
            a->endTextMove(true);
            if (a->pixelMoving || a->pixelSelecting)
                a->clearPixelSelection();
            return 0;
        case WM_SIZE:
            a->positionText(true);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            GetCursorPos(&a->canvasMouse);
            ScreenToClient(h, &a->canvasMouse);
            SetCursor(a->canvasCursor(GetDpiForWindow(h)));
            return TRUE;
        }
    } catch (const std::exception &e) {
        a->dragging = false;
        ReleaseCapture();
        a->report(e);
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace lc
