#pragma once
#include "core.h"
#include "clipboard.h"
#include "overlay.h"
#include <functional>
#include <map>
#include <set>
#include <shobjidl.h>
namespace lc {
struct FolderDialogRelease {
    void operator()(IFileOpenDialog *dialog) const {
        dialog->Release();
    }
};
using FolderDialog = std::unique_ptr<IFileOpenDialog, FolderDialogRelease>;
FolderDialog makeFolderDialog(const fs::path &initial, const std::wstring &title);
fs::path chooseFolder(HWND owner, const fs::path &initial, const std::wstring &title);
enum : UINT {
    ID_CAPTURE = 100,
    ID_FREE,
    ID_WINDOW,
    ID_UNIT,
    ID_FULL,
    ID_SCROLL,
    ID_FIXED,
    ID_OPEN,
    ID_COPY = 120,
    ID_SAVE,
    ID_SAVEAS,
    ID_RESERVED_SAVEALL,
    ID_PRINT,
    ID_EXTERNAL,
    ID_UNDO,
    ID_REDO,
    ID_DELETE,
    ID_SETTINGS,
    ID_FOLDER,
    ID_HIDE,
    ID_QUIT,
    ID_RESTORE,
    ID_ABOUT,
    ID_ORIGINAL,
    ID_DUPLICATE,
    ID_SCROLLH,
    ID_SCROLLMANUAL,
    ID_MONITOR,
    ID_COLOR = 150,
    ID_FONT,
    ID_ZOOMIN,
    ID_ZOOMOUT,
    ID_FIT,
    ID_ACTUAL,
    ID_FILL,
    ID_WIDTH,
    ID_LIST = 190,
    ID_CANVAS,
    ID_TOOL = 200,
    ID_PALETTE = 300,
    ID_TEXT_INPUT = 350,
    ID_CHECK_ALL = 360,
    ID_CHECK_COUNT,
    ID_HISTORY_DELETE,
    WM_FINISH_TEXT = WM_APP + 8,
    WM_TRAY = WM_APP + 1,
    WM_SECOND = WM_APP + 2
};
struct WindowTarget {
    HWND hwnd;
    RECT rect;
    bool child;
};
class App {
  public:
    Store store;
    HWND main = nullptr, canvas = nullptr, list = nullptr, status = nullptr, overlay = nullptr,
         scrollBar = nullptr;
    HINSTANCE instance = nullptr;
    HFONT uiFont = nullptr;
    std::map<UINT, HCURSOR> penCursors;
    HWND tooltip = nullptr, hoveredButton = nullptr, textInput = nullptr;
    HWND quickBar = nullptr;
    HFONT quickFont = nullptr;
    HFONT inputFont = nullptr;
    HBRUSH textBrush = nullptr;
    Image textBackdrop;
    Command textDraft;
    bool finishingText = false, positioningText = false;
    POINT canvasMouse{};
    bool quit = false, busy = false, hotkey = false, ready = false, testMode = false;
    UINT hotId = 1, taskbarMessage = 0;
    NOTIFYICONDATAW tray{};
    int selected = -1, selectedObject = -1;
    Image original, rendered, snapshot;
    OverlayRenderer overlayRenderer;
    std::wstring pressedClose;
    std::set<std::wstring> checkedHistory;
    std::wstring historyAnchor;
    bool swallowListUp = false;
    HWND historySave = nullptr, historyDelete = nullptr, historyCheckAll = nullptr,
         historyCheckCount = nullptr;
    // Internal verification can replace the native picker without injecting user input.
    std::function<std::wstring(const std::wstring &)> savePathPicker;
    std::function<fs::path()> batchFolderPicker;
    std::function<bool(size_t)> historyDeleteConfirm;
    fs::path lastSaveFolder;
    std::function<void(const ClipboardImage &)> clipboardVerifier;
    std::map<std::wstring, Image> thumbs;
    fs::path lastExport;
    Tool tool = Tool::Pen;
    uint32_t color = 0xffe5484d;
    float stroke = 3, fontSize = 20;
    std::wstring fontName = L"맑은 고딕";
    int textStyle = 0;
    bool fill = false, dragging = false, resizing = false;
    Command draft;
    bool pixelReady = false, pixelSelecting = false, pixelMoving = false;
    RECT pixelRect{}, pixelSource{};
    POINT pixelStart{};
    Image pixelPatch, pixelBackdrop;
    float zoom = 1;
    int panX = 0, panY = 0;
    POINT mouse{}, start{}, dragOrigin{}, panOrigin{};
    bool panning = false;
    Mode mode = Mode::Rectangle;
    RECT virtualRect{}, selection{};
    std::vector<POINT> polygon;
    std::vector<WindowTarget> targets;
    HWND previous = nullptr;
    bool previouslyVisible = false;
    bool waitingForHide = false;
    ULONGLONG captureNotBefore = 0, captureStartDeadline = 0;
    unsigned hiddenFrameCount = 0;
    bool selectionReady = false, movingSelection = false;
    RECT selectionBefore{};
    int scrollFrames = 0;
    RECT scrollRect{};
    HWND scrollTarget = nullptr;
    bool scrollHorizontal = false, scrollManual = false;
    bool currentMonitor = false;
    Image scrollPrevious, scrollResult;
    std::wstring warning;
    std::vector<std::pair<UINT, HWND>> topControls;
    HWND widthControl = nullptr, toolControl = nullptr;
    explicit App(fs::path root) : store(std::move(root)) {}
    ~App() {
        for (auto &entry : penCursors)
            DestroyCursor(entry.second);
        if (inputFont)
            DeleteObject(inputFont);
        if (textBrush)
            DeleteObject(textBrush);
        if (quickFont)
            DeleteObject(quickFont);
    }
    HCURSOR canvasCursor(UINT dpi);
    void createToolbar();
    int layoutToolbar(int width, UINT dpi);
    void paintToolbarButton(DRAWITEMSTRUCT *item);
    void refreshToolbar();
    void setTool(Tool next);
    void setColor(uint32_t next);
    void clearPixelSelection();
    void pixelDown(POINT imagePoint);
    void pixelMove(POINT imagePoint);
    void pixelUp();
    void paintPixelSelection(Graphics &g, RECT imageBounds, RECT view);
    void verifyPixelSelection();
    void startText(Command command);
    void finishText(bool accept = true);
    void positionText();
    void updateTextStyle();
    void growText();
    RECT textScreenRect();
    static LRESULT CALLBACK toolbarProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK textProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    Document *doc() {
        return selected >= 0 && selected < (int)store.docs.size() ? &store.docs[selected] : nullptr;
    }
    void create(HINSTANCE inst, bool hidden);
    void show();
    void hide();
    void updateTitle();
    void statusText(const std::wstring &);
    void refreshList();
    void createHistoryActions();
    void refreshHistoryActions();
    void checkHistory(int index, bool range);
    void checkAllHistory();
    std::vector<std::wstring> checkedHistoryIds() const;
    void saveChecked();
    void deleteCheckedHistory();
    static RECT historyCheckRect(RECT row, UINT dpi);
    int historyItemAt(POINT point);
    int historyCheckHit(POINT point);
    void select(int index);
    void rebuild();
    void persist();
    void add(Image image);
    void edit(Command c);
    void command(UINT id);
    void layout();
    void paintCanvas(HDC dc);
    void paintList(DRAWITEMSTRUCT *d);
    void begin(Mode m);
    void createOverlay();
    void createQuickBar(HWND parent);
    void switchCaptureMode(Mode next);
    void openHistoryFromCapture();
    static LRESULT CALLBACK quickProc(HWND, UINT, WPARAM, LPARAM);
    void cancelCapture(bool restoreEditor = true);
    bool advanceCaptureStart(bool hidden, bool composed, ULONGLONG now);
    void pollCaptureStart();
    void finishCapture();
    void paintOverlay(HDC dc, HRGN dirty);
    void invalidateOverlay(RECT before, POINT oldMouse);
    void hover(POINT p);
    void startScroll(RECT r);
    void scrollTick();
    void stopScroll(bool cancelled = false, bool restoreEditor = true);
    void copy();
    void save(bool saveAs = false);
    void print();
    void open();
    void settingsDialog();
    bool registerKey(UINT mods, UINT key, bool notify);
    void notifyTray();
    void trayMenu();
    POINT toImage(POINT p);
    RECT imageBounds();
    void setZoom(float factor);
    void canvasDown(POINT p);
    void canvasMove(POINT p);
    void canvasUp(POINT p);
    void eraseAt(POINT p);
    void deleteSelected();
    void removeHistory(int index);
    static RECT historyCloseRect(RECT row, UINT dpi);
    int historyCloseHit(POINT p);
    static LRESULT CALLBACK listProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void undo(bool redo = false);
    void report(const std::exception &ex);
    void log(const std::string &s);
    void startup(bool enabled);
    void verify();
    void verifyEditor();
    void verifyHistory();
    void saveView();
    void duplicateObject();
    void deleteObject();
    void editText();
    static LRESULT CALLBACK mainProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK canvasProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK overlayProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK scrollProc(HWND, UINT, WPARAM, LPARAM);
};
extern App *app;
HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
             UINT id);
bool textPrompt(HWND owner, const wchar_t *title, std::wstring &value, bool multiline = false);
std::wstring hotkeyName(UINT modifiers, UINT key);
} // namespace lc
