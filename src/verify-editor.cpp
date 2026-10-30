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
