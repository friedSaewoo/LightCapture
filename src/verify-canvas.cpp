#include "app.h"
namespace lc {
static void canvasPreview(HWND h, const fs::path &path) {
    RECT r{}, outer{};
    GetClientRect(h, &r);
    GetWindowRect(h, &outer);
    POINT client{};
    ClientToScreen(h, &client);
    HDC dc = GetDC(h), mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rw(r), rh(r));
    auto old = SelectObject(mem, bitmap);
    FillRect(mem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetViewportOrgEx(mem, outer.left - client.x, outer.top - client.y, nullptr);
    SendMessageW(h, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    auto image = Image(Bitmap::FromHBITMAP(bitmap, nullptr));
    saveImage(*image, path);
    image.reset();
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(h, dc);
}
void App::verifyCanvas() {
    int checks = 0;
    auto check = [&](bool ok, const char *name) {
        require(ok, name);
        ++checks;
    };
    auto color = [](Bitmap &b, int x, int y) {
        Color p;
        require(b.GetPixel(x, y, &p) == Gdiplus::Ok, "canvas test pixel unavailable");
        return p.GetValue();
    };
    auto image = std::make_unique<Bitmap>(120, 80, PixelFormat32bppARGB);
    {
        Graphics g(image.get());
        g.Clear(Color(0xfff7ab2f));
    }
    add(std::move(image));
    setTool(Tool::Pen);
    zoom = 1;
    panX = panY = 0;
    Command mark;
    mark.tool = Tool::Rectangle;
    mark.rect = {10, 10, 30, 30};
    mark.color = 0xff2563eb;
    mark.fill = true;
    edit(mark);
    auto base = doc()->cursor;
    auto point = [&](int x, int y) {
        auto b = imageBounds(), v = viewRect(*doc());
        return POINT{b.left + (LONG)std::lround((x - v.left) * zoom),
                     b.top + (LONG)std::lround((y - v.top) * zoom)};
    };
    auto b = imageBounds();
    check(canvasEdgeHit({b.right, (b.top + b.bottom) / 2}) == 2 &&
              canvasEdgeHit({(b.left + b.right) / 2, b.bottom}) == 8 &&
              canvasEdgeHit({b.right, b.bottom}) == 10 && canvasEdgeHit({b.left, b.top}) == 0,
          "canvas edge hit directions mismatch");
    canvasMove({b.right, b.bottom});
    check(canvasEdgeHover == 10 && canvasCursor(96) == LoadCursor(nullptr, IDC_SIZENWSE),
          "canvas edge hover cursor missing");
    canvasDown({b.right, (b.top + b.bottom) / 2});
    canvasMove({b.right + 80, (b.top + b.bottom) / 2});
    check(canvasGrowing && doc()->cursor == base && rw(canvasGrowRect) == 200,
          "canvas growth preview mutated history or stretched bounds");
    canvasUp({b.right + 80, (b.top + b.bottom) / 2});
    check(!canvasGrowing && rw(viewRect(*doc())) == 200 && rh(viewRect(*doc())) == 80 &&
              original->GetWidth() == 120 && rendered->GetWidth() == 200 && doc()->cursor == base + 1,
          "canvas right expansion lost original dimensions or history");
    check(color(*rendered, 20, 20) == mark.color && color(*rendered, 190, 40) == 0xffffffff &&
              color(*rendered, 50, 50) == 0xfff7ab2f,
          "canvas expansion scaled old pixels or failed to add white space");
    b = imageBounds();
    canvasDown({b.right, b.bottom});
    canvasUp({b.right + 40, b.bottom + 30});
    check(rw(viewRect(*doc())) == 240 && rh(viewRect(*doc())) == 110, "canvas corner expansion failed");
    mark.rect = {210, 90, 230, 105};
    mark.color = 0xff16a34a;
    edit(mark);
    check(color(*rendered, 215, 95) == mark.color, "expanded white area cannot be edited");
    command(ID_UNDO);
    command(ID_UNDO);
    check(rw(viewRect(*doc())) == 200 && rh(viewRect(*doc())) == 80,
          "undo did not restore previous canvas dimensions");
    command(ID_UNDO);
    check(rw(viewRect(*doc())) == 120 && rendered->GetWidth() == 120 &&
              color(*rendered, 20, 20) == 0xff2563eb,
          "undo expansion damaged original edits");
    undo(true);
    undo(true);
    b = imageBounds();
    auto count = doc()->cursor;
    canvasDown({b.right, b.bottom});
    canvasMove({b.right + 30, b.bottom + 30});
    canvasProc(canvas, WM_KEYDOWN, VK_ESCAPE, 0);
    check(!canvasGrowing && doc()->cursor == count && rw(viewRect(*doc())) == 240,
          "Esc did not cancel canvas growth");
    b = imageBounds();
    canvasDown({b.right, b.bottom});
    canvasUp({b.right - 50, b.bottom - 50});
    check(doc()->cursor == count && rw(viewRect(*doc())) == 240,
          "inward canvas drag cropped existing pixels");
    Store expandedStore(store.root);
    auto expanded = expandedStore.read(*doc());
    auto originalFile = loadImage(expandedStore.png(expanded));
    auto expandedOutput = output(*originalFile, expanded);
    check(expandedOutput->GetWidth() == 240 && expandedOutput->GetHeight() == 110 &&
              color(*expandedOutput, 230, 100) == 0xffffffff,
          "expanded canvas failed metadata roundtrip");
    auto patch = std::make_unique<Bitmap>(40, 30, PixelFormat32bppARGB);
    {
        Graphics g(patch.get());
        g.Clear(Color(0xff9333ea));
    }
    patch->SetPixel(0, 0, Color(0, 0, 0, 0));
    auto bytes = encodeImageBytes(*patch);
    auto decoded = decodeImageBytes(*bytes);
    check(color(*decoded, 20, 15) == 0xff9333ea && (color(*decoded, 0, 0) >> 24) == 0,
          "PNG image stream lost owned pixels or transparency");
    auto formats = makeClipboardImage(*patch);
    auto dib = decodeClipboardDib(formats.dib), v5 = decodeClipboardDib(formats.dibv5);
    check(color(*dib, 20, 15) == 0xff9333ea && color(*v5, 20, 15) == 0xff9333ea &&
              color(*dib, 0, 0) == 0xffffffff,
          "clipboard DIB or V5 decode pixels incorrect");
    auto truncated = formats.dib;
    truncated.resize(42);
    bool rejected = false;
    try {
        decodeClipboardDib(truncated);
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "truncated clipboard DIB was accepted");
    clipboardReader = [&]() { return decodeImageBytes(*bytes); };
    command(ID_PASTE_IMAGE);
    check(doc()->cursor == count + 1 && doc()->commands.back().tool == Tool::PasteImage &&
              selectedObject == (int)doc()->cursor - 1 && tool == Tool::Select,
          "paste did not add a selected editable image");
    auto pasted = doc()->commands.back().rect;
    check(color(*rendered, pasted.left + 20, pasted.top + 15) == 0xff9333ea &&
              color(*rendered, pasted.left, pasted.top) == 0xff2563eb,
          "pasted transparency or existing background was lost");
    auto from = point(pasted.left + 20, pasted.top + 15);
    canvasDown(from);
    auto preview = objectPreview.get(), background = objectBackdrop.get();
    canvasMove({from.x + 70, from.y + 50});
    canvasMove({from.x + 70, from.y + 50});
    check(preview && background && objectPreview.get() == preview && objectBackdrop.get() == background,
          "paste movement preview redecoded or reallocated image per mouse move");
    canvasUp({from.x + 70, from.y + 50});
    pasted = doc()->commands.back().rect;
    check(pasted.left == 86 && pasted.top == 66 && color(*rendered, 106, 81) == 0xff9333ea &&
              !objectPreview && !objectBackdrop,
          "pasted image movement or buffer release failed");
    canvasDown(point(pasted.right - 1, pasted.bottom - 1));
    canvasUp(point(pasted.right + 20, pasted.bottom + 10));
    check(rw(doc()->commands.back().rect) == 60 && rh(doc()->commands.back().rect) == 40,
          "pasted image could not be resized");
    auto current = doc()->cursor;
    auto target = store.root / L"expanded-pasted.png";
    savePathPicker = [&](const std::wstring &) { return target.wstring(); };
    command(ID_SAVE_CURRENT);
    savePathPicker = {};
    auto saved = loadImage(target);
    check(saved->GetWidth() == 240 && saved->GetHeight() == 110, "saving clipped the expanded canvas");
    Store pastedStore(store.root);
    auto detail = pastedStore.read(*doc());
    auto source = loadImage(pastedStore.png(detail));
    auto restored = output(*source, detail);
    check(detail.cursor == current && detail.commands.back().imageData &&
              color(*restored, 110, 80) == color(*rendered, 110, 80),
          "embedded pasted image did not survive reload");
    check(detail.commands[count].imageData == detail.commands.back().imageData &&
              fs::file_size(pastedStore.meta(detail)) < bytes->size() + 10000,
          "image moves duplicated embedded PNG bytes instead of reusing a reference");
    command(ID_UNDO);
    command(ID_UNDO);
    command(ID_UNDO);
    check(doc()->cursor == count && color(*rendered, 36, 31) != 0xff9333ea,
          "undo paste failed to restore earlier canvas");
    clipboardReader = []() {
        auto b = std::make_unique<Bitmap>(300, 200, PixelFormat32bppARGB);
        Graphics g(b.get());
        g.Clear(Color(0xff06b6d4));
        return b;
    };
    command(ID_PASTE_IMAGE);
    check(rw(viewRect(*doc())) == 300 && rh(viewRect(*doc())) == 200 &&
              color(*rendered, 299, 199) == 0xff06b6d4,
          "large pasted image was cropped instead of expanding canvas");
    command(ID_UNDO);
    check(rw(viewRect(*doc())) == 240 && rh(viewRect(*doc())) == 110,
          "undo large paste retained auto expansion");
    Command cropView;
    cropView.tool = Tool::Crop;
    cropView.rect = {20, 10, 80, 60};
    edit(cropView);
    b = imageBounds();
    canvasDown({b.right, b.bottom});
    canvasUp({b.right + 140, b.bottom + 60});
    auto cropped = viewRect(*doc());
    check(cropped.left == 20 && cropped.top == 10 && cropped.right == 220 && cropped.bottom == 120,
          "cropped view extension lost its origin");
    check(color(*rendered, 100, 40) == 0xffffffff && color(*rendered, 40, 40) == 0xfff7ab2f,
          "extending a cropped view revealed hidden image pixels instead of white space");
    command(ID_UNDO);
    command(ID_UNDO);
    clipboardReader = []() { return Image{}; };
    current = doc()->cursor;
    command(ID_PASTE_IMAGE);
    check(doc()->cursor == current, "empty clipboard changed the document");
    clipboardReader = [&]() { return decodeImageBytes(*bytes); };
    command(ID_PASTE_IMAGE);
    clipboardReader = {};
    setTool(Tool::Pen);
    selectedObject = -1;
    zoom = 2;
    panX = panY = 0;
    canvasEdgeHover = 10;
    statusText(L"가장자리 위에 마우스: 캔버스 확장 · Ctrl+V: 이미지 붙여넣기 · Ctrl+Z: 되돌리기");
    canvasPreview(main, store.root / L"canvas-extend-paste-preview.png");
    RECT normal{};
    GetWindowRect(main, &normal);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    zoom = 1;
    panX = panY = 0;
    canvasPreview(main, store.root / L"canvas-extend-paste-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(normal), rh(normal), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    while (!store.docs.empty())
        removeHistory(0);
    clipboardReader = [&]() { return decodeImageBytes(*bytes); };
    command(ID_PASTE_IMAGE);
    clipboardReader = {};
    check(doc() && doc()->name.find(L"붙여넣기 ") == 0 && doc()->width == 40 && doc()->height == 30 &&
              color(*rendered, 20, 15) == 0xff9333ea,
          "image paste into empty history failed");
    removeHistory(0);
    std::ofstream report(store.root / L"canvas-edit-verification.json");
    report << "{\n  \"checks_passed\": " << checks
           << ",\n  \"hover_canvas_growth_white_pixels_undo_reload\": true,\n"
              "  \"paste_png_dib_v5_transparency_and_invalid_data\": true,\n"
              "  \"pasted_image_move_resize_undo_save_reload\": true,\n"
              "  \"large_paste_expands_canvas_atomically\": true,\n"
              "  \"preview_buffers_reused_and_released\": true,\n"
              "  \"computer_use\": false,\n  \"system_clipboard_read\": false,\n  "
              "\"system_clipboard_modified\": false\n}\n";
}
} // namespace lc
