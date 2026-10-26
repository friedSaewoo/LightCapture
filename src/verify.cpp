#include "app.h"
#include <iomanip>
namespace lc {
void App::verify() {
    bool defaultAvailable = hotkey;
    bool alternate = false, conflictSafe = false;
    alternate = registerKey(MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F10, false);
    if (alternate) {
        UINT retained = hotId;
        bool rival =
            RegisterHotKey(main, 800, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F9) != FALSE;
        if (rival) {
            bool rejected = !registerKey(MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F9, false);
            conflictSafe = rejected && hotkey && hotId == retained;
            UnregisterHotKey(main, 800);
        }
    }
    store.settings.autoCopy = false;
    // An old enabled preference must not trigger an image export on capture.
    store.settings.autoExport = true;
    store.settings.folder = (store.root / L"unexpected-auto-export").wstring();
    auto image = std::make_unique<Bitmap>(960, 600, PixelFormat32bppARGB);
    {
        Graphics g(image.get());
        g.Clear(Color(255, 255, 255, 255));
        Gdiplus::SolidBrush dark(Color(255, 27, 39, 60)), muted(Color(255, 75, 88, 109)),
            blue(Color(255, 37, 99, 235));
        Gdiplus::Font title(L"맑은 고딕", 30, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font body(L"맑은 고딕", 18, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.DrawString(L"캡처하고, 표시하고, 원하는 곳에 저장하기", -1, &title, Gdiplus::PointF(48, 45), &dark);
        g.DrawString(L"LightCapture  |  Ctrl + Shift + X", -1, &body, Gdiplus::PointF(48, 106), &muted);
        for (int i = 0; i < 5; i++) {
            Gdiplus::SolidBrush row(Color(255, i % 2 ? 242 : 249, i % 2 ? 245 : 250, 252));
            g.FillRectangle(&row, 48, 175 + i * 60, 864, 52);
            auto text =
                L"항목 " + std::to_wstring(i + 1) + L"     선택한 영역의 원본 픽셀과 편집 결과를 유지합니다.";
            g.DrawString(text.c_str(), -1, &body, Gdiplus::PointF(66, 188 + i * 60), &dark);
        }
        g.FillRectangle(&blue, 48, 516, 230, 44);
        Gdiplus::SolidBrush white(Color::White);
        g.DrawString(L"비민감 자동 검증 자료", -1, &body, Gdiplus::PointF(64, 525), &white);
    }
    add(std::move(image));
    require(!fs::exists(store.settings.folder), "capture automatically exported an image");
    store.settings.autoExport = false;
    Command box;
    box.tool = Tool::Rectangle;
    box.rect = {45, 170, 910, 226};
    box.width = 3;
    box.color = 0xffe5484d;
    edit(box);
    Command arrow;
    arrow.tool = Tool::Arrow;
    arrow.points = {{800, 120}, {740, 176}};
    arrow.rect = {740, 120, 800, 176};
    arrow.width = 4;
    arrow.color = 0xff2563eb;
    edit(arrow);
    Command text;
    text.tool = Tool::Text;
    text.rect = {48, 470, 700, 515};
    text.text = L"원본 · 편집 기록 · 최근 목록을 로컬에 보관";
    text.fontSize = 18;
    text.color = 0xff2563eb;
    edit(text);
    auto final = output(*original, *doc());
    saveImage(*final, store.root / L"edited-fixture.png");
    // This renders only our own off-screen controls. No desktop capture or input injection occurs.
    RECT r;
    GetClientRect(main, &r);
    HDC dc = GetDC(main), mem = CreateCompatibleDC(dc);
    HBITMAP hb = CreateCompatibleBitmap(dc, rw(r), rh(r));
    auto old = SelectObject(mem, hb);
    RECT white{0, 0, rw(r), rh(r)};
    FillRect(mem, &white, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SendMessageW(main, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    {
        auto preview = Image(Bitmap::FromHBITMAP(hb, nullptr));
        saveImage(*preview, store.root / L"editor-preview.png");
    }
    SelectObject(mem, old);
    DeleteObject(hb);
    DeleteDC(mem);
    ReleaseDC(main, dc);
    auto id = doc()->id;
    auto commands = doc()->cursor;
    auto previousMem = PROCESS_MEMORY_COUNTERS_EX{sizeof(PROCESS_MEMORY_COUNTERS_EX)};
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&previousMem, sizeof(previousMem));
    hide();
    require(!original && !rendered && thumbs.empty(), "hide retained decoded images");
    select(0);
    require(doc()->id == id && doc()->cursor == commands, "selection lost document state");
    undo();
    require(doc()->cursor == commands - 1, "UI undo failed");
    undo(true);
    require(doc()->cursor == commands, "UI redo failed");
    Store reloaded(store.root);
    require(reloaded.docs.size() == store.docs.size(), "UI persistence count mismatch");
    require(reloaded.docs[0].cursor == commands, "UI persisted edits mismatch");
    int pickerCalls = 0;
    savePathPicker = [&](const std::wstring &) {
        ++pickerCalls;
        if (pickerCalls == 3)
            return std::wstring{};
        return (store.root / (L"chosen-save-" + std::to_wstring(pickerCalls) + L".png")).wstring();
    };
    save();
    save();
    auto retainedExport = lastExport;
    save();
    require(pickerCalls == 3 && fs::exists(store.root / L"chosen-save-1.png") &&
                fs::exists(store.root / L"chosen-save-2.png") && lastExport == retainedExport,
            "Save must ask every time and cancellation must retain the last export");
    savePathPicker = {};
    auto retainedFixture = clone(*original);
    add(clone(*retainedFixture));
    add(clone(*retainedFixture));
    select(2);
    auto originalPointer = original.get();
    auto renderedPointer = rendered.get();
    removeHistory(0);
    require(store.docs.size() == 2 && selected == 1 && doc()->id == id && original.get() == originalPointer &&
                rendered.get() == renderedPointer && doc()->cursor == commands,
            "removing another thumbnail changed the active image or its edits");
    RECT firstRow{};
    SendMessageW(list, LB_GETITEMRECT, 0, (LPARAM)&firstRow);
    auto close = historyCloseRect(firstRow, GetDpiForWindow(list));
    require(historyCloseHit({(close.left + close.right) / 2, (close.top + close.bottom) / 2}) == 0 &&
                historyCloseHit({firstRow.left + 1, firstRow.top + 1}) == -1,
            "thumbnail close hit area mismatch");
    // Exercise the app's own close handler directly, without automating a desktop or injecting input.
    LPARAM point = MAKELPARAM((close.left + close.right) / 2, (close.top + close.bottom) / 2);
    listProc(list, WM_LBUTTONDOWN, MK_LBUTTON, point, 1, (DWORD_PTR)this);
    listProc(list, WM_LBUTTONUP, 0, point, 1, (DWORD_PTR)this);
    require(store.docs.size() == 1 && selected == 0 && doc()->id == id,
            "thumbnail close handler did not remove its item");
    removeHistory(0);
    require(store.docs.empty() && selected == -1 && !original && !rendered,
            "last thumbnail removal retained a stale image");
    Store afterRemoval(store.root);
    require(afterRemoval.docs.empty() && fs::exists(retainedExport),
            "thumbnail removal was not persistent or deleted an exported file");
    add(std::move(retainedFixture));
    verifyEditor();
    verifyHistory();
    verifyPixelSelection();
    std::ofstream f(store.root / L"app-verification.json");
    f << "{\n  \"default_hotkey_available\": " << (defaultAvailable ? "true" : "false")
      << ",\n  \"temporary_hotkey_registered\": " << (alternate ? "true" : "false")
      << ",\n  \"conflict_preserves_existing_registration\": " << (conflictSafe ? "true" : "false")
      << ",\n  \"document_roundtrip\": true,\n  \"undo_redo\": true,\n  \"hide_releases_images\": true,\n  "
         "\"automatic_export_disabled\": true,\n  \"save_picker_every_time_and_cancel\": true,\n  "
         "\"thumbnail_close_hit_and_handler\": true,\n  \"thumbnail_removal_keeps_active_edits\": true,\n  "
         "\"last_thumbnail_clears_editor\": true,\n  \"thumbnail_removal_persistent\": true,\n  "
         "\"export_survives_thumbnail_removal\": true,\n  \"desktop_capture_performed\": false,\n  "
         "\"input_injection_performed\": false\n}\n";
    log("app verification completed");
    hide();
}
} // namespace lc
