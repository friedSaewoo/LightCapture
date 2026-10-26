#include "app.h"
namespace lc {
static Color colorAt(Bitmap &image, int x, int y) {
    Color c;
    require(image.GetPixel(x, y, &c) == Gdiplus::Ok, "selection test pixel unavailable");
    return c;
}
static void pixelPreview(HWND window, const fs::path &path) {
    RECT r{}, outer{};
    GetClientRect(window, &r);
    GetWindowRect(window, &outer);
    POINT client{};
    ClientToScreen(window, &client);
    HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rw(r), rh(r));
    auto old = SelectObject(mem, bitmap);
    FillRect(mem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetViewportOrgEx(mem, outer.left - client.x, outer.top - client.y, nullptr);
    SendMessageW(window, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    SelectObject(mem, old);
    auto image = Image(Bitmap::FromHBITMAP(bitmap, nullptr));
    saveImage(*image, path);
    image.reset();
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(window, dc);
}
void App::verifyPixelSelection() {
    int checks = 0;
    auto check = [&](bool ok, const char *name) {
        require(ok, name);
        ++checks;
    };
    auto image = std::make_unique<Bitmap>(260, 180, PixelFormat32bppARGB);
    {
        Graphics g(image.get());
        g.Clear(Color(0xffe5eefb));
        Gdiplus::SolidBrush red(Color(0xffe5484d)), green(Color(0xff16a34a));
        g.FillRectangle(&red, 20, 20, 50, 40);
        g.FillRectangle(&green, 180, 30, 45, 100);
    }
    add(std::move(image));
    Command annotation;
    annotation.tool = Tool::Rectangle;
    annotation.rect = {30, 30, 40, 40};
    annotation.color = 0xff2563eb;
    annotation.fill = true;
    edit(annotation);
    auto before = clone(*rendered);
    command(ID_TOOL + (UINT)Tool::PixelSelect);
    check(tool == Tool::PixelSelect && GetDlgItem(main, ID_TOOL + (UINT)Tool::PixelSelect),
          "pixel selection toolbar button missing");
    zoom = 1;
    panX = panY = 0;
    auto point = [&](int x, int y) {
        auto b = imageBounds(), v = viewRect(*doc());
        return POINT{b.left + (LONG)std::lround((x - v.left) * zoom),
                     b.top + (LONG)std::lround((y - v.top) * zoom)};
    };
    auto down = [&](int x, int y) { canvasDown(point(x, y)); };
    auto up = [&](int x, int y) { canvasUp(point(x, y)); };
    auto move = [&](int x, int y) { canvasMove(point(x, y)); };
    auto count = doc()->cursor;
    down(70, 60);
    up(20, 20);
    check(pixelReady && EqualRect(&pixelRect, &annotation.rect) == FALSE && pixelRect.left == 20 &&
              pixelRect.top == 20 && rw(pixelRect) == 50 && rh(pixelRect) == 40 && doc()->cursor == count,
          "reverse rectangular selection altered image history");
    down(30, 30);
    move(130, 100);
    auto backdrop = pixelBackdrop.get(), patch = pixelPatch.get();
    move(129, 99);
    move(130, 100);
    check(pixelMoving && backdrop && patch && pixelBackdrop.get() == backdrop && pixelPatch.get() == patch &&
              doc()->cursor == count && colorAt(*pixelBackdrop, 25, 25).GetValue() == Color::White,
          "pixel move preview mutated history or reallocated buffers per mouse move");
    up(130, 100);
    check(pixelReady && !pixelMoving && !pixelPatch && !pixelBackdrop && pixelRect.left == 120 &&
              pixelRect.top == 90 && doc()->cursor == count + 1,
          "pixel move did not commit one history operation");
    bool moved = true;
    for (int y = 20; y < 60; ++y)
        for (int x = 20; x < 70; ++x)
            moved &= colorAt(*rendered, x, y).GetValue() == Color::White &&
                     colorAt(*rendered, x + 100, y + 70).GetValue() == colorAt(*before, x, y).GetValue();
    check(moved && colorAt(*rendered, 190, 40).GetValue() == colorAt(*before, 190, 40).GetValue(),
          "move lost photo/annotation pixels, failed to clear source, or changed outside pixels");
    int copies = 0;
    clipboardVerifier = [&](const ClipboardImage &formats) {
        auto head = (const BITMAPINFOHEADER *)formats.dib.data();
        check(head->biWidth == 50 && head->biHeight == 40, "selection copy exported whole canvas");
        ++copies;
    };
    command(ID_COPY);
    check(copies == 1 && pixelReady, "selected-area copy cleared selection or was omitted");
    clipboardVerifier = {};
    auto id = doc()->id;
    Store readback(store.root);
    auto stored = readback.read(readback.docs.front());
    auto originalFile = loadImage(readback.png(stored));
    auto restored = output(*originalFile, stored);
    check(stored.id == id && stored.commands.back().tool == Tool::PixelSelect &&
              colorAt(*restored, 130, 100).GetValue() == colorAt(*rendered, 130, 100).GetValue() &&
              colorAt(*restored, 25, 25).GetValue() == Color::White,
          "pixel move did not roundtrip through existing metadata format");
    command(ID_UNDO);
    check(!pixelReady && doc()->cursor == count &&
              colorAt(*rendered, 35, 35).GetValue() == colorAt(*before, 35, 35).GetValue() &&
              colorAt(*rendered, 130, 100).GetValue() == colorAt(*before, 130, 100).GetValue(),
          "Ctrl+Z failed to restore source and overwritten destination");
    undo(true);
    auto overlapBefore = clone(*rendered);
    down(120, 90);
    up(170, 130);
    down(125, 95);
    up(135, 105);
    bool overlapOk = true;
    for (int y = 90; y < 130; ++y)
        for (int x = 120; x < 170; ++x)
            overlapOk &=
                colorAt(*rendered, x + 10, y + 10).GetValue() == colorAt(*overlapBefore, x, y).GetValue();
    check(overlapOk && colorAt(*rendered, 120, 90).GetValue() == Color::White,
          "overlapping move erased its own source pixels");
    count = doc()->cursor;
    down(135, 105);
    move(50, 50);
    canvasProc(canvas, WM_KEYDOWN, VK_ESCAPE, 0);
    check(!pixelReady && !pixelMoving && !pixelBackdrop && !pixelPatch && doc()->cursor == count &&
              colorAt(*rendered, 140, 110).GetValue() == colorAt(*overlapBefore, 130, 100).GetValue(),
          "Esc during pixel drag changed committed pixels");
    down(130, 100);
    up(180, 140);
    auto bounds = imageBounds();
    canvasDown({bounds.right + 10, bounds.bottom + 10});
    check(!pixelReady && doc()->cursor == count, "outside click altered image or retained selection");
    down(10, 10);
    up(10, 10);
    check(!pixelReady && doc()->cursor == count, "zero-area selection created an edit");
    down(130, 100);
    up(180, 140);
    down(135, 105);
    up(135, 105);
    check(pixelReady && doc()->cursor == count, "zero-distance move created an edit");
    auto frame = store.root / L"pixel-selection-edited.png";
    auto result = output(*original, *doc());
    saveImage(*result, frame);
    check(loadImage(frame)->GetWidth() == 260, "pixel movement changed full-image save dimensions");
    pixelPreview(main, store.root / L"pixel-selection-preview.png");
    RECT normal{};
    GetWindowRect(main, &normal);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    pixelPreview(main, store.root / L"pixel-selection-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(normal), rh(normal), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    command(ID_TOOL + (UINT)Tool::Pen);
    check(!pixelReady && !pixelPatch && !pixelBackdrop, "switching tools retained selection buffers");
    command(ID_TOOL + (UINT)Tool::Select);
    down(35, 35);
    check(!dragging && selectedObject == -1,
          "old moved annotations remained selectable at their former location");
    auto rasterCount = doc()->cursor;
    eraseAt({35, 35});
    check(doc()->cursor == rasterCount, "eraser modified annotations already merged into raster movement");
    Command cropView;
    cropView.tool = Tool::Crop;
    cropView.rect = {20, 10, 210, 150};
    edit(cropView);
    command(ID_TOOL + (UINT)Tool::PixelSelect);
    down(30, 30);
    up(60, 50);
    down(35, 35);
    up(-100, -100);
    check(pixelRect.left == 20 && pixelRect.top == 10 && rw(pixelRect) == 30 && rh(pixelRect) == 20,
          "pixel move escaped a cropped view boundary");
    result = output(*original, *doc());
    check(result->GetWidth() == 190 && result->GetHeight() == 140,
          "pixel movement discarded crop dimensions");
    select(selected);
    check(!pixelReady && !pixelPatch && !pixelBackdrop, "document reload retained temporary selection");
    while (!store.docs.empty())
        removeHistory(0);
    std::ofstream report(store.root / L"pixel-selection-verification.json");
    report << "{\n  \"checks_passed\": " << checks
           << ",\n  \"rectangle_pixel_move\": true,\n  \"photo_and_annotations_moved\": true,\n"
              "  \"overlap_safe\": true,\n  \"undo_and_metadata_roundtrip\": true,\n"
              "  \"selected_region_copy\": true,\n  \"crop_boundary_clamp\": true,\n"
              "  \"preview_buffers_reused_and_released\": true,\n"
              "  \"computer_use\": false,\n  \"system_clipboard_modified\": false\n}\n";
}
} // namespace lc
