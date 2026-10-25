#include "app.h"
namespace lc {
static void historyPreview(HWND window, const fs::path &path) {
    RECT r{};
    GetClientRect(window, &r);
    HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, rw(r), rh(r));
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
static std::vector<fs::path> files(const fs::path &folder) {
    std::vector<fs::path> result;
    for (auto &f : fs::directory_iterator(folder))
        if (f.is_regular_file())
            result.push_back(f.path());
    return result;
}
void App::verifyHistory() {
    for (auto child : {GetDlgItem(main, ID_FILL), historyCheckAll, historyCheckCount}) {
        HDC dc = GetDC(child);
        auto brush = (HBRUSH)SendMessageW(main, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)child);
        LOGBRUSH color{};
        bool white = GetObjectW(brush, sizeof(color), &color) && color.lbColor == RGB(255, 255, 255) &&
                     GetBkColor(dc) == RGB(255, 255, 255);
        ReleaseDC(child, dc);
        require(white, "toolbar checkbox or count background differs from white surface");
    }
    auto initialFolder = store.root / L"폴더 선택 한글 경로";
    fs::create_directories(initialFolder);
    {
        // Configure the real Shell dialog without displaying or automating it.
        auto dialog = makeFolderDialog(initialFolder, L"저장 폴더 선택 검증");
        DWORD options = 0;
        DWORD wanted = FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        require(SUCCEEDED(dialog->GetOptions(&options)) && (options & wanted) == wanted,
                "modern folder dialog options mismatch");
        IShellItem *item = nullptr;
        require(SUCCEEDED(dialog->GetFolder(&item)), "folder dialog did not retain its initial folder");
        PWSTR path = nullptr;
        auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        item->Release();
        require(SUCCEEDED(result), "folder dialog initial path unavailable");
        fs::path actual(path);
        CoTaskMemFree(path);
        require(fs::equivalent(actual, initialFolder), "folder dialog initial Unicode path mismatch");
    }
    while (!store.docs.empty())
        removeHistory(0);
    auto make = [&](int index) {
        auto image = std::make_unique<Bitmap>(120 + index * 20, 80 + index * 10, PixelFormat32bppARGB);
        {
            Graphics g(image.get());
            g.Clear(Color(index == 0 ? 0xff4f46e5 : index == 1 ? 0xff16a34a : 0xfff59e0b));
        }
        add(std::move(image));
    };
    for (int i = 0; i < 3; ++i)
        make(i);
    auto active = doc()->id;
    auto originalPointer = original.get();
    require(checkedHistory.empty(), "new captures were unexpectedly checked");
    auto click = [&](int index, bool range, bool photo = false) {
        RECT row{};
        SendMessageW(list, LB_GETITEMRECT, index, (LPARAM)&row);
        auto box = historyCheckRect(row, GetDpiForWindow(list));
        auto point = photo ? MAKELPARAM(row.left + 60, row.top + 30)
                           : MAKELPARAM((box.left + box.right) / 2, (box.top + box.bottom) / 2);
        WPARAM keys = MK_LBUTTON | (range ? MK_SHIFT : 0);
        listProc(list, WM_LBUTTONDOWN, keys, point, 1, (DWORD_PTR)this);
        listProc(list, WM_LBUTTONUP, 0, point, 1, (DWORD_PTR)this);
    };
    click(0, false);
    click(2, true);
    require(checkedHistory.size() == 3 && doc()->id == active && original.get() == originalPointer,
            "range checking changed the active image or missed an item");
    checkAllHistory();
    click(2, false, true);
    require(checkedHistory.size() == 1 && checkedHistory.count(store.docs[2].id) &&
                doc()->id == store.docs[2].id && SendMessageW(list, LB_GETCURSEL, 0, 0) == 2,
            "photo click did not check and activate its image");
    click(2, false, true);
    require(checkedHistory.empty() && doc()->id == store.docs[2].id,
            "second photo click did not uncheck its image");
    click(2, false, true);
    click(0, true, true);
    require(checkedHistory.size() == 3 && doc()->id == active && SendMessageW(list, LB_GETCURSEL, 0, 0) == 0,
            "shift photo click missed range or active selection");
    checkHistory(1, false);
    auto ids = checkedHistoryIds();
    require(ids.size() == 2 && ids[0] == store.docs[0].id && ids[1] == store.docs[2].id &&
                SendMessageW(historyCheckAll, BM_GETCHECK, 0, 0) == BST_INDETERMINATE,
            "checkbox state was confused with active-list selection");
    RECT row{};
    SendMessageW(list, LB_GETITEMRECT, 0, (LPARAM)&row);
    auto box = historyCheckRect(row, GetDpiForWindow(list)),
         close = historyCloseRect(row, GetDpiForWindow(list));
    require(historyCheckHit({(box.left + box.right) / 2, (box.top + box.bottom) / 2}) == 0 &&
                historyCheckHit({(close.left + close.right) / 2, (close.top + close.bottom) / 2}) == -1,
            "checkbox and close button hit areas overlap");
    Command mark;
    mark.tool = Tool::Rectangle;
    mark.fill = true;
    mark.color = 0xffe5484d;
    mark.rect = {0, 0, 20, 20};
    edit(mark);
    int copied = 0;
    clipboardVerifier = [&](const ClipboardImage &formats) {
        auto header = *(const BITMAPV5HEADER *)formats.dibv5.data();
        auto pixels = formats.dibv5.data() + sizeof(BITMAPV5HEADER);
        require(!formats.png.empty() && !formats.dib.empty(), "copy compatibility formats missing");
        ++copied;
        require(header.bV5Width == 160 && header.bV5Height == -100 && header.bV5BitCount == 32 &&
                    header.bV5RedMask == 0x00ff0000 && header.bV5AlphaMask == 0xff000000,
                "copy image DIB header mismatch");
        const BYTE *pixel = pixels + (5 * 160 + 5) * 4;
        require(pixel[2] > 180 && pixel[1] < 100 && pixel[3] == 255, "copy image omitted the current edits");
    };
    require(GetMenuState(GetMenu(main), ID_COPY, MF_BYCOMMAND) != (UINT)-1, "image copy menu is missing");
    command(ID_COPY);
    require(copied == 1, "copy command did not prepare an image");
    clipboardVerifier = {};
    auto folder = store.root / L"checked-export";
    fs::create_directories(folder);
    auto patternBefore = store.settings.pattern;
    int formatBefore = store.settings.format;
    store.settings.pattern = L"체크 저장";
    store.settings.format = 0;
    Bitmap sentinel(17, 17, PixelFormat32bppARGB);
    {
        Graphics g(&sentinel);
        g.Clear(Color::Magenta);
    }
    auto existing = folder / L"체크 저장.png";
    saveImage(sentinel, existing);
    int picks = 0;
    batchFolderPicker = [&]() {
        ++picks;
        return folder;
    };
    command(ID_SAVE);
    require(picks == 1 && files(folder).size() == 3, "checked batch save count mismatch");
    require(lastSaveFolder == folder, "last chosen save folder was not retained");
    auto retained = loadImage(existing);
    require(retained->GetWidth() == 17, "batch save overwrote an existing file");
    std::set<UINT> widths;
    for (auto &path : files(folder))
        if (path != existing) {
            auto image = loadImage(path);
            widths.insert(image->GetWidth());
            if (image->GetWidth() == 160) {
                Color pixel;
                image->GetPixel(5, 5, &pixel);
                require(pixel.GetR() > 180 && pixel.GetG() < 100, "batch export lost edits");
            }
        }
    require(widths == std::set<UINT>{120, 160}, "unchecked image was exported or checked image was omitted");
    auto checkedBefore = checkedHistory;
    size_t docsBefore = store.docs.size();
    batchFolderPicker = []() { return fs::path{}; };
    command(ID_SAVE);
    require(files(folder).size() == 3 && checkedBefore == checkedHistory && store.docs.size() == docsBefore &&
                lastSaveFolder == folder,
            "canceling batch folder selection changed files or checks");
    checkAllHistory();
    require(checkedHistory.size() == store.docs.size(), "select-all did not check every image");
    checkAllHistory();
    require(checkedHistory.empty() && checkedHistoryIds().empty() && !IsWindowEnabled(historySave) &&
                !IsWindowEnabled(historyDelete),
            "save without checks was enabled or targeted unchecked history");
    picks = 0;
    batchFolderPicker = [&]() {
        ++picks;
        return folder;
    };
    command(ID_SAVE);
    int confirmations = 0;
    historyDeleteConfirm = [&](size_t) {
        ++confirmations;
        return true;
    };
    command(ID_HISTORY_DELETE);
    require(picks == 0 && files(folder).size() == 3,
            "save without checks opened a picker or exported images");
    require(confirmations == 0 && store.docs.size() == docsBefore,
            "delete without checks prompted or removed captures");
    auto allFolder = store.root / L"all-export";
    fs::create_directories(allFolder);
    checkHistory(0, false);
    require(checkedHistory.size() == 1 && IsWindowEnabled(historySave), "checked save was not enabled");
    batchFolderPicker = [&]() { return allFolder; };
    checkAllHistory();
    command(ID_SAVE);
    require(files(allFolder).size() == store.docs.size(), "unfiltered save-all count mismatch");
    widths.clear();
    for (auto &path : files(allFolder))
        widths.insert(loadImage(path)->GetWidth());
    require(widths == std::set<UINT>{120, 140, 160}, "save-all skipped unchecked images");
    checkAllHistory();
    batchFolderPicker = {};
    store.settings.pattern = patternBefore;
    store.settings.format = formatBefore;
    checkHistory(2, false);
    auto anchor = historyAnchor;
    make(3);
    checkHistory(1, true);
    require(historyAnchor == anchor && checkedHistory.size() == 3 && !checkedHistory.count(store.docs[0].id),
            "new capture shifted the range anchor or checked the wrong image");
    auto removed = store.docs[2].id;
    active = doc()->id;
    removeHistory(2);
    require(!checkedHistory.count(removed) && checkedHistory.size() == 2 && doc()->id == active,
            "removing a checked image damaged check state or active image");
    auto assertLayout = [&]() {
        RECT canvasRect{}, listRect{}, saveRect{}, deleteRect{};
        GetWindowRect(canvas, &canvasRect);
        GetWindowRect(list, &listRect);
        GetWindowRect(historySave, &saveRect);
        GetWindowRect(historyDelete, &deleteRect);
        require(listRect.left >= canvasRect.right && saveRect.top < listRect.top &&
                    deleteRect.top < listRect.top && deleteRect.left >= listRect.left &&
                    deleteRect.right < saveRect.left && saveRect.right <= listRect.right &&
                    !GetDlgItem(main, ID_RESERVED_SAVEALL) &&
                    GetMenuState(GetMenu(main), ID_RESERVED_SAVEALL, MF_BYCOMMAND) == (UINT)-1,
                "right-side delete/save order or removed save-all control mismatch");
    };
    assertLayout();
    historyPreview(main, store.root / L"right-history-preview.png");
    RECT size{};
    GetWindowRect(main, &size);
    SetWindowPos(main, nullptr, 0, 0, 720, 480, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    assertLayout();
    historyPreview(main, store.root / L"right-history-720-preview.png");
    SetWindowPos(main, nullptr, 0, 0, rw(size), rh(size), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    auto deletedIds = checkedHistory;
    auto activePointer = original.get();
    historyDeleteConfirm = [&](size_t count) {
        ++confirmations;
        require(count == 2, "delete confirmation count mismatch");
        return false;
    };
    command(ID_HISTORY_DELETE);
    require(store.docs.size() == 3 && checkedHistory == deletedIds && original.get() == activePointer,
            "cancelled deletion changed captures or checks");
    auto lockedId = store.docs[1].id;
    HANDLE locked = CreateFileW(store.png(store.docs[1]).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(locked != INVALID_HANDLE_VALUE, "delete-failure fixture lock failed");
    historyDeleteConfirm = [](size_t) { return true; };
    command(ID_HISTORY_DELETE);
    CloseHandle(locked);
    require(store.docs.size() == 2 && checkedHistory == std::set<std::wstring>{lockedId} &&
                doc()->id == active && original.get() == activePointer && fs::exists(existing),
            "partial checked deletion damaged failed or unchecked captures");
    command(ID_HISTORY_DELETE);
    require(store.docs.size() == 1 && checkedHistory.empty() && doc()->id == active &&
                original.get() == activePointer && fs::exists(existing) && files(allFolder).size() == 3,
            "checked deletion removed active unchecked image or exported files");
    Store afterDelete(store.root);
    require(afterDelete.docs.size() == 1 && afterDelete.docs[0].id == active,
            "checked deletion was not persistent");
    make(1);
    make(2);
    checkHistory(0, false);
    checkHistory(2, false);
    command(ID_HISTORY_DELETE);
    require(store.docs.size() == 1 && doc()->width == 140 && original && rendered && checkedHistory.empty(),
            "deleting active checked image failed to select a remaining image");
    checkAllHistory();
    command(ID_HISTORY_DELETE);
    historyDeleteConfirm = {};
    require(store.docs.empty() && !original && !rendered && selected == -1 && checkedHistory.empty() &&
                !IsWindowEnabled(historySave) && !IsWindowEnabled(historyDelete) &&
                !IsWindowEnabled(historyCheckAll),
            "empty history retained checks or enabled save controls");
    std::ofstream f(store.root / L"history-verification.json");
    f << "{\n  \"individual_and_shift_range_checks\": true,\n  \"checks_do_not_change_active_image\": true,\n"
         "  \"photo_click_toggles_and_activates\": true,\n  \"shift_photo_click_checks_range\": true,\n"
         "  \"copy_menu_command_prepares_edited_dib\": true,\n  \"system_clipboard_modified\": false,\n"
         "  \"modern_folder_dialog_options_and_unicode_initial_path\": true,\n"
         "  \"last_save_folder_retained_on_cancel\": true,\n  \"folder_dialog_displayed\": false,\n"
         "  \"checkbox_and_count_backgrounds_white\": true,\n"
         "  \"checked_only_export_with_edits\": true,\n  \"existing_files_preserved\": true,\n"
         "  \"folder_cancel_preserves_state\": true,\n  \"select_all_toggle\": true,\n"
         "  \"save_without_checks_disabled\": true,\n  \"check_all_then_save_exports_all\": true,\n"
         "  \"delete_without_checks_noop\": true,\n  \"delete_cancel_preserves_checks\": true,\n"
         "  \"checked_delete_only_and_exports_preserved\": true,\n"
         "  \"partial_delete_failure_retains_failed_checked_image\": true,\n"
         "  \"checked_delete_persistent_and_active_selection_safe\": true,\n"
         "  \"anchor_survives_new_capture\": true,\n"
         "  \"deletion_cleans_checks\": true,\n  \"right_sidebar_and_save_header\": true,\n"
         "  \"minimum_window_layout\": true,\n  \"empty_history_disables_save\": true,\n"
         "  \"desktop_capture\": false,\n  \"external_input\": false\n}\n";
}
} // namespace lc
