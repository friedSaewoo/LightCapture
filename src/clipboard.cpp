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
Image decodeClipboardDib(const std::vector<BYTE> &bytes) {
    require(bytes.size() >= sizeof(BITMAPINFOHEADER), "clipboard DIB header truncated");
    BITMAPINFOHEADER header{};
    memcpy(&header, bytes.data(), sizeof(header));
    require(header.biSize >= 40 && header.biSize <= 124 && header.biSize <= bytes.size() &&
                header.biWidth > 0 && header.biHeight != LONG_MIN && header.biHeight != 0 &&
                header.biPlanes == 1 && validCanvasSize(header.biWidth, std::abs(header.biHeight)) &&
                (header.biBitCount == 1 || header.biBitCount == 4 || header.biBitCount == 8 ||
                 header.biBitCount == 16 || header.biBitCount == 24 || header.biBitCount == 32) &&
                (header.biCompression == BI_RGB || header.biCompression == BI_BITFIELDS ||
                 header.biCompression == 6),
            "clipboard DIB format invalid");
    uint64_t colors = header.biClrUsed;
    if (!colors && header.biBitCount <= 8)
        colors = 1ull << header.biBitCount;
    uint64_t offset = header.biSize + colors * 4;
    if (header.biSize == 40 && header.biCompression != BI_RGB)
        offset += header.biCompression == 6 ? 16 : 12;
    uint64_t stride = (((uint64_t)header.biWidth * header.biBitCount + 31) / 32) * 4;
    uint64_t pixels = stride * std::abs(header.biHeight);
    require(offset <= bytes.size() && pixels <= bytes.size() - offset, "clipboard DIB pixels truncated");
    std::vector<BYTE> bmp(sizeof(BITMAPFILEHEADER) + offset + pixels);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfSize = (DWORD)bmp.size();
    file.bfOffBits = (DWORD)(sizeof(file) + offset);
    memcpy(bmp.data(), &file, sizeof(file));
    memcpy(bmp.data() + sizeof(file), bytes.data(), (size_t)(offset + pixels));
    if (header.biBitCount == 32 && header.biCompression == BI_RGB)
        for (uint64_t i = 3; i < pixels; i += 4)
            bmp[(size_t)(sizeof(file) + offset + i)] = 255;
    auto image = decodeImageBytes(bmp);
    // Classic DIB alpha is unspecified. Only a V5 explicit alpha mask carries transparency.
    bool alpha = false;
    if (header.biSize >= 56 && header.biCompression != BI_RGB) {
        DWORD mask{};
        memcpy(&mask, bytes.data() + 52, sizeof(mask));
        alpha = mask != 0;
    }
    if (!alpha) {
        Gdiplus::BitmapData data{};
        Gdiplus::Rect area(0, 0, image->GetWidth(), image->GetHeight());
        require(image->LockBits(&area, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                                PixelFormat32bppARGB, &data) == Gdiplus::Ok,
                "clipboard alpha unavailable");
        for (int y = 0; y < area.Height; ++y) {
            auto row = (BYTE *)data.Scan0 + (ptrdiff_t)y * data.Stride;
            for (int x = 0; x < area.Width; ++x)
                row[x * 4 + 3] = 255;
        }
        image->UnlockBits(&data);
    }
    return image;
}
Image readClipboardImage(HWND owner) {
    bool opened = false;
    for (int i = 0; i < 6; ++i) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        Sleep(20);
    }
    if (!opened)
        return {};
    struct Close {
        ~Close() {
            CloseClipboard();
        }
    } close;
    auto read = [&](UINT format) {
        std::vector<BYTE> bytes;
        if (!IsClipboardFormatAvailable(format))
            return bytes;
        auto memory = GetClipboardData(format);
        auto size = memory ? GlobalSize(memory) : 0;
        if (!size || size > 256ull * 1024 * 1024)
            return bytes;
        auto data = (const BYTE *)GlobalLock(memory);
        if (data) {
            bytes.assign(data, data + size);
            GlobalUnlock(memory);
        }
        return bytes;
    };
    auto png = read(RegisterClipboardFormatW(L"PNG"));
    if (!png.empty()) {
        try {
            return decodeImageBytes(png);
        } catch (const std::exception &) {
        }
    }
    for (auto format : {CF_DIBV5, CF_DIB}) {
        auto bytes = read(format);
        if (!bytes.empty()) {
            try {
                return decodeClipboardDib(bytes);
            } catch (const std::exception &) {
            }
        }
    }
    auto handle = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (handle) {
        BITMAP info{};
        if (GetObjectW(handle, sizeof(info), &info) &&
            validCanvasSize(info.bmWidth, std::abs(info.bmHeight))) {
            BITMAPINFO dib{};
            dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            dib.bmiHeader.biWidth = info.bmWidth;
            dib.bmiHeader.biHeight = -std::abs(info.bmHeight);
            dib.bmiHeader.biPlanes = 1;
            dib.bmiHeader.biBitCount = 32;
            dib.bmiHeader.biCompression = BI_RGB;
            std::vector<BYTE> bytes(sizeof(BITMAPINFOHEADER) +
                                    (size_t)info.bmWidth * std::abs(info.bmHeight) * 4);
            memcpy(bytes.data(), &dib.bmiHeader, sizeof(BITMAPINFOHEADER));
            HDC dc = CreateCompatibleDC(nullptr);
            auto rows = GetDIBits(dc, handle, 0, std::abs(info.bmHeight),
                                  bytes.data() + sizeof(BITMAPINFOHEADER), &dib, DIB_RGB_COLORS);
            DeleteDC(dc);
            if (rows == std::abs(info.bmHeight))
                return decodeClipboardDib(bytes);
        }
    }
    return {};
}
} // namespace lc
