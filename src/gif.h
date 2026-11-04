#pragma once
#include "core.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <wincodec.h>
namespace lc {
template <class T> struct ComRelease {
    void operator()(T *p) const {
        if (p)
            p->Release();
    }
};
template <class T> using Com = std::unique_ptr<T, ComRelease<T>>;
struct GifProfile {
    const wchar_t *label;
    int fps, edge;
};
inline constexpr GifProfile GifProfiles[] = {{L"표준 · 12fps · 최대 1280px", 12, 1280},
                                             {L"선명 · 15fps · 최대 1600px", 15, 1600},
                                             {L"작게 · 10fps · 최대 960px", 10, 960}};
inline constexpr ULONGLONG GifDurationLimit = 120000;
inline constexpr uint64_t GifByteLimit = 80ull * 1024 * 1024;
SIZE gifSize(int width, int height, int profile);
struct GifFrame {
    std::vector<uint32_t> pixels;
    ULONGLONG time = 0;
};
struct GifStats {
    UINT frames = 0;
    uint64_t pixels = 0, bytes = 0;
    ULONGLONG duration = 0;
};
// WIC owns color quantization and LZW. Only a pending frame and the last committed pixels are retained.
class GifWriter {
    Com<IWICImagingFactory> factory;
    Com<IWICStream> stream;
    Com<IWICBitmapEncoder> encoder;
    int width, height;
    ULONGLONG origin = 0, encodedCs = 0;
    bool started = false;
    GifFrame pending;
    std::vector<uint32_t> base;
    void flush(ULONGLONG end);

  public:
    GifStats stats;
    GifWriter(const fs::path &file, int width, int height);
    void append(GifFrame frame);
    void finish(ULONGLONG end);
};
// A full frame queue is capped at three; overload drops a sample, preserving its real elapsed time.
class GifSession {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<GifFrame> queue;
    bool finishing = false, cancelled = false;
    ULONGLONG end = 0;
    void run(int width, int height);

  public:
    fs::path file;
    std::atomic<bool> done{false}, limit{false};
    std::atomic<uint64_t> bytes{0};
    std::atomic<UINT> dropped{0}, queuePeak{0};
    GifStats stats;
    std::string error;
    GifSession(fs::path file, int width, int height);
    ~GifSession();
    bool submit(GifFrame frame);
    void finish(ULONGLONG time, bool cancel = false);
};
// Preview decodes one delta frame at a time, composing into a single bounded canvas.
class GifPreview {
    Com<IWICImagingFactory> factory;
    Com<IWICBitmapDecoder> decoder;

  public:
    UINT width = 0, height = 0, count = 0, next = 0, delay = 100;
    std::vector<uint32_t> pixels;
    explicit GifPreview(const fs::path &file);
    void advance();
};
class GifCaptureBuffer {
    HDC source = nullptr, memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    uint32_t *pixels = nullptr;
    int width, height;

  public:
    GifCaptureBuffer(int width, int height);
    ~GifCaptureBuffer();
    GifFrame capture(RECT area, bool cursor, ULONGLONG time);
};
void saveGifFile(const fs::path &source, const fs::path &destination);
} // namespace lc
