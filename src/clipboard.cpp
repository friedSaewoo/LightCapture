#include "clipboard.h"
namespace lc {
namespace {
struct GlobalBlock {
    HGLOBAL handle = nullptr;
    explicit GlobalBlock(const std::vector<BYTE> &bytes) {
        handle = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        require(handle != nullptr, "clipboard allocation failed");
        void *data = GlobalLock(handle);
        if (!data) {
            GlobalFree(handle);
            handle = nullptr;
            throw std::runtime_error("clipboard memory lock failed");
        }
        memcpy(data, bytes.data(), bytes.size());
        GlobalUnlock(handle);
    }
    ~GlobalBlock() {
        if (handle)
            GlobalFree(handle);
    }
    GlobalBlock(const GlobalBlock &) = delete;
    GlobalBlock &operator=(const GlobalBlock &) = delete;
    bool publish(UINT format) {
        if (!SetClipboardData(format, handle))
            return false;
        handle = nullptr; // Ownership has transferred to Windows.
        return true;
    }
};
struct StreamRelease {
    void operator()(IStream *stream) const {
        stream->Release();
    }
};
} // namespace
ClipboardImage makeClipboardImage(Bitmap &image) {
    const int w = image.GetWidth(), h = image.GetHeight();
    require(w > 0 && h > 0 && (uint64_t)w * h * 4 <= 512ull * 1024 * 1024,
            "clipboard image exceeds 512 MiB limit");
    // Some consumers ignore or disagree about DIBV5 alpha. Give every format the
    // same opaque, white-composited image instead of relying on alpha synthesis.
    Bitmap opaque(w, h, PixelFormat32bppARGB);
    require(opaque.GetLastStatus() == Gdiplus::Ok, "clipboard image allocation failed");
    {
        Graphics g(&opaque);
        require(g.Clear(Color::White) == Gdiplus::Ok && g.DrawImage(&image, 0, 0, w, h) == Gdiplus::Ok,
                "clipboard image rendering failed");
    }
    ClipboardImage result;
    const size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    result.dib.resize(sizeof(BITMAPINFOHEADER) + stride * h);
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = w;
    info.biHeight = h; // Positive height: conventional bottom-up 24-bit DIB.
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biCompression = BI_RGB;
    info.biSizeImage = (DWORD)(stride * h);
    memcpy(result.dib.data(), &info, sizeof(info));
    result.dibv5.resize(sizeof(BITMAPV5HEADER) + (size_t)w * h * 4);
    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(v5);
    v5.bV5Width = w;
    v5.bV5Height = -h;
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5SizeImage = (DWORD)((size_t)w * h * 4);
    v5.bV5RedMask = 0x00ff0000;
    v5.bV5GreenMask = 0x0000ff00;
    v5.bV5BlueMask = 0x000000ff;
    v5.bV5AlphaMask = 0xff000000;
    v5.bV5CSType = 0x73524742; // LCS_sRGB, without the SDK's multicharacter literal.
    v5.bV5Intent = LCS_GM_IMAGES;
    memcpy(result.dibv5.data(), &v5, sizeof(v5));
    Gdiplus::BitmapData pixels{};
    Gdiplus::Rect area(0, 0, w, h);
    require(opaque.LockBits(&area, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &pixels) == Gdiplus::Ok,
            "clipboard pixels unavailable");
    for (int y = 0; y < h; ++y) {
        auto source = (const BYTE *)pixels.Scan0 + (ptrdiff_t)y * pixels.Stride;
        auto rgb = result.dib.data() + sizeof(info) + (size_t)(h - y - 1) * stride;
        auto rgba = result.dibv5.data() + sizeof(v5) + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            memcpy(rgb + (size_t)x * 3, source + (size_t)x * 4, 3);
            memcpy(rgba + (size_t)x * 4, source + (size_t)x * 4, 3);
            rgba[(size_t)x * 4 + 3] = 255;
        }
    }
    opaque.UnlockBits(&pixels);
    IStream *raw = nullptr;
    require(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &raw)), "clipboard PNG stream failed");
    std::unique_ptr<IStream, StreamRelease> stream(raw);
    auto png = encoder(L"image/png");
    require(opaque.Save(stream.get(), &png, nullptr) == Gdiplus::Ok, "clipboard PNG encoding failed");
    STATSTG stat{};
    require(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0,
            "clipboard PNG length unavailable");
    HGLOBAL memory = nullptr;
    require(SUCCEEDED(GetHGlobalFromStream(stream.get(), &memory)), "clipboard PNG memory unavailable");
    auto bytes = (const BYTE *)GlobalLock(memory);
    require(bytes != nullptr, "clipboard PNG lock failed");
    result.png.assign(bytes, bytes + (size_t)stat.cbSize.QuadPart);
    GlobalUnlock(memory);
    return result;
}
bool publishClipboardImage(HWND owner, const ClipboardImage &image) {
    require(owner != nullptr && IsWindow(owner), "clipboard owner window unavailable");
    // Prepare every allocation before touching the user's existing clipboard.
    GlobalBlock png(image.png), dib(image.dib), v5(image.dibv5);
    UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    require(pngFormat != 0, "clipboard PNG registration failed");
    bool opened = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        Sleep(20);
    }
    if (!opened)
        return false;
    if (!EmptyClipboard()) {
        CloseClipboard();
        throw std::runtime_error("clipboard clear failed");
    }
    // PNG and a conventional explicit DIB precede DIBV5. Avoid GetHBITMAP and
    // Windows' implicit DIBV5-to-DIB conversion in older image consumers.
    bool pngOk = png.publish(pngFormat);
    bool dibOk = dib.publish(CF_DIB);
    bool v5Ok = v5.publish(CF_DIBV5);
    CloseClipboard();
    require(dibOk, "clipboard bitmap transfer failed");
    require(pngOk && v5Ok, "clipboard supplementary image transfer failed");
    return true;
}
} // namespace lc
