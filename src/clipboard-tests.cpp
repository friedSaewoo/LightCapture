#include "clipboard.h"
#include <iostream>
using namespace lc;
static int passed = 0;
static void check(bool ok, const char *name) {
    require(ok, name);
    ++passed;
    std::cout << "PASS " << name << "\n";
}
static Color at(Bitmap &image, int x, int y) {
    Color pixel;
    require(image.GetPixel(x, y, &pixel) == Gdiplus::Ok, "test pixel unavailable");
    return pixel;
}
static Image decodePng(const std::vector<BYTE> &bytes) {
    IStream *stream = nullptr;
    require(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "PNG test stream unavailable");
    ULONG written = 0;
    HRESULT result = stream->Write(bytes.data(), (ULONG)bytes.size(), &written);
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    Image image;
    if (SUCCEEDED(result) && written == bytes.size()) {
        Bitmap decoded(stream);
        require(decoded.GetLastStatus() == Gdiplus::Ok, "clipboard PNG is invalid");
        image = clone(decoded);
    }
    stream->Release();
    require(image != nullptr, "PNG test decode failed");
    return image;
}
static std::vector<BYTE> clipboardBytes(UINT format) {
    auto handle = GetClipboardData(format);
    require(handle != nullptr, "clipboard format missing");
    auto data = (const BYTE *)GlobalLock(handle);
    require(data != nullptr, "clipboard format lock failed");
    std::vector<BYTE> result(data, data + GlobalSize(handle));
    GlobalUnlock(handle);
    return result;
}
static void verifyPixels(const ClipboardImage &formats, int w, int h) {
    auto info = (const BITMAPINFOHEADER *)formats.dib.data();
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    check(info->biSize == sizeof(*info) && info->biWidth == w && info->biHeight == h &&
              info->biBitCount == 24 && info->biCompression == BI_RGB && info->biSizeImage == stride * h &&
              formats.dib.size() >= sizeof(*info) + stride * h,
          "explicit DIB has standard 24-bit header and padded bottom-up rows");
    auto png = decodePng(formats.png);
    check(png->GetWidth() == (UINT)w && png->GetHeight() == (UINT)h, "PNG decodes to original dimensions");
    auto v5 = (const BITMAPV5HEADER *)formats.dibv5.data();
    check(v5->bV5Height == -h && v5->bV5SizeImage == (DWORD)((size_t)w * h * 4) &&
              v5->bV5Intent == LCS_GM_IMAGES && v5->bV5CSType == 0x73524742,
          "DIBV5 supplies image size, color space and rendering intent");
    bool same = true;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto dib = formats.dib.data() + sizeof(*info) + (size_t)(h - y - 1) * stride + x * 3;
            auto rgba = formats.dibv5.data() + sizeof(*v5) + ((size_t)y * w + x) * 4;
            auto p = at(*png, x, y);
            same &= dib[0] == p.GetB() && dib[1] == p.GetG() && dib[2] == p.GetR() && rgba[0] == p.GetB() &&
                    rgba[1] == p.GetG() && rgba[2] == p.GetR() && rgba[3] == 255 && p.GetA() == 255;
        }
    check(same, "PNG, explicit DIB and DIBV5 match every pixel with opaque alpha");
}
static void captureFixture() {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 13;
    info.bmiHeader.biHeight = -9;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    uint32_t *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
    require(dc && bitmap && pixels, "synthetic capture surface unavailable");
    auto old = SelectObject(dc, bitmap);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 13; ++x)
            pixels[y * 13 + x] = ((x * 17u) << 16) | ((y * 23u) << 8) | (x + y);
    // Same BitBlt -> owned ARGB path as screen capture, with synthetic RGB and zero alpha.
    auto captured = grabFromDC(dc, {2, 1, 11, 8});
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    bool same = true;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) {
            auto p = at(*captured, x, y);
            same &= p.GetA() == 255 && p.GetR() == (x + 2) * 17 && p.GetG() == (y + 1) * 23 &&
                    p.GetB() == x + y + 3;
        }
    check(same, "capture pixels survive GDI buffer destruction and zero source alpha");
    auto formats = makeClipboardImage(*captured);
    verifyPixels(formats, 9, 7);
}
int wmain(int argc, wchar_t **argv) {
    bool systemClipboard = argc > 1 && wcscmp(argv[1], L"--system-clipboard") == 0;
    bool isolated = argc > 1 && wcscmp(argv[1], L"--isolated") == 0;
    bool nativeClipboard = systemClipboard || isolated;
    // All native clipboard operations belong to a private, never-visible station.
    // Never fall back to the user's interactive clipboard if isolation fails.
    HWINSTA originalStation = GetProcessWindowStation();
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    // A named station may require administrator membership. Isolation is opt-in
    // and must never silently fall back to the interactive clipboard.
    auto stationName = L"LightCaptureClipboardTest-" + std::to_wstring(GetCurrentProcessId());
    HWINSTA station =
        isolated ? CreateWindowStationW(stationName.c_str(), CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, nullptr)
                 : nullptr;
    DWORD creationError = GetLastError();
    bool assigned = !isolated || (station && SetProcessWindowStation(station));
    DWORD assignmentError = GetLastError();
    if (!assigned) {
        std::cerr << "station creation: " << (station ? 0 : creationError)
                  << ", assignment: " << (station ? assignmentError : 0) << "\n";
        if (station)
            CloseWindowStation(station);
        std::cerr << "FAIL isolated clipboard station unavailable: " << GetLastError()
                  << "; user clipboard untouched\n";
        return 1;
    }
    HDESK desktop =
        isolated ? CreateDesktopW(L"ClipboardTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr) : nullptr;
    if (isolated && (!desktop || !SetThreadDesktop(desktop))) {
        if (desktop)
            CloseDesktop(desktop);
        SetProcessWindowStation(originalStation);
        CloseWindowStation(station);
        std::cerr << "FAIL isolated desktop unavailable; user clipboard untouched\n";
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    HWND owner = CreateWindowExW(0, L"STATIC", L"Clipboard test owner", WS_POPUP, 0, 0, 1, 1, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    try {
        check(owner && !IsWindowVisible(owner), "clipboard test uses only a hidden own window");
        captureFixture();
        Bitmap image(7, 5, PixelFormat32bppARGB);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x)
                image.SetPixel(x, y, Color(255, x * 31, y * 47, x + y));
        image.SetPixel(0, 0, Color(0, 255, 0, 0));
        image.SetPixel(1, 0, Color(128, 255, 0, 0));
        auto formats = makeClipboardImage(image);
        verifyPixels(formats, 7, 5);
        auto png = decodePng(formats.png);
        auto transparent = at(*png, 0, 0), half = at(*png, 1, 0);
        check(transparent.GetValue() == Color::White && half.GetR() == 255 && half.GetG() >= 126 &&
                  half.GetG() <= 128 && half.GetB() == half.GetG(),
              "transparent and partial-alpha pixels are composited onto white consistently");
        UINT pngFormat = nativeClipboard ? RegisterClipboardFormatW(L"PNG") : 0;
        for (int iteration = 0; nativeClipboard && iteration < 2; ++iteration) {
            check(publishClipboardImage(owner, formats), "real SetClipboardData publishes all formats");
            check(OpenClipboard(owner) != FALSE, "real clipboard can be reopened by consumer");
            ClipboardImage received{clipboardBytes(pngFormat), clipboardBytes(CF_DIB),
                                    clipboardBytes(CF_DIBV5)};
            bool payloads = received.png.size() >= formats.png.size() &&
                            std::equal(formats.png.begin(), formats.png.end(), received.png.begin()) &&
                            received.dib.size() >= formats.dib.size() &&
                            std::equal(formats.dib.begin(), formats.dib.end(), received.dib.begin());
            auto bitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
            BITMAP details{};
            bool bitmapOk = bitmap && GetObjectW(bitmap, sizeof(details), &details) && details.bmWidth == 7 &&
                            details.bmHeight == 5;
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = bitmap ? SelectObject(dc, bitmap) : nullptr;
            COLORREF pixel = bitmap ? GetPixel(dc, 6, 4) : CLR_INVALID;
            if (old)
                SelectObject(dc, old);
            DeleteDC(dc);
            CloseClipboard();
            check(payloads, "real GetClipboardData returns explicit PNG and DIB bytes intact");
            check(bitmapOk && pixel == RGB(186, 188, 10),
                  "Windows-synthesized CF_BITMAP consumer receives original color pixels");
            verifyPixels(received, 7, 5);
        }
        std::cout << "RESULT " << passed << " passed, 0 failed; "
                  << (systemClipboard ? "system clipboard contains synthetic test image"
                                      : "user clipboard untouched")
                  << "\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << "\n";
        if (nativeClipboard)
            CloseClipboard();
        result = 1;
    }
    if (owner)
        DestroyWindow(owner);
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    if (isolated) {
        SetProcessWindowStation(originalStation);
        SetThreadDesktop(originalDesktop);
        CloseDesktop(desktop);
        CloseWindowStation(station);
    }
    return result;
}
