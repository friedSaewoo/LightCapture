#include "gif.h"
namespace lc {
static void wic(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        char detail[32];
        snprintf(detail, sizeof(detail), " (0x%08lx)", (unsigned long)result);
        throw std::runtime_error(std::string(operation) + detail);
    }
}
template <class T, class F> static Com<T> comObject(F make, const char *operation) {
    T *raw = nullptr;
    wic(make(&raw), operation);
    return Com<T>(raw);
}
static Com<IWICImagingFactory> makeFactory() {
    return comObject<IWICImagingFactory>(
        [](auto p) {
            return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IWICImagingFactory, (void **)p);
        },
        "GIF imaging factory unavailable");
}
static void metadata(IWICMetadataQueryWriter *writer, LPCWSTR name, UINT value, VARTYPE type = VT_UI2) {
    PROPVARIANT v{};
    v.vt = type;
    if (type == VT_UI1)
        v.bVal = (BYTE)value;
    else
        v.uiVal = (USHORT)value;
    wic(writer->SetMetadataByName(name, &v), "GIF metadata write failed");
}
static UINT metadata(IWICMetadataQueryReader *reader, LPCWSTR name) {
    PROPVARIANT v{};
    wic(reader->GetMetadataByName(name, &v), "GIF metadata read failed");
    UINT result = v.vt == VT_UI2 ? v.uiVal : v.vt == VT_UI1 ? v.bVal : 0;
    PropVariantClear(&v);
    return result;
}
SIZE gifSize(int width, int height, int profile) {
    require(width > 0 && height > 0 && profile >= 0 && profile < 3, "invalid GIF dimensions or profile");
    double ratio = std::min(1., (double)GifProfiles[profile].edge / std::max(width, height));
    return {std::max(1L, (LONG)std::lround(width * ratio)), std::max(1L, (LONG)std::lround(height * ratio))};
}
GifWriter::GifWriter(const fs::path &file, int w, int h) : width(w), height(h) {
    require(w > 0 && h > 0 && w <= 1600 && h <= 1600, "GIF output dimensions exceed limit");
    factory = makeFactory();
    stream =
        comObject<IWICStream>([&](auto p) { return factory->CreateStream(p); }, "GIF stream unavailable");
    wic(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE), "GIF temporary file unavailable");
    encoder = comObject<IWICBitmapEncoder>(
        [&](auto p) { return factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, p); },
        "GIF encoder unavailable");
    wic(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache), "GIF encoder initialization failed");
    auto writer = comObject<IWICMetadataQueryWriter>(
        [&](auto p) { return encoder->GetMetadataQueryWriter(p); }, "GIF global metadata unavailable");
    metadata(writer.get(), L"/logscrdesc/Width", w);
    metadata(writer.get(), L"/logscrdesc/Height", h);
    BYTE application[] = {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
    BYTE loop[] = {3, 1, 0, 0, 0};
    PROPVARIANT v{};
    v.vt = VT_VECTOR | VT_UI1;
    v.caub = {sizeof(application), application};
    wic(writer->SetMetadataByName(L"/appext/Application", &v), "GIF loop application write failed");
    v.caub = {sizeof(loop), loop};
    wic(writer->SetMetadataByName(L"/appext/Data", &v), "GIF loop count write failed");
}
void GifWriter::append(GifFrame frame) {
    require(frame.pixels.size() == (size_t)width * height, "GIF sample dimensions mismatch");
    if (!started) {
        origin = frame.time;
        pending = std::move(frame);
        started = true;
        return;
    }
    require(frame.time >= pending.time, "GIF sample timestamp out of order");
    // Ignore the unused capture alpha byte. Stationary pixels remain exact.
    bool same = true;
    for (size_t i = 0; i < frame.pixels.size(); ++i)
        if ((frame.pixels[i] & 0xffffff) != (pending.pixels[i] & 0xffffff)) {
            same = false;
            break;
        }
    if (same)
        return;
    flush(frame.time);
    base = std::move(pending.pixels);
    pending = std::move(frame);
}
void GifWriter::flush(ULONGLONG end) {
    auto totalCs = std::max(encodedCs + 2, (end - origin + 5) / 10);
    UINT delay = (UINT)std::min<ULONGLONG>(65535, totalCs - encodedCs);
    RECT changed{0, 0, width, height};
    if (!base.empty()) {
        changed = {width, height, 0, 0};
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                size_t i = (size_t)y * width + x;
                if ((base[i] & 0xffffff) != (pending.pixels[i] & 0xffffff)) {
                    changed.left = std::min(changed.left, (LONG)x);
                    changed.top = std::min(changed.top, (LONG)y);
                    changed.right = std::max(changed.right, (LONG)x + 1);
                    changed.bottom = std::max(changed.bottom, (LONG)y + 1);
                }
            }
    }
    require(rw(changed) > 0 && rh(changed) > 0, "GIF delta rectangle empty");
    std::vector<BYTE> patch((size_t)rw(changed) * rh(changed) * 4);
    for (int y = 0; y < rh(changed); ++y) {
        auto source = pending.pixels.data() + (size_t)(changed.top + y) * width + changed.left;
        auto destination = (uint32_t *)(patch.data() + (size_t)y * rw(changed) * 4);
        for (int x = 0; x < rw(changed); ++x)
            destination[x] = source[x] | 0xff000000;
    }
    auto bitmap = comObject<IWICBitmap>(
        [&](auto p) {
            return factory->CreateBitmapFromMemory(rw(changed), rh(changed), GUID_WICPixelFormat32bppBGRA,
                                                   rw(changed) * 4, (UINT)patch.size(), patch.data(), p);
        },
        "GIF frame bitmap unavailable");
    auto palette =
        comObject<IWICPalette>([&](auto p) { return factory->CreatePalette(p); }, "GIF palette unavailable");
    wic(palette->InitializeFromBitmap(bitmap.get(), 256, FALSE), "GIF adaptive palette failed");
    auto converter = comObject<IWICFormatConverter>([&](auto p) { return factory->CreateFormatConverter(p); },
                                                    "GIF color converter unavailable");
    wic(converter->Initialize(bitmap.get(), GUID_WICPixelFormat8bppIndexed, WICBitmapDitherTypeNone,
                              palette.get(), 0, WICBitmapPaletteTypeCustom),
        "GIF color reduction failed");
    auto frame = comObject<IWICBitmapFrameEncode>([&](auto p) { return encoder->CreateNewFrame(p, nullptr); },
                                                  "GIF frame encoder unavailable");
    wic(frame->Initialize(nullptr), "GIF frame initialization failed");
    wic(frame->SetSize(rw(changed), rh(changed)), "GIF frame size failed");
    auto format = GUID_WICPixelFormat8bppIndexed;
    wic(frame->SetPixelFormat(&format), "GIF indexed format failed");
    wic(frame->SetPalette(palette.get()), "GIF local palette failed");
    auto writer = comObject<IWICMetadataQueryWriter>([&](auto p) { return frame->GetMetadataQueryWriter(p); },
                                                     "GIF frame metadata unavailable");
    metadata(writer.get(), L"/imgdesc/Left", changed.left);
    metadata(writer.get(), L"/imgdesc/Top", changed.top);
    metadata(writer.get(), L"/grctlext/Disposal", 1, VT_UI1);
    metadata(writer.get(), L"/grctlext/Delay", delay);
    wic(frame->WriteSource(converter.get(), nullptr), "GIF frame write failed");
    wic(frame->Commit(), "GIF frame commit failed");
    ++stats.frames;
    stats.pixels += (uint64_t)rw(changed) * rh(changed);
    encodedCs += delay;
    stats.duration = encodedCs * 10;
    STATSTG st{};
    wic(stream->Stat(&st, STATFLAG_NONAME), "GIF temporary size unavailable");
    stats.bytes = st.cbSize.QuadPart;
}
void GifWriter::finish(ULONGLONG end) {
    require(started, "GIF recording has no frames");
    require(end >= pending.time, "GIF stop timestamp out of order");
    flush(end);
    wic(encoder->Commit(), "GIF finalization failed");
    encoder.reset();
    STATSTG st{};
    wic(stream->Stat(&st, STATFLAG_NONAME), "GIF final size unavailable");
    stats.bytes = st.cbSize.QuadPart;
    stream.reset();
    pending.pixels.clear();
    base.clear();
}
GifSession::GifSession(fs::path path, int w, int h) : file(std::move(path)) {
    worker = std::thread([this, w, h] { run(w, h); });
}
GifSession::~GifSession() {
    finish(0, true);
    if (worker.joinable())
        worker.join();
    std::error_code ignored;
    fs::remove(file, ignored);
}
bool GifSession::submit(GifFrame frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (finishing || done)
        return false;
    if (queue.size() >= 3) {
        ++dropped;
        return false;
    }
    queue.push_back(std::move(frame));
    queuePeak = std::max(queuePeak.load(), (UINT)queue.size());
    wake.notify_one();
    return true;
}
void GifSession::finish(ULONGLONG time, bool cancel) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!finishing)
        end = time;
    finishing = true;
    cancelled = cancelled || cancel;
    wake.notify_one();
}
void GifSession::run(int w, int h) {
    auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool initialized = SUCCEEDED(result);
    try {
        wic(result, "GIF worker COM initialization failed");
        GifWriter writer(file, w, h);
        for (;;) {
            GifFrame frame;
            ULONGLONG stopped = 0;
            bool finishNow = false, cancelNow = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return finishing || !queue.empty(); });
                cancelNow = cancelled;
                if (!cancelNow && !queue.empty()) {
                    frame = std::move(queue.front());
                    queue.pop_front();
                } else if (finishing) {
                    finishNow = true;
                    stopped = end;
                }
            }
            if (cancelNow)
                break;
            if (finishNow) {
                writer.finish(stopped);
                stats = writer.stats;
                bytes = stats.bytes;
                break;
            }
            writer.append(std::move(frame));
            bytes = writer.stats.bytes;
            if (writer.stats.bytes >= GifByteLimit)
                limit = true;
        }
    } catch (const std::exception &ex) {
        error = ex.what();
    }
    if (initialized)
        CoUninitialize();
    done = true; // Publishes stats and error before the UI reads them.
}
GifPreview::GifPreview(const fs::path &file) {
    factory = makeFactory();
    decoder = comObject<IWICBitmapDecoder>(
        [&](auto p) {
            return factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, p);
        },
        "GIF preview decoder unavailable");
    auto reader = comObject<IWICMetadataQueryReader>(
        [&](auto p) { return decoder->GetMetadataQueryReader(p); }, "GIF preview metadata unavailable");
    width = metadata(reader.get(), L"/logscrdesc/Width");
    height = metadata(reader.get(), L"/logscrdesc/Height");
    wic(decoder->GetFrameCount(&count), "GIF preview frame count unavailable");
    require(width && height && width <= 1600 && height <= 1600 && count && count <= 10000,
            "GIF preview dimensions or frame count invalid");
    pixels.resize((size_t)width * height, 0xffffffff);
    advance();
}
void GifPreview::advance() {
    if (!next)
        std::fill(pixels.begin(), pixels.end(), 0xffffffff);
    auto frame = comObject<IWICBitmapFrameDecode>([&](auto p) { return decoder->GetFrame(next, p); },
                                                  "GIF preview frame unavailable");
    auto reader = comObject<IWICMetadataQueryReader>([&](auto p) { return frame->GetMetadataQueryReader(p); },
                                                     "GIF preview frame metadata unavailable");
    UINT left = metadata(reader.get(), L"/imgdesc/Left"), top = metadata(reader.get(), L"/imgdesc/Top");
    delay = std::max(20u, metadata(reader.get(), L"/grctlext/Delay") * 10);
    require(metadata(reader.get(), L"/grctlext/Disposal") == 1, "unsupported GIF preview disposal");
    UINT w = 0, h = 0;
    wic(frame->GetSize(&w, &h), "GIF preview frame size unavailable");
    require(w && h && left + w <= width && top + h <= height, "GIF preview delta outside canvas");
    auto converter = comObject<IWICFormatConverter>([&](auto p) { return factory->CreateFormatConverter(p); },
                                                    "GIF preview converter unavailable");
    wic(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom),
        "GIF preview conversion failed");
    std::vector<uint32_t> patch((size_t)w * h);
    wic(converter->CopyPixels(nullptr, w * 4, (UINT)patch.size() * 4, (BYTE *)patch.data()),
        "GIF preview pixels unavailable");
    for (UINT y = 0; y < h; ++y)
        std::copy_n(patch.data() + (size_t)y * w, w, pixels.data() + (size_t)(top + y) * width + left);
    next = (next + 1) % count;
}
GifCaptureBuffer::GifCaptureBuffer(int w, int h) : width(w), height(h) {
    require(w > 0 && h > 0 && w <= 1600 && h <= 1600, "GIF capture dimensions invalid");
    source = GetDC(nullptr);
    memory = CreateCompatibleDC(source);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
    if (bitmap && memory)
        old = SelectObject(memory, bitmap);
    if (!source || !memory || !bitmap || !old || old == HGDI_ERROR) {
        if (bitmap)
            DeleteObject(bitmap);
        if (memory)
            DeleteDC(memory);
        if (source)
            ReleaseDC(nullptr, source);
        throw std::runtime_error("GIF capture buffer unavailable");
    }
    SetStretchBltMode(memory, HALFTONE);
    SetBrushOrgEx(memory, 0, 0, nullptr);
}
GifCaptureBuffer::~GifCaptureBuffer() {
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, source);
}
GifFrame GifCaptureBuffer::capture(RECT area, bool cursor, ULONGLONG time) {
    require(StretchBlt(memory, 0, 0, width, height, source, area.left, area.top, rw(area), rh(area),
                       SRCCOPY | CAPTUREBLT) != FALSE,
            "GIF screen transfer failed");
    if (cursor) {
        CURSORINFO ci{sizeof(ci)};
        if (GetCursorInfo(&ci) && ci.flags == CURSOR_SHOWING && inside(area, ci.ptScreenPos)) {
            ICONINFO ii{};
            if (GetIconInfo(ci.hCursor, &ii)) {
                double sx = (double)width / rw(area), sy = (double)height / rh(area);
                BITMAP bm{};
                GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
                int cw = bm.bmWidth, ch = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
                DrawIconEx(memory, (int)std::lround((ci.ptScreenPos.x - area.left - (LONG)ii.xHotspot) * sx),
                           (int)std::lround((ci.ptScreenPos.y - area.top - (LONG)ii.yHotspot) * sy),
                           ci.hCursor, std::max(1, (int)std::lround(cw * sx)),
                           std::max(1, (int)std::lround(ch * sy)), 0, nullptr, DI_NORMAL);
                DeleteObject(ii.hbmMask);
                if (ii.hbmColor)
                    DeleteObject(ii.hbmColor);
            }
        }
    }
    require(GdiFlush() != FALSE, "GIF screen transfer flush failed");
    GifFrame frame;
    frame.time = time;
    frame.pixels.assign(pixels, pixels + (size_t)width * height);
    return frame;
}
void saveGifFile(const fs::path &source, const fs::path &destination) {
    require(!destination.empty(), "GIF save destination empty");
    auto temporary = destination;
    temporary += L".LightCapture-" + std::to_wstring(GetTickCount64()) + L".partial";
    if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("GIF copy to destination failed");
    }
    try {
        atomicReplace(temporary, destination);
    } catch (...) {
        DeleteFileW(temporary.c_str());
        throw;
    }
}
} // namespace lc
