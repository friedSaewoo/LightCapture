#include "app.h"
#include "toolbar.h"
namespace lc {
static std::vector<uint32_t> inputFrame(HWND h, const std::vector<uint32_t> &previous = {}) {
    RECT r{};
    GetClientRect(h, &r);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rw(r);
    info.bmiHeader.biHeight = -rh(r);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    uint32_t *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
    require(bitmap && pixels, "input repaint test allocation failed");
    auto old = SelectObject(dc, bitmap);
    size_t count = (size_t)rw(r) * rh(r);
    for (size_t i = 0; i < count; ++i)
        pixels[i] = previous.size() == count ? previous[i] : 0x00ff00ff;
    SendMessageW(h, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    GdiFlush();
    std::vector<uint32_t> result(pixels, pixels + count);
    for (auto &pixel : result)
        pixel &= 0x00ffffff;
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}
static void preview(HWND window, const fs::path &path) {
    RECT r{};
    GetClientRect(window, &r);
    HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rw(r), rh(r));
    auto old = SelectObject(mem, bitmap);
    FillRect(mem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    RECT outer{};
    GetWindowRect(window, &outer);
    POINT client{};
    ClientToScreen(window, &client);
    SetViewportOrgEx(mem, outer.left - client.x, outer.top - client.y, nullptr);
    SendMessageW(window, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    auto image = Image(Bitmap::FromHBITMAP(bitmap, nullptr));
    saveImage(*image, path);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(window, dc);
}
void App::verifyEditor() {
    require(aboutText().find(L"알캡") == std::wstring::npos, "about text still contains removed comparison");
    Bitmap displayFixture(8, 8, PixelFormat32bppARGB);
    {
        Graphics g(&displayFixture);
        g.Clear(Color(0xffe5484d));
    }
    auto cached = scaledDisplay(displayFixture, {0, 0, 8, 8}, 16, 16);
    Color displayed{};
    cached->GetPixel(8, 8, &displayed);
    require(displayed.GetValue() == 0xffe5484d &&
                scaledDisplay(displayFixture, {0, 0, 8, 8}, 16, 16) == cached,
            "scaled display lost pixels or failed to reuse a steady frame");
    clearDisplayCache();
    {
        Graphics g(&displayFixture);
        g.Clear(Color(0xff1761d7));
    }
    cached = scaledDisplay(displayFixture, {0, 0, 8, 8}, 16, 16);
    cached->GetPixel(8, 8, &displayed);
    require(displayed.GetValue() == 0xff1761d7, "display cache retained stale edited pixels");
    require(!scaledDisplay(displayFixture, {0, 0, 8, 8}, 8192, 8192),
            "display cache allocated an unbounded zoom image");
    clearDisplayCache();
    require(!GetDlgItem(main, ID_COPY) && !GetDlgItem(main, ID_UNDO) && !GetDlgItem(main, ID_REDO) &&
                !GetDlgItem(main, 180),
            "old action buttons or tool dropdown remain");
    for (int i = 0; i < 11; ++i) {
        wchar_t type[32]{};
        auto button = GetDlgItem(main, ID_TOOL + i);
        GetClassNameW(button, type, 32);
        require(wcscmp(type, L"Button") == 0, "tool icon is not a keyboard-accessible native button");
        command(ID_TOOL + i);
        require((int)tool == i, "tool icon command did not select the tool");
    }
    for (int i = 0; i < PaletteCount; ++i) {
        command(ID_PALETTE + i);
        require(color == Palette[i].value, "palette selection mismatch");
    }
    setColor(Palette[3].value);
    setTool(Tool::Text);
    setZoom(1);
    auto b = imageBounds();
    size_t before = doc()->cursor;
    canvasDown({b.left + 270, b.top + 244});
    canvasMove({b.left + 600, b.top + 345});
    canvasUp({b.left + 600, b.top + 345});
    require(textInput && GetParent(textInput) == canvas && textBrush && doc()->cursor == before,
            "drag did not create an uncommitted inline editor");
    auto input = textInput;
    auto initialRect = textDraft.rect;
    auto border = textScreenRect();
    POINT grip{border.left + rw(border) / 3, border.top - 3};
    canvasMouse = grip;
    require(textHit(grip) == 16 && canvasCursor(96) == LoadCursor(nullptr, IDC_SIZEALL),
            "text frame did not expose a move cursor");
    canvasDown(grip);
    canvasMove({grip.x + 40, grip.y + 24});
    canvasUp({grip.x + 40, grip.y + 24});
    require(textInput == input && !textMoving && textDraft.rect.left == initialRect.left + 40 &&
                textDraft.rect.top == initialRect.top + 24 && rw(textDraft.rect) == rw(initialRect) &&
                doc()->cursor == before,
            "moving an empty input box committed text or changed its size");
    SetWindowTextW(textInput, L"잔상을 확인할 긴 글자입니다.\r\n두 번째 줄도 지워져야 합니다.");
    auto oldFrame = inputFrame(textInput);
    SendMessageW(textInput, EM_SETSEL, 0, -1);
    SendMessageW(textInput, EM_REPLACESEL, FALSE, (LPARAM)L"짧은 글자");
    auto replacedFrame = inputFrame(textInput, oldFrame);
    require(replacedFrame == inputFrame(textInput) && replacedFrame != oldFrame,
            "replacing text retained previous glyph pixels");
    SendMessageW(textInput, EM_SETSEL, 0, -1);
    SendMessageW(textInput, EM_REPLACESEL, FALSE, (LPARAM)L"");
    require(inputFrame(textInput, replacedFrame) == inputFrame(textInput),
            "deleting text failed to restore the image background");
    SetWindowTextW(textInput, L"이미지를 보며 바로 입력합니다.\r\n여러 줄도 영역 안에서 작성합니다.");
    auto sizedRect = textDraft.rect;
    auto inputFontBefore = inputFont;
    auto fontSizeBefore = textDraft.fontSize;
    SendMessageW(textInput, EM_SETSEL, 3, 7);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            if (row == 1 && col == 1)
                continue;
            textDraft.rect = sizedRect;
            positionText();
            auto frame = textFrameRect();
            POINT handle{frame.left + rw(frame) * col / 2, frame.top + rh(frame) * row / 2};
            int hit = (col == 0 ? 1 : col == 2 ? 2 : 0) | (row == 0 ? 4 : row == 2 ? 8 : 0);
            require(textHit(handle) == hit, "text resize handle hit mismatch");
            canvasDown(handle);
            canvasMove({handle.x + 12, handle.y + 10});
            canvasUp({handle.x + 12, handle.y + 10});
            auto expected = sizedRect;
            if (hit & 1)
                expected.left += 12;
            if (hit & 2)
                expected.right += 12;
            if (hit & 4)
                expected.top += 10;
            if (hit & 8)
                expected.bottom += 10;
            DWORD selectionStart = 0, selectionEnd = 0;
            SendMessageW(textInput, EM_GETSEL, (WPARAM)&selectionStart, (LPARAM)&selectionEnd);
            require(EqualRect(&textDraft.rect, &expected) && textInput == input &&
                        textDraft.fontSize == fontSizeBefore && inputFont == inputFontBefore &&
                        selectionStart == 3 && selectionEnd == 7 && doc()->cursor == before &&
                        GetWindowTextLengthW(textInput) > 20,
                    "resizing the text box changed glyph size, text selection, input handle or history");
        }
    textDraft.rect = sizedRect;
    positionText();
    auto frame = textFrameRect();
    canvasDown({frame.right, frame.bottom});
    canvasUp({frame.left - 100, frame.top - 100});
    require(rw(textDraft.rect) >= 24 && rh(textDraft.rect) >= 24 && textInput == input,
            "text resize inverted or removed the input box");
    textDraft.rect = sizedRect;
    positionText();
    frame = textFrameRect();
    canvasDown({frame.right, frame.bottom});
    canvasUp({b.right + 100, b.bottom + 100});
    auto view = viewRect(*doc());
    require(textDraft.rect.right == view.right && textDraft.rect.bottom == view.bottom,
            "text resize escaped visible image bounds");
    textDraft.rect = sizedRect;
    positionText();
    border = textScreenRect();
    grip = {border.left + rw(border) / 3, border.top - 3};
    canvasDown(grip);
    canvasMove({grip.x + 100, grip.y + 80});
    SendMessageW(textInput, WM_KEYDOWN, VK_ESCAPE, 0);
    require(EqualRect(&textDraft.rect, &sizedRect) && textInput == input && !textMoving && !textResize &&
                doc()->cursor == before,
            "Esc during input movement failed to restore draft position and keep text editable");
    command(ID_PALETTE + 8);
    require(textInput && textDraft.color == Palette[8].value,
            "palette click committed or failed to recolor inline text");
    preview(main, store.root / L"inline-text-preview.png");
    RECT inlineWindow{};
    GetWindowRect(main, &inlineWindow);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    RECT inputClient{};
    GetClientRect(canvas, &inputClient);
    auto inputBounds = textFrameRect();
    require(inputBounds.left >= 0 && inputBounds.top >= 0 && inputBounds.right <= inputClient.right &&
                inputBounds.bottom <= inputClient.bottom,
            "resizing the main window hid the active text box");
    preview(main, store.root / L"inline-text-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(inlineWindow), rh(inlineWindow),
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    finishText(false);
    require(!textInput && !textBackdrop && doc()->cursor == before, "cancel changed document history");
    Command text;
    text.tool = Tool::Text;
    text.rect = {280, 244, 600, 336};
    text.font = fontName;
    text.fontSize = 22;
    text.color = Palette[8].value;
    startText(text);
    SetWindowTextW(
        textInput,
        L"영역 안에서 줄바꿈되는 긴 문장을 입력하여 배경과 함께 저장합니다.\r\n한글 입력과 Enter 줄바꿈");
    finishText();
    require(doc()->cursor == before + 1 && doc()->commands.back().fill &&
                doc()->commands.back().tool == Tool::Text,
            "inline text was not stored as a bounded object");
    auto savedText = doc()->commands.back().text;
    Store reloaded(store.root);
    auto persisted = reloaded.read(*doc());
    require(persisted.commands.back().text == savedText && persisted.commands.back().fill,
            "boxed text was not preserved by metadata roundtrip");
    selectedObject = (int)doc()->cursor - 1;
    editText();
    require(textInput && textDraft.target == (int)before, "existing text did not reopen inline");
    auto persistedRect = doc()->commands[before].rect;
    border = textScreenRect();
    grip = {border.left + rw(border) / 3, border.top - 3};
    canvasDown(grip);
    canvasUp({grip.x + 20, grip.y + 15});
    SetWindowTextW(textInput, L"취소할 수정");
    finishText(false);
    require(doc()->cursor == before + 1 && doc()->commands.back().text == savedText,
            "cancel damaged an existing text object");
    require(EqualRect(&doc()->commands[before].rect, &persistedRect),
            "cancelling an existing text move persisted draft geometry");
    selectedObject = (int)doc()->cursor - 1;
    editText();
    frame = textFrameRect();
    canvasDown({frame.right, frame.bottom});
    canvasUp({frame.right + 40, frame.bottom + 20});
    SetWindowTextW(textInput, L"이미지 위에서 수정한 텍스트");
    finishText();
    require(doc()->cursor == before + 2 && doc()->commands.back().target == (int)before &&
                erased(*doc())[before] && rw(doc()->commands.back().rect) == rw(persistedRect) + 40 &&
                rh(doc()->commands.back().rect) == rh(persistedRect) + 20,
            "editing existing text left both old and new versions visible");
    startText(text);
    finishText();
    require(doc()->cursor == before + 2, "empty text created an object");
    startText(text);
    SetWindowTextW(textInput, L"저장 버튼으로 입력을 확정합니다.");
    savePathPicker = [&](const std::wstring &) { return (store.root / L"inline-text-saved.png").wstring(); };
    command(ID_SAVE_CURRENT);
    savePathPicker = {};
    require(!textInput && doc()->cursor == before + 3 && fs::exists(store.root / L"inline-text-saved.png"),
            "saving while typing omitted pending text");
    auto image = output(*original, *doc());
    saveImage(*image, store.root / L"boxed-text-fixture.png");
    setTool(Tool::Pen);
    preview(main, store.root / L"toolbar-preview.png");
    RECT mainRect{};
    GetWindowRect(main, &mainRect);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    for (auto [id, h] : topControls) {
        RECT controlRect{}, client{};
        GetWindowRect(h, &controlRect);
        MapWindowPoints(nullptr, main, (POINT *)&controlRect, 2);
        GetClientRect(main, &client);
        require(controlRect.left >= 0 && controlRect.right <= client.right,
                "toolbar control clipped horizontally at minimum window width");
    }
    preview(main, store.root / L"toolbar-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(mainRect), rh(mainRect), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    virtualRect = {0, 0, (LONG)original->GetWidth(), (LONG)original->GetHeight()};
    snapshot = clone(*rendered);
    busy = true;
    mode = Mode::Rectangle;
    createQuickBar(main);
    preview(quickBar, store.root / L"quick-bar-preview.png");
    quickProc(quickBar, WM_COMMAND, ID_WINDOW, 0);
    require(mode == Mode::Window && snapshot && busy, "capture toolbar did not switch to window mode");
    quickProc(quickBar, WM_COMMAND, ID_CAPTURE, 0);
    require(mode == Mode::Rectangle && snapshot, "capture toolbar did not switch back to region mode");
    auto retainedId = doc()->id;
    quickProc(quickBar, WM_COMMAND, ID_RESTORE, 0);
    require(!quickBar && !busy && !snapshot && doc()->id == retainedId && original,
            "recent-list action lost the document or left capture active");
    size_t count = store.docs.size();
    snapshot = clone(*rendered);
    busy = true;
    createQuickBar(main);
    quickProc(quickBar, WM_COMMAND, ID_FULL, 0);
    require(!busy && !quickBar && !snapshot && store.docs.size() == count + 1 &&
                doc()->width == (UINT)rw(virtualRect) && doc()->height == (UINT)rh(virtualRect),
            "quick fullscreen action did not complete its synthetic snapshot");
    count = store.docs.size();
    retainedId = doc()->id;
    for (auto captureMode : {Mode::Rectangle, Mode::Window, Mode::Fixed}) {
        mode = captureMode;
        snapshot = clone(*rendered);
        busy = true;
        dragging = true;
        previouslyVisible = true;
        previous = nullptr;
        overlayRenderer.prepare(*snapshot);
        overlay = CreateWindowExW(WS_EX_TOOLWINDOW, L"LCOverlay", L"Hidden toggle verification",
                                  WS_POPUP | WS_CLIPCHILDREN, 0, 0, rw(virtualRect), rh(virtualRect), nullptr,
                                  nullptr, instance, this);
        require(overlay != nullptr, "hidden overlay creation failed");
        createQuickBar(overlay);
        HWND closedOverlay = overlay, closedBar = quickBar;
        SendMessageW(main, WM_HOTKEY, hotId, 0);
        require(!busy && !dragging && !overlay && !quickBar && !snapshot && !IsWindow(closedOverlay) &&
                    !IsWindow(closedBar) && !IsWindowVisible(main) && !original && !rendered &&
                    store.docs.size() == count && doc()->id == retainedId,
                "repeated capture hotkey did not close capture UI without adding an image");
        select(selected);
    }
    busy = true;
    previouslyVisible = true;
    SetTimer(main, 3, 30000, nullptr);
    SendMessageW(main, WM_HOTKEY, hotId, 0);
    require(!busy && !IsWindowVisible(main) && !original && !rendered && store.docs.size() == count,
            "hotkey cancellation restored editor during delay");
    select(selected);
    busy = true;
    scrollResult = clone(*rendered);
    scrollBar = CreateWindowExW(WS_EX_TOOLWINDOW, L"LCScroll", L"Hidden scroll cancellation test", WS_POPUP,
                                0, 0, 200, 40, nullptr, nullptr, instance, this);
    require(scrollBar != nullptr, "hidden scroll control creation failed");
    SendMessageW(main, WM_HOTKEY, hotId, 0);
    require(!busy && !scrollBar && !scrollResult && !IsWindowVisible(main) && !original && !rendered &&
                store.docs.size() == count && doc()->id == retainedId,
            "hotkey scroll cancellation restored editor or changed documents");
    select(selected);
    int oldDelay = store.settings.delay;
    store.settings.delay = 30;
    begin(Mode::Rectangle);
    require(busy && waitingForHide && !IsWindowVisible(main) && !snapshot && !overlay && !quickBar &&
                !original && !rendered,
            "capture began before hidden-window verification");
    show();
    createOverlay();
    require(!IsWindowVisible(main) && !snapshot && !overlay && waitingForHide,
            "editor restoration or early overlay bypassed capture preparation");
    captureNotBefore = 1000;
    require(!advanceCaptureStart(true, true, 999) && !advanceCaptureStart(false, true, 1000) &&
                !advanceCaptureStart(true, false, 1000),
            "capture readiness ignored delay, visibility or compositor failure");
    require(!advanceCaptureStart(true, true, 1000) && !advanceCaptureStart(false, true, 1016) &&
                !advanceCaptureStart(true, true, 1032) && advanceCaptureStart(true, true, 1048),
            "capture readiness did not require consecutive hidden composed frames");
    SendMessageW(main, WM_HOTKEY, hotId, 0);
    SendMessageW(main, WM_TIMER, 5, 0);
    require(!busy && !waitingForHide && !hiddenFrameCount && !snapshot && !overlay &&
                store.docs.size() == count && doc()->id == retainedId,
            "cancelled preparation or queued timer started a capture");
    begin(Mode::Rectangle);
    captureStartDeadline = 0;
    pollCaptureStart();
    require(!busy && !waitingForHide && !snapshot && !overlay && !IsWindowVisible(main) &&
                store.docs.size() == count,
            "hide timeout captured an unconfirmed desktop");
    store.settings.delay = oldDelay;
    select(selected);
    std::ofstream result(store.root / L"editor-verification.json");
    result << "{\n  \"icon_tools_11\": true,\n  \"palette_colors_20\": true,\n"
              "  \"unneeded_buttons_and_tool_dropdown_removed\": true,\n"
              "  \"about_comparison_removed\": true,\n"
              "  \"display_cache_pixels_reuse_invalidation_and_limit\": true,\n"
              "  \"drag_creates_inline_editor\": true,\n  \"live_palette_while_typing\": true,\n"
              "  \"new_and_existing_text_cancel\": true,\n  \"boxed_text_persistence\": true,\n"
              "  \"text_replacement_hides_old_object\": true,\n  \"empty_text_ignored\": true,\n"
              "  \"save_commits_pending_text\": true,\n  \"minimum_width_controls_fit\": true,\n"
              "  \"text_input_border_move_keeps_draft_editable\": true,\n"
              "  \"text_box_eight_handles_resize_without_scaling_font\": true,\n"
              "  \"text_repaint_replace_delete_clears_old_glyphs\": true,\n"
              "  \"text_geometry_bounds_cancel_and_persistence\": true,\n"
              "  \"capture_toolbar_mode_switches\": true,\n  \"capture_toolbar_recent_list\": true,\n"
              "  \"capture_toolbar_fullscreen\": true,\n"
              "  \"hotkey_repeat_cancels_region_window_fixed\": true,\n"
              "  \"capture_toolbar_destroyed_on_cancel\": true,\n"
              "  \"hotkey_cancel_keeps_editor_hidden_and_releases_images\": true,\n"
              "  \"delay_and_scroll_hotkey_cancel_keeps_editor_hidden\": true,\n"
              "  \"capture_start_waits_for_hidden_composed_frames\": true,\n"
              "  \"preparation_cancel_and_timeout_never_capture\": true,\n"
              "  \"editor_cannot_reopen_during_capture_preparation\": true,\n"
              "  \"desktop_capture\": false,\n  \"external_input\": false\n}\n";
}
} // namespace lc
namespace lc {
void App::verifyPaint() {
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        require(ok, message);
        ++checks;
    };
    auto pixel = [&](Bitmap &image, int x, int y) {
        Color color;
        require(image.GetPixel(x, y, &color) == Gdiplus::Ok, "paint test pixel unavailable");
        return color.GetValue();
    };
    auto blank = [&](Bitmap &image) {
        Gdiplus::BitmapData data{};
        Gdiplus::Rect r(0, 0, image.GetWidth(), image.GetHeight());
        require(image.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok,
                "blank canvas pixels unavailable");
        bool white = true;
        for (int y = 0; y < r.Height; ++y) {
            auto row = (uint32_t *)((BYTE *)data.Scan0 + (ptrdiff_t)y * data.Stride);
            for (int x = 0; x < r.Width; ++x)
                white &= row[x] == 0xffffffff;
        }
        image.UnlockBits(&data);
        return white;
    };
    auto oldStroke = stroke;
    check(!GetDlgItem(main, ID_FIT) && !GetDlgItem(main, 181) &&
              GetMenuState(GetMenu(main), ID_FIT, MF_BYCOMMAND) == (UINT)-1,
          "fit action or old width dropdown remains");
    auto button = GetDlgItem(main, ID_NEW_CANVAS);
    check(button && GetMenuState(GetMenu(main), ID_NEW_CANVAS, MF_BYCOMMAND) != (UINT)-1,
          "new paint button or menu missing");
    SendMessageW(main, WM_COMMAND, ID_NEW_CANVAS, (LPARAM)button);
    check(store.docs.size() == 1 && selected == 0 && tool == Tool::Pen && doc()->name.find(L"그림판 ") == 0 &&
              doc()->width == 1280 && doc()->height == 720 && doc()->cursor == 0 &&
              SendMessageW(list, LB_GETCURSEL, 0, 0) == 0 && checkedHistory.empty(),
          "paint button did not add and activate a fresh document");
    check(blank(*original) && blank(*rendered), "paint canvas is not opaque white");
    check(penWidthVisible() && (GetWindowLongPtrW(widthControl, GWL_STYLE) & WS_VISIBLE) &&
              (GetWindowLongPtrW(widthControl, GWL_STYLE) & TBS_VERT),
          "pen did not expose its vertical width control");
    SendMessageW(widthControl, TBM_SETPOS, TRUE, 81 - 17);
    SendMessageW(main, WM_VSCROLL, TB_THUMBTRACK, (LPARAM)widthControl);
    wchar_t label[32]{};
    GetWindowTextW(widthLabel, label, 32);
    check(stroke == 17 && wcscmp(label, L"17 px") == 0,
          "vertical width adjustment did not update stroke and displayed value");
    SendMessageW(widthControl, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    check(stroke == 18 && SendMessageW(widthControl, TBM_GETPOS, 0, 0) == 63,
          "width wheel up did not make the pen thicker");
    SendMessageW(widthControl, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    check(stroke == 17, "width wheel down did not make the pen thinner");
    setPenWidth(1000);
    check(stroke == 80, "pen width exceeded upper bound");
    setPenWidth(-10);
    check(stroke == 1, "pen width fell below lower bound");
    setPenWidth(17);
    setColor(0xff111827);
    setZoom(1);
    auto b = imageBounds();
    canvasDown({b.left + 50, b.top + 50});
    canvasMove({b.left + 100, b.top + 50});
    canvasUp({b.left + 150, b.top + 50});
    check(doc()->cursor == 1 && doc()->commands.back().tool == Tool::Pen &&
              doc()->commands.back().width == 17 && pixel(*rendered, 100, 50) == 0xff111827 &&
              pixel(*rendered, 100, 56) != 0xffffffff && pixel(*rendered, 100, 65) == 0xffffffff,
          "blank canvas pen did not use the selected stroke width");
    command(ID_UNDO);
    check(doc()->cursor == 0 && blank(*rendered), "paint undo did not restore blank canvas");
    undo(true);
    auto first = doc()->id;
    checkHistory(0, false);
    setTool(Tool::Text);
    check(!(GetWindowLongPtrW(widthControl, GWL_STYLE) & WS_VISIBLE),
          "pen width control remained visible for text tool");
    Command text;
    text.tool = Tool::Text;
    text.rect = {200, 100, 500, 200};
    startText(text);
    SetWindowTextW(textInput, L"그림판 입력 보존");
    command(ID_NEW_CANVAS);
    check(store.docs.size() == 2 && doc()->id != first && selected == 0 && tool == Tool::Pen && !textInput &&
              blank(*rendered) && checkedHistory.size() == 1 && checkedHistory.count(first),
          "another blank canvas lost the previous image or checked state");
    auto second = doc()->id;
    auto previous = store.read(store.docs[1]);
    check(previous.cursor == 2 && previous.commands.back().text == L"그림판 입력 보존",
          "new canvas did not commit pending text on the previous image");
    select(1);
    check(doc()->id == first && pixel(*rendered, 100, 50) == 0xff111827,
          "returning to an earlier painting lost its edited pixels");
    select(0);
    auto target = store.root / L"blank-paint.png";
    savePathPicker = [&](const std::wstring &) { return target.wstring(); };
    command(ID_SAVE_CURRENT);
    savePathPicker = {};
    auto exported = loadImage(target);
    check(exported->GetWidth() == 1280 && exported->GetHeight() == 720 && blank(*exported),
          "current canvas save exported a checked previous image instead");
    int copies = 0;
    clipboardVerifier = [&](const ClipboardImage &formats) {
        auto header = (const BITMAPINFOHEADER *)formats.dib.data();
        ++copies;
        check(header->biWidth == 1280 && header->biHeight == 720, "paint copy size mismatch");
    };
    command(ID_COPY);
    clipboardVerifier = {};
    check(copies == 1, "paint copy did not prepare clipboard formats");
    Store readback(store.root);
    auto restored = readback.read(readback.docs.front());
    auto restoredImage = loadImage(readback.png(restored));
    check(restored.id == second && restored.name.find(L"그림판 ") == 0 && blank(*restoredImage),
          "blank canvas did not persist in history");
    auto same = doc()->id;
    busy = true;
    newCanvas();
    busy = false;
    check(store.docs.size() == 2 && doc()->id == same, "blank canvas interrupted capture mode");
    select(1);
    statusText(L"그림판 · 왼쪽 막대와 휠로 펜 굵기 조절 · Ctrl+S로 현재 그림 저장");
    preview(main, store.root / L"paint-canvas-preview.png");
    RECT normal{};
    GetWindowRect(main, &normal);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    RECT slider{}, canvasRect{}, client{};
    GetWindowRect(widthControl, &slider);
    GetWindowRect(canvas, &canvasRect);
    GetClientRect(main, &client);
    MapWindowPoints(nullptr, main, (POINT *)&slider, 2);
    MapWindowPoints(nullptr, main, (POINT *)&canvasRect, 2);
    check(slider.left >= 0 && slider.right <= canvasRect.left && slider.bottom <= client.bottom &&
              slider.top >= toolbarBottom,
          "vertical pen width control overlaps the canvas or clips in a small window");
    for (auto [id, h] : topControls) {
        RECT rect{};
        GetWindowRect(h, &rect);
        MapWindowPoints(nullptr, main, (POINT *)&rect, 2);
        require(rect.left >= 0 && rect.right <= client.right && rect.bottom <= toolbarBottom,
                "new toolbar button clips at minimum width");
    }
    preview(main, store.root / L"paint-canvas-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(normal), rh(normal), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    setPenWidth((int)oldStroke);
    while (!store.docs.empty())
        removeHistory(0);
    check(!penWidthVisible() && !(GetWindowLongPtrW(widthControl, GWL_STYLE) & WS_VISIBLE) &&
              fs::exists(target),
          "empty history retained the width rail or deleted the exported painting");
    std::ofstream report(store.root / L"paint-verification.json");
    report << "{\n  \"checks_passed\": " << checks
           << ",\n  \"opaque_white_canvas_and_activation\": true,\n"
              "  \"previous_images_and_pending_text_preserved\": true,\n"
              "  \"pen_drawing_undo_and_persistence\": true,\n"
              "  \"current_canvas_save_and_copy\": true,\n"
              "  \"vertical_pen_width_drag_wheel_and_bounds\": true,\n"
              "  \"pen_only_rail_and_small_window_layout\": true,\n"
              "  \"fit_action_removed\": true,\n"
              "  \"computer_use\": false,\n  \"system_clipboard_modified\": false\n}\n";
}
} // namespace lc
