#include "app.h"
namespace lc {
static Color colorAt(Bitmap &image, int x, int y) {
    Color c;
    require(image.GetPixel(x, y, &c) == Gdiplus::Ok, "selection test pixel unavailable");
    return c;
}
static bool equalPixels(Bitmap &a, Bitmap &b) {
    if (a.GetWidth() != b.GetWidth() || a.GetHeight() != b.GetHeight())
        return false;
    for (UINT y = 0; y < a.GetHeight(); ++y)
        for (UINT x = 0; x < a.GetWidth(); ++x)
            if (colorAt(a, x, y).GetValue() != colorAt(b, x, y).GetValue())
                return false;
    return true;
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
    auto resizeImage = std::make_unique<Bitmap>(260, 180, PixelFormat32bppARGB);
    {
        Graphics g(resizeImage.get());
        g.Clear(Color(0xff16a34a));
        Gdiplus::SolidBrush orange(Color(0xffffa000));
        g.FillRectangle(&orange, 20, 20, 80, 60);
    }
    add(std::move(resizeImage));
    annotation.rect = {36, 32, 76, 66};
    edit(annotation);
    count = doc()->cursor;
    zoom = 1;
    auto resizeOriginal = clone(*rendered);
    auto choose = [&]() {
        clearPixelSelection();
        down(20, 20);
        up(100, 80);
    };
    struct ResizeCase {
        POINT from, to;
        RECT expected;
        LPCWSTR cursor;
    };
    ResizeCase cases[] = {
        {{20, 50}, {40, 50}, {40, 20, 100, 80}, IDC_SIZEWE},
        {{100, 50}, {60, 50}, {20, 20, 60, 80}, IDC_SIZEWE},
        {{60, 20}, {60, 40}, {20, 40, 100, 80}, IDC_SIZENS},
        {{60, 80}, {60, 50}, {20, 20, 100, 50}, IDC_SIZENS},
        {{20, 20}, {40, 35}, {40, 35, 100, 80}, IDC_SIZENWSE},
        {{100, 20}, {60, 35}, {20, 35, 60, 80}, IDC_SIZENESW},
        {{20, 80}, {40, 50}, {40, 20, 100, 50}, IDC_SIZENESW},
        {{100, 80}, {60, 50}, {20, 20, 60, 50}, IDC_SIZENWSE},
    };
    for (auto &test : cases) {
        choose();
        canvasMouse = point(test.from.x, test.from.y);
        check(canvasCursor(96) == LoadCursor(nullptr, test.cursor), "resize border cursor mismatch");
        down(test.from.x, test.from.y);
        auto buffer = pixelPatch.get();
        move(test.to.x, test.to.y);
        move(test.to.x, test.to.y);
        check(pixelMoving && pixelResize && pixelPatch.get() == buffer && doc()->cursor == count,
              "resize preview reallocated its source or changed history");
        up(test.to.x, test.to.y);
        check(EqualRect(&pixelRect, &test.expected) && doc()->cursor == count + 1 &&
                  doc()->commands.back().points.size() == 2 && !pixelPatch && !pixelBackdrop,
              "border resize did not commit one bounded transform");
        auto destination = pixelRect;
        int centerX = (destination.left + destination.right) / 2;
        int centerY = (destination.top + destination.bottom) / 2;
        check(colorAt(*rendered, centerX, centerY).GetB() > 150 &&
                  colorAt(*rendered, 200, 120).GetValue() == colorAt(*resizeOriginal, 200, 120).GetValue(),
              "resize lost scaled annotations or changed outside pixels");
        command(ID_UNDO);
        check(doc()->cursor == count && !pixelReady &&
                  colorAt(*rendered, 40, 40).GetValue() == colorAt(*resizeOriginal, 40, 40).GetValue(),
              "resize undo did not restore the original image");
    }
    choose();
    down(100, 80);
    up(60, 50);
    check(colorAt(*rendered, 90, 70).GetValue() == Color::White,
          "shrinking failed to clear the removed source area");
    Store resizeStore(store.root);
    auto resizedDocument = resizeStore.read(*doc());
    auto resizedFile = loadImage(resizeStore.png(resizedDocument));
    auto resizedOutput = output(*resizedFile, resizedDocument);
    check(resizedDocument.commands.back().points.size() == 2 &&
              colorAt(*resizedOutput, 40, 35).GetValue() == colorAt(*rendered, 40, 35).GetValue() &&
              colorAt(*resizedOutput, 90, 70).GetValue() == Color::White,
          "resized pixels did not survive metadata reload");
    clipboardVerifier = [&](const ClipboardImage &formats) {
        auto head = (const BITMAPINFOHEADER *)formats.dib.data();
        check(head->biWidth == 40 && head->biHeight == 30, "resized-area copy dimensions mismatch");
    };
    command(ID_COPY);
    clipboardVerifier = {};
    saveImage(*resizedOutput, store.root / L"pixel-resize-edited.png");
    check(loadImage(store.root / L"pixel-resize-edited.png")->GetWidth() == 260,
          "resizing a region changed full-image export dimensions");
    zoom = 2;
    pixelPreview(main, store.root / L"pixel-resize-preview.png");
    GetWindowRect(main, &normal);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    pixelPreview(main, store.root / L"pixel-resize-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(normal), rh(normal), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    zoom = 1;
    down(40, 35);
    up(120, 75);
    check(pixelRect.left == 100 && pixelRect.top == 60 && rw(pixelRect) == 40 && rh(pixelRect) == 30 &&
              doc()->commands.back().points.size() == 1,
          "resized selection could not subsequently move from its interior");
    command(ID_UNDO);
    command(ID_UNDO);
    choose();
    down(100, 80);
    move(140, 110);
    canvasProc(canvas, WM_KEYDOWN, VK_ESCAPE, 0);
    check(doc()->cursor == count && !pixelMoving && !pixelResize && !pixelPatch && !pixelBackdrop &&
              colorAt(*rendered, 40, 40).GetValue() == colorAt(*resizeOriginal, 40, 40).GetValue(),
          "Esc during resize changed pixels or retained buffers");
    choose();
    down(100, 80);
    up(140, 110);
    check(rw(pixelRect) == 120 && rh(pixelRect) == 90 && doc()->cursor == count + 1,
          "selection could not be enlarged");
    command(ID_UNDO);
    choose();
    down(100, 80);
    up(0, 0);
    check(rw(pixelRect) == 1 && rh(pixelRect) == 1, "crossed resize allowed inverted or empty bounds");
    command(ID_UNDO);
    choose();
    down(20, 20);
    up(-100, -100);
    check(pixelRect.left == 0 && pixelRect.top == 0, "resize escaped image boundary");
    command(ID_UNDO);
    choose();
    down(100, 80);
    up(100, 80);
    check(doc()->cursor == count && pixelReady, "zero-distance border click created history");
    zoom = .5f;
    canvasMouse = point(100, 50);
    check(canvasCursor(96) == LoadCursor(nullptr, IDC_SIZEWE) && pixelHit({106, 50}) == 2,
          "zoomed border hit target did not retain screen-space tolerance");
    zoom = 1;
    edit(cropView);
    choose();
    down(20, 20);
    up(-100, -100);
    check(pixelRect.left == 20 && pixelRect.top == 10, "resize escaped the current cropped view");
    select(selected);
    check(!pixelReady && !pixelResize && !pixelPatch && !pixelBackdrop,
          "reload retained resize state or buffers");
    // Delete must edit pixels, never remove a checked capture from history.
    auto deleteImage = std::make_unique<Bitmap>(260, 180, PixelFormat32bppARGB);
    {
        Graphics g(deleteImage.get());
        g.Clear(Color(0xff16a34a));
    }
    add(std::move(deleteImage));
    edit(annotation);
    zoom = 1;
    auto deleteBefore = clone(*rendered);
    count = doc()->cursor;
    auto documentId = doc()->id;
    auto historyCount = store.docs.size();
    choose();
    canvasProc(canvas, WM_KEYDOWN, VK_DELETE, 0);
    check(doc()->id == documentId && store.docs.size() == historyCount && doc()->cursor == count + 1 &&
              !pixelReady && !pixelPatch && !pixelBackdrop && selectedObject == -1,
          "Delete removed a capture or failed to commit one pixel edit and clear selection");
    bool deletePixels = true;
    for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 260; ++x)
            deletePixels &=
                colorAt(*rendered, x, y).GetValue() == (x >= 20 && x < 100 && y >= 20 && y < 80
                                                            ? Color::White
                                                            : colorAt(*deleteBefore, x, y).GetValue());
    check(deletePixels, "Delete failed to clear selected photo and annotations or changed outside pixels");
    canvasProc(canvas, WM_KEYDOWN, VK_DELETE, 0);
    check(doc()->cursor == count + 1 && store.docs.size() == historyCount,
          "Delete with no pixel selection changed history");
    auto deletedImage = clone(*rendered);
    command(ID_UNDO);
    check(doc()->cursor == count && equalPixels(*rendered, *deleteBefore),
          "Delete undo did not restore every original pixel");
    undo(true);
    check(equalPixels(*rendered, *deletedImage), "Delete redo did not restore cleared pixels");
    Store deletedStore(store.root);
    auto deletedDocument = deletedStore.read(*doc());
    auto deletedOriginal = loadImage(deletedStore.png(deletedDocument));
    auto deletedOutput = output(*deletedOriginal, deletedDocument);
    check(equalPixels(*deletedOutput, *deletedImage) && deletedDocument.commands.back().points.empty(),
          "Delete did not roundtrip through metadata and image output");
    select(selected);
    check(equalPixels(*rendered, *deletedImage), "Delete was lost after document reload");
    clipboardVerifier = [&](const ClipboardImage &formats) {
        auto copied = decodeImageBytes(formats.png);
        check(copied && equalPixels(*copied, *deletedImage), "Delete did not reach copied image pixels");
    };
    command(ID_COPY);
    clipboardVerifier = {};
    command(ID_UNDO);
    zoom = 1;
    choose();
    down(100, 80);
    up(140, 110);
    down(60, 50);
    up(110, 80);
    auto movedBeforeDelete = clone(*rendered);
    auto deletedArea = pixelRect;
    count = doc()->cursor;
    canvasProc(canvas, WM_KEYDOWN, VK_DELETE, 0);
    check(doc()->cursor == count + 1 &&
              colorAt(*rendered, deletedArea.left + 10, deletedArea.top + 10).GetValue() == Color::White,
          "Delete failed after selection resize and movement");
    command(ID_UNDO);
    check(equalPixels(*rendered, *movedBeforeDelete),
          "Delete undo after resize and movement changed earlier edits");
    edit(cropView);
    zoom = 1;
    down(20, 10);
    up(60, 50);
    auto croppedBeforeDelete = clone(*rendered);
    canvasProc(canvas, WM_KEYDOWN, VK_DELETE, 0);
    result = output(*original, *doc());
    check(result->GetWidth() == 190 && result->GetHeight() == 140 &&
              colorAt(*result, 0, 0).GetValue() == Color::White &&
              colorAt(*rendered, 10, 10).GetValue() == colorAt(*croppedBeforeDelete, 10, 10).GetValue(),
          "Delete ignored crop coordinates or changed canvas dimensions");
    command(ID_UNDO);
    check(equalPixels(*rendered, *croppedBeforeDelete), "cropped Delete undo changed source pixels");
    while (!store.docs.empty())
        removeHistory(0);
    std::ofstream report(store.root / L"pixel-selection-verification.json");
    report << "{\n  \"checks_passed\": " << checks
           << ",\n  \"rectangle_pixel_move\": true,\n  \"photo_and_annotations_moved\": true,\n"
              "  \"overlap_safe\": true,\n  \"undo_and_metadata_roundtrip\": true,\n"
              "  \"selected_region_copy\": true,\n  \"crop_boundary_clamp\": true,\n"
              "  \"preview_buffers_reused_and_released\": true,\n"
              "  \"eight_border_and_corner_resize_targets\": true,\n"
              "  \"resize_cursor_and_zoom_hit_targets\": true,\n"
              "  \"resize_pixels_undo_reload_copy_export\": true,\n"
              "  \"resize_minimum_bounds_crop_and_cancel\": true,\n"
              "  \"delete_selected_pixels_undo_redo_reload_copy\": true,\n"
              "  \"delete_after_resize_move_and_crop\": true,\n"
              "  \"delete_preserves_capture_history_and_outside_pixels\": true,\n"
              "  \"computer_use\": false,\n  \"system_clipboard_modified\": false\n}\n";
}
} // namespace lc
