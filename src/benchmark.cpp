#include "app.h"
#include <chrono>
#include <iomanip>
namespace lc {
using BenchClock = std::chrono::steady_clock;
static double elapsed(BenchClock::time_point start) {
    return std::chrono::duration<double, std::milli>(BenchClock::now() - start).count();
}
struct BenchCase {
    std::string action;
    int width, height;
    std::vector<double> samples;
};
struct BenchMemory {
    std::string stage;
    SIZE_T working, privateBytes, peakWorking;
    DWORD gdi, user;
};
static Image fixture(int width, int height) {
    auto image = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
    Graphics g(image.get());
    g.Clear(Color::White);
    Gdiplus::SolidBrush panel(Color(0xffe8eef8)), ink(Color(0xff172033));
    Gdiplus::Pen line(Color(0xff1761d7), 3);
    for (int y = 16; y < height; y += 48) {
        g.FillRectangle(&panel, 16, y, width - 32, 32);
        for (int x = 32; x < width - 30; x += 90)
            g.FillRectangle(&ink, x, y + 12, 60, 3);
    }
    g.DrawRectangle(&line, 20, 20, width - 40, height - 40);
    return image;
}
struct BenchTarget {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    BenchTarget(HWND canvas) {
        RECT r{};
        GetClientRect(canvas, &r);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = std::max(1, rw(r));
        info.bmiHeader.biHeight = -std::max(1, rh(r));
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(dc && bitmap, "benchmark render target failed");
        old = SelectObject(dc, bitmap);
    }
    ~BenchTarget() {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};
void App::benchmark() {
    require(testMode, "performance test must use isolated data");
    if (hotkey) {
        UnregisterHotKey(main, hotId);
        hotkey = false;
    }
    require(store.docs.empty(), "benchmark requires a fresh data directory");
    require(aboutText().find(L"알캡") == std::wstring::npos &&
                aboutText().find(Version) != std::wstring::npos,
            "about dialog contains removed comparison text or wrong version");
    std::vector<BenchCase> cases;
    std::vector<BenchMemory> memory;
    auto snap = [&](const std::string &stage) {
        PROCESS_MEMORY_COUNTERS_EX m{sizeof(m)};
        require(GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof(m)),
                "memory sample failed");
        memory.push_back({stage, m.WorkingSetSize, m.PrivateUsage, m.PeakWorkingSetSize,
                          GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
                          GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)});
    };
    auto settle = [](int ms) {
        auto until = GetTickCount64() + ms;
        do {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message != WM_HOTKEY && msg.message != WM_QUIT) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            Sleep(10);
        } while (GetTickCount64() < until);
    };
    auto timed = [&](const char *name, int w, int h, int n, const std::function<void(int)> &action) {
        BenchCase c{name, w, h, {}};
        for (int i = 0; i < n; ++i) {
            auto start = BenchClock::now();
            action(i);
            GdiFlush();
            c.samples.push_back(elapsed(start));
        }
        cases.push_back(std::move(c));
    };
    auto paint = [&]() {
        BenchTarget target(canvas);
        paintCanvas(target.dc);
        GdiFlush();
    };
    auto paintEditor = [&]() {
        BenchTarget target(main);
        SendMessageW(main, WM_PRINT, (WPARAM)target.dc, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        GdiFlush();
    };
    settle(500);
    snap("fresh_empty_idle");
    for (auto size : {POINT{1920, 1080}, POINT{3840, 2160}}) {
        int w = size.x, h = size.y;
        std::string tag = w == 1920 ? "1080p" : "4k";
        timed("add_capture_to_history", w, h, 3, [&](int) { add(fixture(w, h)); });
        snap(tag + "_loaded");
        timed("editor_cold_thumbnail_repaint", w, h, 1, [&](int) { paintEditor(); });
        timed("editor_cached_thumbnail_repaint", w, h, 12, [&](int) { paintEditor(); });
        timed("canvas_repaint", w, h, 30, [&](int) { paint(); });
        setTool(Tool::Pen);
        setPenWidth(3);
        auto b = imageBounds();
        auto toScreen = [&](int x, int y) {
            auto r = imageBounds();
            return POINT{r.left + (LONG)std::lround(x * zoom), r.top + (LONG)std::lround(y * zoom)};
        };
        canvasDown(toScreen(100, 100));
        timed("pen_drag_repaint", w, h, 60, [&](int i) {
            canvasMove(toScreen(120 + i * 4, 120 + i % 20));
            paint();
        });
        timed("pen_commit", w, h, 1, [&](int) { canvasUp(toScreen(400, 130)); });
        timed("undo_redo", w, h, 8, [&](int) {
            undo();
            undo(true);
        });
        setTool(Tool::Text);
        Command text;
        text.tool = Tool::Text;
        text.rect = {100, 200, 800, 450};
        text.fontSize = 24;
        startText(text);
        timed("text_replace_and_repaint", w, h, 30, [&](int i) {
            std::wstring value = L"한글 텍스트 입력 성능 확인 " + std::to_wstring(i) +
                                 L"\r\n편집 화면을 보며 글자를 바꿉니다.";
            SetWindowTextW(textInput, value.c_str());
            BenchTarget target(textInput);
            SendMessageW(textInput, WM_PRINTCLIENT, (WPARAM)target.dc, PRF_CLIENT);
        });
        timed("text_box_move", w, h, 20, [&](int i) {
            textDraft.rect = {100 + i * 2, 200, 800 + i * 2, 450};
            positionText();
            paint();
        });
        timed("text_commit", w, h, 1, [&](int) { finishText(); });
        setTool(Tool::PixelSelect);
        pixelDown({100, 100});
        pixelMove({700, 500});
        pixelUp();
        pixelDown({300, 300});
        timed("pixel_move_repaint", w, h, 30, [&](int i) {
            pixelMove({320 + i * 3, 320});
            paint();
        });
        timed("pixel_move_commit", w, h, 1, [&](int) { pixelUp(); });
        clearPixelSelection();
        pixelDown({120, 120});
        pixelMove({720, 520});
        pixelUp();
        pixelDown({720, 520});
        timed("pixel_resize_repaint", w, h, 30, [&](int i) {
            pixelMove({700 - i * 3, 500 - i});
            paint();
        });
        timed("pixel_resize_commit", w, h, 1, [&](int) { pixelUp(); });
        clearPixelSelection();
        setTool(Tool::Pen);
        selectedObject = -1;
        b = imageBounds();
        canvasDown({b.right, b.bottom});
        timed("canvas_growth_preview", w, h, 30, [&](int i) {
            growCanvas({b.right + 20 + i, b.bottom + 15 + i});
            paint();
        });
        timed("canvas_growth_commit", w, h, 1, [&](int) { finishCanvasGrow(); });
        auto patch = fixture(600, 400);
        auto bytes = encodeImageBytes(*patch);
        patch.reset();
        clipboardReader = [&]() { return decodeImageBytes(*bytes); };
        timed("paste_image", w, h, 3, [&](int) { command(ID_PASTE_IMAGE); });
        clipboardReader = {};
        auto r = doc()->commands.back().rect;
        canvasDown(toScreen(r.left + 100, r.top + 100));
        timed("pasted_image_move_repaint", w, h, 30, [&](int i) {
            canvasMove(toScreen(r.left + 110 + i * 2, r.top + 110));
            paint();
        });
        canvasUp(toScreen(r.left + 170, r.top + 110));
        snap(tag + "_edited");
        timed("copy_image_encoding", w, h, 3, [&](int) {
            auto result = output(*original, *doc());
            auto formats = makeClipboardImage(*result);
        });
        auto target = store.root / (L"benchmark-export-" + std::to_wstring(w) + L".png");
        savePathPicker = [&](const std::wstring &) { return target.wstring(); };
        timed("save_current_png", w, h, 3, [&](int) { command(ID_SAVE_CURRENT); });
        savePathPicker = {};
        timed("history_switch", w, h, 10, [&](int i) {
            select(i % 3);
            paint();
        });
        timed("hide_and_restore_decode", w, h, 3, [&](int) {
            hide();
            select(0);
        });
        timed("hide_to_tray_only", w, h, 1, [&](int) { hide(); });
        hide();
        settle(500);
        snap(tag + "_after_hide");
        while (!store.docs.empty())
            removeHistory(0);
    }
    for (int i = 0; i < 100; ++i) {
        auto image = fixture(320, 180);
        store.add(*image);
    }
    selected = 0;
    original.reset();
    rendered.reset();
    refreshList();
    select(0);
    timed("history_check_100_items", 320, 180, 40, [&](int i) { checkHistory(i % 100, false); });
    timed("history_repaint_100_items", 320, 180, 12, [&](int) { paintEditor(); });
    timed("history_select_all_100_items", 320, 180, 20, [&](int) { checkAllHistory(); });
    snap("100_history_items");
    auto target = store.root / L"batch-export";
    fs::create_directories(target);
    batchFolderPicker = [&]() { return target; };
    checkedHistory.clear();
    for (int i = 0; i < 10; ++i)
        checkedHistory.insert(store.docs[i].id);
    timed("save_10_checked_pngs", 320, 180, 1, [&](int) { saveChecked(); });
    batchFolderPicker = {};
    checkedHistory.clear();
    checkAllHistory();
    historyDeleteConfirm = [](size_t) { return true; };
    timed("delete_100_checked", 320, 180, 1, [&](int) { deleteCheckedHistory(); });
    historyDeleteConfirm = {};
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 10; ++i) {
            newCanvas();
            hide();
        }
        settle(200);
        snap("new_canvas_hide_cycles_" + std::to_string((round + 1) * 10));
    }
    hide();
    settle(1000);
    snap("final_idle_after_30_cycles");
    std::ofstream out(store.root / L"performance-benchmark.json");
    out << std::fixed << std::setprecision(3);
    out << "{\n  \"version\": \"" << std::string(Version, Version + wcslen(Version))
        << "\",\n"
           "  \"kind\": \"synthetic own-window action and offscreen CPU timing; no input-to-display "
           "latency\",\n"
           "  \"desktop_capture\": false,\n  \"system_clipboard_read_or_write\": false,\n  \"memory\": [\n";
    for (size_t i = 0; i < memory.size(); ++i) {
        auto &m = memory[i];
        if (i)
            out << ",\n";
        out << "    {\"stage\": \"" << m.stage << "\", \"working_set_mib\": " << m.working / 1048576.0
            << ", \"private_mib\": " << m.privateBytes / 1048576.0
            << ", \"peak_working_set_mib\": " << m.peakWorking / 1048576.0 << ", \"gdi_objects\": " << m.gdi
            << ", \"user_objects\": " << m.user << "}";
    }
    out << "\n  ],\n  \"actions\": [\n";
    for (size_t i = 0; i < cases.size(); ++i) {
        auto &c = cases[i];
        auto samples = c.samples;
        std::sort(samples.begin(), samples.end());
        if (i)
            out << ",\n";
        size_t p95 = std::max<size_t>(1, (size_t)std::ceil(samples.size() * .95)) - 1;
        auto slow = std::count_if(samples.begin(), samples.end(), [](double t) { return t > 16.667; });
        out << "    {\"action\": \"" << c.action << "\", \"width\": " << c.width
            << ", \"height\": " << c.height << ", \"samples\": " << samples.size()
            << ", \"median_ms\": " << samples[samples.size() / 2] << ", \"p95_ms\": " << samples[p95]
            << ", \"max_ms\": " << samples.back() << ", \"over_16_7ms\": " << slow << "}";
    }
    out << "\n  ]\n}\n";
    require((bool)out, "benchmark report write failed");
    hide();
}
} // namespace lc
