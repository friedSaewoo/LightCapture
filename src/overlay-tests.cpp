#include "overlay.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include "legacy-overlay.h"
using namespace lc;
struct Target {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    uint32_t *pixels = nullptr;
    int w, h;
    Target(int width, int height) : w(width), h(height) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
        require(bitmap && dc, "test target allocation failed");
        old = SelectObject(dc, bitmap);
    }
    ~Target() {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};
static void check(bool ok, const char *label) {
    require(ok, label);
    std::cout << "PASS " << label << "\n";
}
static double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
static std::vector<uint32_t> pixels(int w, int h) {
    std::vector<uint32_t> data((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            data[(size_t)y * w + x] =
                0xff000000 | (((x / 3) % 256) << 16) | (((y / 2) % 256) << 8) | ((x + y) % 256);
    return data;
}
int wmain(int argc, wchar_t **argv) {
    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &si, nullptr);
    int exitCode = 0;
    try {
        fs::path root = argc > 1 ? argv[1] : L"overlay-evidence";
        fs::create_directories(root);
        {
            const int w = 800, h = 500;
            auto data = pixels(w, h);
            Bitmap image(w, h, w * 4, PixelFormat32bppARGB, (BYTE *)data.data());
            OverlayRenderer incremental, reference;
            incremental.prepare(image);
            reference.prepare(image);
            Target actual(w, h), expected(w, h);
            RECT before{};
            POINT oldMouse{};
            std::vector<POINT> polygon;
            auto full = CreateRectRgn(0, 0, w, h);
            incremental.paint(actual.dc, full, before, oldMouse, {-1920, -100}, Mode::Rectangle, {}, false);
            for (int i = 0; i < 80; ++i) {
                POINT p{(i * 89 + 7) % w, (i * 47 + 11) % h};
                RECT r = i % 5 == 0 ? RECT{} : normalized({(i * 13) % w, (i * 17) % h}, p);
                Mode mode = i % 9 == 0 ? Mode::Freeform : Mode::Rectangle;
                polygon = {{r.left, r.top}, {r.right, r.top + rh(r) / 2}, {r.left, r.bottom}};
                // A transition to/from a polygon may change the entire previous selection.
                auto dirty = incremental.damage(before, oldMouse, r, p, true);
                incremental.paint(actual.dc, dirty, r, p, {-1920, -100}, mode, polygon, i % 3 == 0);
                reference.paint(expected.dc, full, r, p, {-1920, -100}, mode, polygon, i % 3 == 0);
                GdiFlush();
                require(memcmp(actual.pixels, expected.pixels, (size_t)w * h * 4) == 0,
                        "partial selector repaint left stale pixels (polygon/edges/negative origin)");
                DeleteObject(dirty);
                before = r;
                oldMouse = p;
            }
            check(true, "80 partial repaints equal full reference including polygon and edge panels");
            // Rectangle-only damage uses XOR instead of repainting the full selected area.
            before = {};
            oldMouse = {};
            incremental.paint(actual.dc, full, before, oldMouse, {}, Mode::Rectangle, {}, false);
            auto coalesced = CreateRectRgn(0, 0, 0, 0);
            for (int i = 1; i <= 120; ++i) {
                POINT p{(i * 53) % w, (i * 37) % h};
                RECT r = normalized({150, 180}, p);
                auto dirty = incremental.damage(before, oldMouse, r, p, false);
                CombineRgn(coalesced, coalesced, dirty, RGN_OR);
                DeleteObject(dirty);
                if (i % 4 == 0) {
                    incremental.paint(actual.dc, coalesced, r, p, {}, Mode::Rectangle, {}, false);
                    reference.paint(expected.dc, full, r, p, {}, Mode::Rectangle, {}, false);
                    GdiFlush();
                    require(memcmp(actual.pixels, expected.pixels, (size_t)w * h * 4) == 0,
                            "coalesced rectangle invalidation left stale pixels");
                    SetRectRgn(coalesced, 0, 0, 0, 0);
                }
                before = r;
                oldMouse = p;
            }
            check(true, "120 rectangle movements coalesced into 30 paints equal full reference");
            DeleteObject(coalesced);
            DeleteObject(full);
            incremental.clear();
            reference.clear();
            DWORD baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            for (int i = 0; i < 100; ++i) {
                incremental.prepare(image);
                incremental.clear();
            }
            GdiFlush();
            check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baseline,
                  "100 selector prepare/clear cycles release every GDI resource");
            incremental.prepare(image);
            auto dirty =
                incremental.damage({10, 10, 700, 400}, {700, 400}, {10, 10, 701, 401}, {701, 401}, false);
            check(!PtInRegion(dirty, 200, 200) && PtInRegion(dirty, 700, 399),
                  "one-pixel drag excludes unchanged selection interior");
            DeleteObject(dirty);
            incremental.paint(actual.dc, nullptr, {100, 100, 450, 300}, {450, 300}, {}, Mode::Rectangle, {},
                              false);
            GdiFlush();
            auto preview = Image(Bitmap::FromHBITMAP(actual.bitmap, nullptr));
            saveImage(*preview, root / L"synthetic-selector.png");
            check(actual.pixels[200 * w + 200] == data[200 * w + 200],
                  "selected pixels preserve original RGB");
            auto dark = actual.pixels[50 * w + 50] & 0x00ffffff;
            check(dark != (data[50 * w + 50] & 0x00ffffff), "outside selection is dimmed");
        }
        std::ofstream json(root / L"overlay-benchmark.json");
        json << "{\n  \"kind\": \"synthetic offscreen render CPU time; not input-to-display latency\",\n"
                "  \"frames_per_path\": 60,\n  \"desktop_capture\": false,\n  \"cases\": [\n";
        bool first = true;
        for (auto size : {POINT{1920, 1080}, POINT{3840, 2160}, POINT{7680, 2160}}) {
            int w = size.x, h = size.y;
            auto data = pixels(w, h);
            Bitmap image(w, h, w * 4, PixelFormat32bppARGB, (BYTE *)data.data());
            Target target(w, h);
            OverlayRenderer renderer;
            auto started = std::chrono::steady_clock::now();
            renderer.prepare(image);
            GdiFlush();
            double prepareMs = milliseconds(started);
            std::vector<double> oldTimes, newTimes;
            auto full = CreateRectRgn(0, 0, w, h);
            // Warm both paths before timing. The old function is extracted from the v0.1.0 package.
            legacyPaint(target.dc, &image, w, h, {100, 100, w / 2, h / 2}, {w / 2, h / 2});
            GdiFlush();
            renderer.paint(target.dc, full, {}, {}, {}, Mode::Rectangle, {}, false);
            GdiFlush();
            for (int path = 0; path < 2; ++path) {
                RECT before{};
                POINT oldMouse{};
                renderer.paint(target.dc, full, before, oldMouse, {}, Mode::Rectangle, {}, false);
                GdiFlush();
                for (int i = 0; i < 60; ++i) {
                    POINT mouse{w / 3 + i * 7, h / 3 + i * 3};
                    RECT selection{100, 100, mouse.x, mouse.y};
                    started = std::chrono::steady_clock::now();
                    if (!path)
                        legacyPaint(target.dc, &image, w, h, selection, mouse);
                    else {
                        auto dirty = renderer.damage(before, oldMouse, selection, mouse, false);
                        renderer.paint(target.dc, dirty, selection, mouse, {}, Mode::Rectangle, {}, false);
                        DeleteObject(dirty);
                    }
                    GdiFlush();
                    (path ? newTimes : oldTimes).push_back(milliseconds(started));
                    before = selection;
                    oldMouse = mouse;
                }
            }
            DeleteObject(full);
            std::sort(oldTimes.begin(), oldTimes.end());
            std::sort(newTimes.begin(), newTimes.end());
            if (!first)
                json << ",\n";
            first = false;
            json << std::fixed << std::setprecision(3) << "    {\"width\": " << w << ", \"height\": " << h
                 << ", \"prepare_ms\": " << prepareMs << ", \"v010_median_ms\": " << oldTimes[30]
                 << ", \"v010_p95_ms\": " << oldTimes[56] << ", \"v011_median_ms\": " << newTimes[30]
                 << ", \"v011_p95_ms\": " << newTimes[56] << "}";
            std::cout << "BENCH " << w << "x" << h << " old median/p95 " << oldTimes[30] << "/"
                      << oldTimes[56] << " ms; new " << newTimes[30] << "/" << newTimes[56] << " ms\n";
        }
        json << "\n  ]\n}\n";
        std::cout << "RESULT 6 overlay checks passed; 3 synthetic benchmark cases completed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "\n";
        exitCode = 1;
    }
    Gdiplus::GdiplusShutdown(token);
    return exitCode;
}
