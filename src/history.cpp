#include "app.h"
namespace lc {
FolderDialog makeFolderDialog(const fs::path &initial, const std::wstring &title) {
    IFileOpenDialog *raw = nullptr;
    require(
        SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))),
        "folder dialog creation failed");
    FolderDialog dialog(raw);
    DWORD options = 0;
    require(SUCCEEDED(dialog->GetOptions(&options)) &&
                SUCCEEDED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                             FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR)),
            "folder dialog options failed");
    require(SUCCEEDED(dialog->SetTitle(title.c_str())) &&
                SUCCEEDED(dialog->SetOkButtonLabel(L"이 폴더 선택")),
            "folder dialog labels failed");
    if (!initial.empty()) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dialog->SetFolder(item);
            item->Release();
        }
    }
    return dialog;
}
fs::path chooseFolder(HWND owner, const fs::path &initial, const std::wstring &title) {
    auto dialog = makeFolderDialog(initial, title);
    HRESULT result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    require(SUCCEEDED(result), "folder selection failed");
    IShellItem *item = nullptr;
    require(SUCCEEDED(dialog->GetResult(&item)), "selected folder unavailable");
    PWSTR path = nullptr;
    result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    require(SUCCEEDED(result), "selected folder path unavailable");
    fs::path selected(path);
    CoTaskMemFree(path);
    return selected;
}
void App::createHistoryActions() {
    historyDelete =
        control(main, L"BUTTON", L"삭제", WS_TABSTOP | BS_OWNERDRAW, 0, 0, 92, 28, ID_HISTORY_DELETE);
    historySave = control(main, L"BUTTON", L"저장", WS_TABSTOP | BS_OWNERDRAW, 0, 0, 92, 28, ID_SAVE);
    SetWindowSubclass(historyDelete, toolbarProc, 1, (DWORD_PTR)this);
    SetWindowSubclass(historySave, toolbarProc, 1, (DWORD_PTR)this);
    historyCheckAll =
        control(main, L"BUTTON", L"전체 체크", BS_3STATE | WS_TABSTOP, 0, 0, 104, 24, ID_CHECK_ALL);
    historyCheckCount = control(main, L"STATIC", L"0개 선택", SS_RIGHT, 0, 0, 88, 24, ID_CHECK_COUNT);
    TOOLINFOW ti{sizeof(ti)};
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = main;
    ti.uId = (UINT_PTR)historySave;
    ti.lpszText = (wchar_t *)L"체크한 캡처만 저장합니다. Ctrl+S";
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    ti.uId = (UINT_PTR)historyDelete;
    ti.lpszText = (wchar_t *)L"체크한 캡처만 목록과 내부 보관함에서 삭제합니다.";
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}
void App::refreshHistoryActions() {
    std::set<std::wstring> valid;
    for (auto &d : store.docs)
        valid.insert(d.id);
    for (auto i = checkedHistory.begin(); i != checkedHistory.end();)
        if (!valid.count(*i))
            i = checkedHistory.erase(i);
        else
            ++i;
    if (!historyAnchor.empty() && !valid.count(historyAnchor))
        historyAnchor.clear();
    if (!historySave)
        return;
    EnableWindow(historySave, !checkedHistory.empty());
    EnableWindow(historyDelete, !checkedHistory.empty());
    EnableWindow(historyCheckAll, !store.docs.empty());
    UINT state = checkedHistory.empty()                       ? BST_UNCHECKED
                 : checkedHistory.size() == store.docs.size() ? BST_CHECKED
                                                              : BST_INDETERMINATE;
    SendMessageW(historyCheckAll, BM_SETCHECK, state, 0);
    SetWindowTextW(historyCheckCount, (std::to_wstring(checkedHistory.size()) + L"개 선택").c_str());
}
RECT App::historyCheckRect(RECT row, UINT dpi) {
    auto s = [&](int v) { return MulDiv(v, dpi ? dpi : 96, 96); };
    int top = row.top + (rh(row) - s(18)) / 2;
    return {row.left + s(12), top, row.left + s(30), top + s(18)};
}
int App::historyItemAt(POINT p) {
    RECT client{};
    GetClientRect(list, &client);
    if (!inside(client, p))
        return -1;
    auto hit = (DWORD)SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(p.x, p.y));
    if (HIWORD(hit) || LOWORD(hit) >= store.docs.size())
        return -1;
    RECT row{};
    if (SendMessageW(list, LB_GETITEMRECT, LOWORD(hit), (LPARAM)&row) == LB_ERR || !inside(row, p))
        return -1;
    return LOWORD(hit);
}
int App::historyCheckHit(POINT p) {
    int index = historyItemAt(p);
    if (index < 0)
        return -1;
    RECT row{};
    if (SendMessageW(list, LB_GETITEMRECT, index, (LPARAM)&row) == LB_ERR)
        return -1;
    auto r = historyCheckRect(row, GetDpiForWindow(list));
    InflateRect(&r, 3, 3);
    return inside(r, p) ? index : -1;
}
void App::checkHistory(int index, bool range) {
    if (index < 0 || index >= (int)store.docs.size())
        return;
    if (range) {
        int anchor = selected >= 0 ? selected : index;
        for (int i = 0; i < (int)store.docs.size(); ++i)
            if (store.docs[i].id == historyAnchor) {
                anchor = i;
                break;
            }
        for (int i = std::min(anchor, index); i <= std::max(anchor, index); ++i)
            checkedHistory.insert(store.docs[i].id);
        if (historyAnchor.empty())
            historyAnchor = store.docs[anchor].id;
    } else {
        auto &id = store.docs[index].id;
        if (!checkedHistory.erase(id))
            checkedHistory.insert(id);
        historyAnchor = id;
    }
    refreshList();
}
void App::checkAllHistory() {
    if (checkedHistory.size() == store.docs.size())
        checkedHistory.clear();
    else
        for (auto &d : store.docs)
            checkedHistory.insert(d.id);
    refreshList();
}
std::vector<std::wstring> App::checkedHistoryIds() const {
    std::vector<std::wstring> ids;
    for (auto &d : store.docs)
        if (checkedHistory.count(d.id))
            ids.push_back(d.id);
    return ids;
}
void App::saveChecked() {
    finishText();
    refreshHistoryActions();
    auto ids = checkedHistoryIds();
    if (ids.empty()) {
        statusText(L"저장할 캡처의 체크박스를 선택하세요.");
        return;
    }
    fs::path folder;
    if (testMode && batchFolderPicker)
        folder = batchFolderPicker();
    else {
        auto title = std::to_wstring(ids.size()) + L"개 캡처를 저장할 폴더를 선택하세요";
        folder = chooseFolder(main, lastSaveFolder.empty() ? fs::path(store.settings.folder) : lastSaveFolder,
                              title);
    }
    if (folder.empty())
        return;
    lastSaveFolder = folder;
    Settings naming = store.settings;
    naming.folder = folder.wstring();
    size_t saved = 0;
    std::wstring errors;
    for (auto &id : ids) {
        try {
            auto item =
                std::find_if(store.docs.begin(), store.docs.end(), [&](auto &d) { return d.id == id; });
            require(item != store.docs.end(), "capture was removed before export");
            auto detail = store.read(*item);
            auto image = loadImage(store.png(detail));
            auto final = output(*image, detail);
            saveImage(*final, exportPath(naming), store.settings.jpeg);
            ++saved;
        } catch (const std::exception &) {
            errors += id + L"\n";
        }
    }
    auto text = std::to_wstring(saved) + L"개 저장 완료";
    statusText(text + L": " + folder.wstring());
    if (!errors.empty() && !testMode)
        message(main, text + L"\n저장하지 못한 캡처:\n" + errors, true);
}
void App::deleteCheckedHistory() {
    finishText();
    refreshHistoryActions();
    auto ids = checkedHistoryIds();
    if (ids.empty()) {
        statusText(L"삭제할 캡처의 체크박스를 선택하세요.");
        return;
    }
    bool confirmed;
    if (testMode)
        confirmed = historyDeleteConfirm && historyDeleteConfirm(ids.size());
    else {
        auto question = L"체크한 캡처 " + std::to_wstring(ids.size()) +
                        L"개를 목록과 내부 보관함에서 삭제할까요?\n직접 저장한 이미지 파일은 유지됩니다.";
        confirmed = MessageBoxW(main, question.c_str(), AppName, MB_YESNO | MB_ICONQUESTION) == IDYES;
    }
    if (!confirmed)
        return;
    std::set<std::wstring> targets(ids.begin(), ids.end());
    auto active = doc() ? doc()->id : std::wstring{};
    int previousIndex = selected;
    size_t removed = 0;
    std::wstring errors;
    for (int i = (int)store.docs.size() - 1; i >= 0; --i) {
        auto id = store.docs[i].id;
        if (!targets.count(id))
            continue;
        try {
            store.remove(i);
            thumbs.erase(id);
            ++removed;
        } catch (const std::exception &) {
            errors += id + L"\n";
        }
    }
    selected = -1;
    for (int i = 0; i < (int)store.docs.size(); ++i)
        if (store.docs[i].id == active)
            selected = i;
    if (selected < 0) {
        clearPixelSelection();
        original.reset();
        rendered.reset();
        selectedObject = -1;
        lastExport.clear();
        if (!store.docs.empty())
            select(std::clamp(previousIndex, 0, (int)store.docs.size() - 1));
    }
    refreshList();
    updateTitle();
    InvalidateRect(canvas, nullptr, FALSE);
    statusText(std::to_wstring(removed) + L"개 삭제했습니다. 직접 저장한 이미지 파일은 유지됩니다.");
    if (!errors.empty() && !testMode)
        message(main, L"삭제하지 못한 캡처:\n" + errors, true);
}
} // namespace lc
