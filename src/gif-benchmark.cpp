#include "app.h"
#include <psapi.h>
#include <iomanip>
namespace lc {
struct GifMemory {
    double working, privateMiB;
};
static GifMemory gifMemory() {
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb = sizeof(m);
    require(GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof(m)),
            "GIF benchmark memory unavailable");
    return {m.WorkingSetSize / (1024. * 1024.), m.PrivateUsage / (1024. * 1024.)};
}
static std::vector<uint32_t> gifTextFixture(int w, int h) {
    Bitmap image(w, h, PixelFormat32bppARGB);
    {
        Graphics g(&image);
        g.Clear(Color::White);
        Gdiplus::SolidBrush blue(Color(0xff1761d7)), ink(Color(0xff172033)), light(Color(0xffedf1f6));
        g.FillRectangle(&blue, 0, 0, w, 64);
        g.FillRectangle(&light, w - 220, 64, 220, h - 64);
        Gdiplus::Font title(L"맑은 고딕", 24, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font body(L"맑은 고딕", 17, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush white(Color::White);
        g.DrawString(L"LightCapture · 화면 영역 GIF 녹화", -1, &title, Gdiplus::PointF(24, 16), &white);
        for (int y = 95; y < h - 36; y += 40)
            g.DrawString(L"화면의 글자를 확인하세요. 0123456789 / Capture, preview and save.", -1, &body,
                         Gdiplus::PointF(24, (float)y), &ink);
    }
    std::vector<uint32_t> pixels((size_t)w * h);
    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, w, h);
    require(image.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok,
            "GIF benchmark fixture unavailable");
    for (int y = 0; y < h; ++y)
        memcpy(pixels.data() + (size_t)y * w, (BYTE *)data.Scan0 + (ptrdiff_t)y * data.Stride, w * 4);
    image.UnlockBits(&data);
    return pixels;
}
void App::benchmarkGif() {
    require(testMode && store.docs.empty(), "GIF benchmark requires an empty isolated data directory");
    if (hotkey) {
        UnregisterHotKey(main, hotId);
        hotkey = false;
    }
    auto initial = gifMemory();
    std::ofstream report(store.root / L"gif-benchmark.json");
    report << std::fixed << std::setprecision(3)
           << "{\n  \"desktop_capture_performed\": false,\n"
              "  \"system_clipboard_modified\": false,\n  \"computer_use\": false,\n"
              "  \"initial_working_set_mib\": "
           << initial.working << ",\n  \"cases\": [\n";
    struct Case {
        int w, h, fps, frames;
        bool full;
        const char *name;
    };
    Case cases[] = {{1280, 720, 12, 360, false, "standard_ui_30s"},
                    {1600, 900, 15, 30, true, "sharp_full_motion_2s"},
                    {960, 540, 10, 60, true, "compact_full_motion_6s"}};
    for (int ci = 0; ci < 3; ++ci) {
        auto c = cases[ci];
        auto path = store.root / (std::wstring(c.name, c.name + strlen(c.name)) + L".gif");
        auto base = gifTextFixture(c.w, c.h);
        auto before = gifMemory();
        double peak = before.working, privatePeak = before.privateMiB;
        std::vector<double> timings, memorySamples;
        GifStats stats;
        {
            GifWriter writer(path, c.w, c.h);
            for (int i = 0; i < c.frames; ++i) {
                GifFrame frame;
                frame.time = 1000 + (ULONGLONG)i * 1000 / c.fps;
                frame.pixels = base;
                if (c.full) {
                    for (int y = 64; y < c.h; ++y)
                        for (int x = 0; x < c.w; ++x) {
                            int r = (x * 255 / c.w + i * 3) & 255, g = (y * 255 / c.h + i * 2) & 255,
                                b = ((x + y) * 128 / (c.w + c.h) + i * 5) & 255;
                            frame.pixels[(size_t)y * c.w + x] = 0xff000000 | (r << 16) | (g << 8) | b;
                        }
                } else {
                    int x = 40 + (i * 5) % (c.w - 300), y = c.h - 80;
                    for (int yy = y; yy < y + 20; ++yy)
                        for (int xx = x; xx < x + 16; ++xx)
                            frame.pixels[(size_t)yy * c.w + xx] = 0xffe5484d;
                }
                LARGE_INTEGER frequency{}, start{}, finish{};
                QueryPerformanceFrequency(&frequency);
                QueryPerformanceCounter(&start);
                writer.append(std::move(frame));
                QueryPerformanceCounter(&finish);
                timings.push_back((finish.QuadPart - start.QuadPart) * 1000. / frequency.QuadPart);
                auto memory = gifMemory();
                peak = std::max(peak, memory.working);
                privatePeak = std::max(privatePeak, memory.privateMiB);
                if (i % 30 == 29)
                    memorySamples.push_back(memory.working);
            }
            writer.finish(1000 + (ULONGLONG)c.frames * 1000 / c.fps);
            stats = writer.stats;
        }
        GifPreview preview(path);
        for (int i = 1; i < 30 && i < (int)preview.count; ++i)
            preview.advance();
        auto previewMemory = gifMemory();
        std::sort(timings.begin(), timings.end());
        if (ci)
            report << ",\n";
        report << "    {\"name\": \"" << c.name << "\", \"width\": " << c.w << ", \"height\": " << c.h
               << ", \"fps\": " << c.fps << ", \"frames\": " << stats.frames
               << ", \"duration_ms\": " << stats.duration << ", \"bytes\": " << stats.bytes
               << ", \"encoded_pixels\": " << stats.pixels
               << ", \"encode_median_ms\": " << timings[timings.size() / 2]
               << ", \"encode_p95_ms\": " << timings[(timings.size() - 1) * 95 / 100]
               << ", \"encode_max_ms\": " << timings.back() << ", \"working_set_peak_mib\": " << peak
               << ", \"private_peak_mib\": " << privatePeak
               << ", \"preview_working_set_mib\": " << previewMemory.working
               << ", \"working_set_every_30_frames_mib\": [";
        for (size_t i = 0; i < memorySamples.size(); ++i) {
            if (i)
                report << ", ";
            report << memorySamples[i];
        }
        report << "]}";
    }
    auto final = gifMemory();
    report << "\n  ],\n  \"after_working_set_mib\": " << final.working
           << ",\n  \"after_private_mib\": " << final.privateMiB
           << ",\n  \"limits\": \"Synthetic encoder and decoder only; real capture, input latency, GPU and "
              "video entropy vary.\"\n}\n";
}
} // namespace lc
