#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdint>
#include <ctime>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gdiplus.h>
#include <memory>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace lc {
namespace fs = std::filesystem;
using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Graphics;
using Image = std::unique_ptr<Bitmap>;
inline const wchar_t *AppName = L"LightCapture";
inline const wchar_t *Version = L"0.1.15";
inline RECT normalized(POINT a, POINT b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
inline int rw(RECT r) {
    return r.right - r.left;
}
inline int rh(RECT r) {
    return r.bottom - r.top;
}
inline bool inside(RECT r, POINT p) {
    return PtInRect(&r, p) != FALSE;
}
inline RECT bounded(RECT r, RECT bounds) {
    RECT v{};
    IntersectRect(&v, &r, &bounds);
    return v;
}
inline void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline std::wstring errorText(DWORD code) {
    wchar_t *p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (wchar_t *)&p, 0, nullptr);
    std::wstring s = p ? p : L"알 수 없는 오류";
    LocalFree(p);
    return s;
}
inline void message(HWND h, const std::wstring &s, bool error = false) {
    MessageBoxW(h, s.c_str(), AppName, MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}
inline fs::path modulePath() {
    wchar_t p[32768]{};
    GetModuleFileNameW(nullptr, p, 32768);
    return p;
}
inline fs::path defaultData() {
    wchar_t *p = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p);
    fs::path r = fs::path(p ? p : L".") / L"LightCapture";
    CoTaskMemFree(p);
    return r;
}
inline void atomicReplace(const fs::path &temp, const fs::path &dest) {
    if (!MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD e = GetLastError();
        DeleteFileW(temp.c_str());
        throw std::runtime_error("atomic replace failed: " + std::to_string(e));
    }
}
inline CLSID encoder(const wchar_t *mime) {
    UINT n = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    std::vector<BYTE> bytes(size);
    auto list = (Gdiplus::ImageCodecInfo *)bytes.data();
    Gdiplus::GetImageEncoders(n, size, list);
    for (UINT i = 0; i < n; i++)
        if (wcscmp(list[i].MimeType, mime) == 0)
            return list[i].Clsid;
    throw std::runtime_error("image encoder unavailable");
}
inline void saveImage(Bitmap &b, const fs::path &p, int quality = 95) {
    auto ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    const wchar_t *mime = ext == L".jpg" || ext == L".jpeg"   ? L"image/jpeg"
                          : ext == L".bmp"                    ? L"image/bmp"
                          : ext == L".gif"                    ? L"image/gif"
                          : ext == L".tif" || ext == L".tiff" ? L"image/tiff"
                                                              : L"image/png";
    auto id = encoder(mime);
    Gdiplus::EncoderParameters ep{};
    ep.Count = 1;
    ULONG q = quality;
    ep.Parameter[0] = {Gdiplus::EncoderQuality, 1, Gdiplus::EncoderParameterValueTypeLong, &q};
    auto tmp = p;
    tmp += L".partial";
    Image flattened;
    Bitmap *source = &b;
    if (ext == L".jpg" || ext == L".jpeg") {
        flattened = std::make_unique<Bitmap>(b.GetWidth(), b.GetHeight(), PixelFormat24bppRGB);
        {
            Graphics g(flattened.get());
            g.Clear(Color::White);
            g.DrawImage(&b, 0, 0);
        }
        source = flattened.get();
    }
    if (source->Save(tmp.c_str(), &id, &ep) != Gdiplus::Ok) {
        DeleteFileW(tmp.c_str());
        throw std::runtime_error("image save failed");
    }
    atomicReplace(tmp, p);
}
inline Image loadImage(const fs::path &p) {
    Bitmap source(p.c_str());
    require(source.GetLastStatus() == Gdiplus::Ok, "image open failed");
    require((uint64_t)source.GetWidth() * source.GetHeight() * 4 <= 512ull * 1024 * 1024,
            "image exceeds 512 MiB decode limit");
    // GDI+ Clone can retain the decoder's file backing. Copy pixels into a separately
    // allocated bitmap so deleting a recent-list entry never depends on an open image handle.
    auto result = std::make_unique<Bitmap>(source.GetWidth(), source.GetHeight(), PixelFormat32bppARGB);
    require(result && result->GetLastStatus() == Gdiplus::Ok, "image decode failed");
    Graphics g(result.get());
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    require(g.DrawImage(&source, 0, 0, (int)source.GetWidth(), (int)source.GetHeight()) == Gdiplus::Ok,
            "image pixel copy failed");
    return result;
}
inline Image clone(Bitmap &b) {
    return Image(b.Clone(0, 0, b.GetWidth(), b.GetHeight(), PixelFormat32bppARGB));
}
inline Image grabFromDC(HDC source, RECT r, bool cursor = false) {
    require(rw(r) > 0 && rh(r) > 0, "empty capture");
    require((uint64_t)rw(r) * rh(r) * 4 <= 512ull * 1024 * 1024, "screen exceeds 512 MiB capture limit");
    require(source != nullptr, "capture source unavailable");
    struct CaptureBuffer {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ old = nullptr;
        ~CaptureBuffer() {
            if (old && old != HGDI_ERROR)
                SelectObject(dc, old);
            if (bitmap)
                DeleteObject(bitmap);
            if (dc)
                DeleteDC(dc);
        }
    } buffer;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rw(r);
    info.bmiHeader.biHeight = -rh(r);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    BYTE *pixels = nullptr;
    buffer.dc = CreateCompatibleDC(source);
    require(buffer.dc != nullptr, "capture device context allocation failed");
    buffer.bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
    require(buffer.bitmap && pixels, "capture pixel buffer allocation failed");
    buffer.old = SelectObject(buffer.dc, buffer.bitmap);
    require(buffer.old && buffer.old != HGDI_ERROR, "capture pixel buffer selection failed");
    require(BitBlt(buffer.dc, 0, 0, rw(r), rh(r), source, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE,
            "screen pixel transfer failed");
    if (cursor) {
        CURSORINFO ci{sizeof(ci)};
        if (GetCursorInfo(&ci) && ci.flags == CURSOR_SHOWING) {
            ICONINFO ii{};
            if (GetIconInfo(ci.hCursor, &ii)) {
                DrawIconEx(buffer.dc, ci.ptScreenPos.x - r.left - ii.xHotspot,
                           ci.ptScreenPos.y - r.top - ii.yHotspot, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
                DeleteObject(ii.hbmMask);
                if (ii.hbmColor)
                    DeleteObject(ii.hbmColor);
            }
        }
    }
    require(GdiFlush() != FALSE, "capture pixel flush failed");
    auto result = std::make_unique<Bitmap>(rw(r), rh(r), PixelFormat32bppARGB);
    require(result->GetLastStatus() == Gdiplus::Ok, "capture image allocation failed");
    Gdiplus::BitmapData data{};
    Gdiplus::Rect area(0, 0, rw(r), rh(r));
    require(result->LockBits(&area, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok,
            "capture image lock failed");
    for (int y = 0; y < rh(r); ++y) {
        auto row = (BYTE *)data.Scan0 + (ptrdiff_t)y * data.Stride;
        memcpy(row, pixels + (size_t)y * rw(r) * 4, (size_t)rw(r) * 4);
        for (int x = 0; x < rw(r); ++x)
            row[(size_t)x * 4 + 3] = 255;
    }
    result->UnlockBits(&data);
    return result;
}
inline Image grab(RECT r, bool cursor = false) {
    HDC source = GetDC(nullptr);
    require(source != nullptr, "screen device context unavailable");
    try {
        auto image = grabFromDC(source, r, cursor);
        ReleaseDC(nullptr, source);
        return image;
    } catch (...) {
        ReleaseDC(nullptr, source);
        throw;
    }
}
inline Image crop(Bitmap &b, RECT r) {
    r = bounded(r, {0, 0, (LONG)b.GetWidth(), (LONG)b.GetHeight()});
    require(rw(r) > 0 && rh(r) > 0, "empty crop");
    return Image(b.Clone(r.left, r.top, rw(r), rh(r), PixelFormat32bppARGB));
}
enum class Tool : int {
    Select,
    Pen,
    Line,
    Arrow,
    Rectangle,
    Ellipse,
    Highlight,
    Text,
    Mosaic,
    Crop,
    Erase,
    Reset,
    PixelSelect
};
enum class Mode : int { Rectangle, Freeform, Window, Unit, Fullscreen, Scroll, Fixed };
struct Command {
    Tool tool = Tool::Pen;
    uint32_t color = 0xffe5484d;
    float width = 3;
    bool fill = false;
    RECT rect{};
    std::vector<POINT> points;
    std::wstring text;
    std::wstring font = L"맑은 고딕";
    float fontSize = 20;
    int fontStyle = 0;
    int target = -1;
};
struct Document {
    std::wstring id, name;
    UINT width = 0, height = 0;
    std::vector<Command> commands;
    size_t cursor = 0;
    bool loaded = true;
};
inline RECT viewRect(const Document &d) {
    RECT r{0, 0, (LONG)d.width, (LONG)d.height};
    if (!d.loaded)
        return r;
    for (size_t i = 0; i < d.cursor; i++)
        if (d.commands[i].tool == Tool::Reset)
            r = {0, 0, (LONG)d.width, (LONG)d.height};
        else if (d.commands[i].tool == Tool::Crop)
            r = bounded(d.commands[i].rect, r);
    return r;
}
inline void commit(Document &d, Command c) {
    d.commands.resize(d.cursor);
    d.commands.push_back(std::move(c));
    d.cursor = d.commands.size();
}
inline std::vector<bool> erased(const Document &d) {
    std::vector<bool> hidden(d.cursor, false);
    for (size_t i = 0; i < d.cursor; i++) {
        if (d.commands[i].tool == Tool::Reset)
            for (size_t j = 0; j < i; j++)
                hidden[j] = true;
        if (d.commands[i].target >= 0 && (size_t)d.commands[i].target < i)
            hidden[d.commands[i].target] = true;
    }
    return hidden;
}
inline void mosaic(Bitmap &b, RECT r, int block) {
    r = bounded(r, {0, 0, (LONG)b.GetWidth(), (LONG)b.GetHeight()});
    if (rw(r) <= 0 || rh(r) <= 0)
        return;
    block = std::clamp(block, 4, 80);
    Gdiplus::BitmapData bd{};
    Gdiplus::Rect all(0, 0, b.GetWidth(), b.GetHeight());
    if (b.LockBits(&all, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB,
                   &bd) != Gdiplus::Ok)
        return;
    for (int y = r.top; y < r.bottom; y += block)
        for (int x = r.left; x < r.right; x += block) {
            uint64_t rr = 0, gg = 0, bb = 0, n = 0;
            for (int yy = y; yy < std::min(y + block, (int)r.bottom); yy++)
                for (int xx = x; xx < std::min(x + block, (int)r.right); xx++) {
                    auto p = ((uint32_t *)((BYTE *)bd.Scan0 + yy * bd.Stride))[xx];
                    rr += (p >> 16) & 255;
                    gg += (p >> 8) & 255;
                    bb += p & 255;
                    n++;
                }
            uint32_t c = 0xff000000 | ((rr / n) << 16) | ((gg / n) << 8) | (bb / n);
            for (int yy = y; yy < std::min(y + block, (int)r.bottom); yy++)
                for (int xx = x; xx < std::min(x + block, (int)r.right); xx++)
                    ((uint32_t *)((BYTE *)bd.Scan0 + yy * bd.Stride))[xx] = c;
        }
    b.UnlockBits(&bd);
}
inline Image boxedText(const Command &c) {
    int width = rw(c.rect), height = rh(c.rect);
    if (width < 1 || height < 1)
        return nullptr;
    require((uint64_t)width * height * 4 <= 512ull * 1024 * 1024, "text area is too large");
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    uint32_t *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&pixels, nullptr, 0);
    HFONT font = CreateFontW(
        -std::max(1, (int)std::lround(c.fontSize)), 0, 0, 0,
        (c.fontStyle & Gdiplus::FontStyleBold) ? FW_BOLD : FW_NORMAL,
        (c.fontStyle & Gdiplus::FontStyleItalic) != 0, (c.fontStyle & Gdiplus::FontStyleUnderline) != 0,
        (c.fontStyle & Gdiplus::FontStyleStrikeout) != 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, c.font.c_str());
    if (!dc || !bitmap || !font || !pixels) {
        if (font)
            DeleteObject(font);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        throw std::runtime_error("text rendering allocation failed");
    }
    auto oldBitmap = SelectObject(dc, bitmap), oldFont = SelectObject(dc, font);
    memset(pixels, 0, (size_t)width * height * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rect{0, 0, width, height};
    DrawTextW(dc, c.text.c_str(), (int)c.text.size(), &rect,
              DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_EXPANDTABS);
    GdiFlush();
    for (size_t i = 0, n = (size_t)width * height; i < n; ++i)
        pixels[i] = ((((pixels[i] & 255) * (c.color >> 24)) / 255) << 24) | (c.color & 0x00ffffff);
    Image result;
    try {
        result = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
        require(result && result->GetLastStatus() == Gdiplus::Ok, "text pixels unavailable");
        Gdiplus::BitmapData data{};
        Gdiplus::Rect area(0, 0, width, height);
        require(result->LockBits(&area, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) ==
                    Gdiplus::Ok,
                "text pixel destination unavailable");
        for (int y = 0; y < height; ++y)
            memcpy((BYTE *)data.Scan0 + (ptrdiff_t)y * data.Stride, pixels + (size_t)y * width,
                   (size_t)width * 4);
        result->UnlockBits(&data);
    } catch (...) {
        SelectObject(dc, oldFont);
        SelectObject(dc, oldBitmap);
        DeleteObject(font);
        DeleteObject(bitmap);
        DeleteDC(dc);
        throw;
    }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}
inline void drawCommand(Graphics &g, const Command &c) {
    Gdiplus::Pen pen(Color(c.color), c.width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush brush(Color(c.color));
    auto r = c.rect;
    float x = r.left, y = r.top, w = rw(r), h = rh(r);
    if (c.tool == Tool::Pen) {
        if (c.points.size() == 1)
            g.FillEllipse(&brush, (float)c.points[0].x - c.width / 2, (float)c.points[0].y - c.width / 2,
                          c.width, c.width);
        else if (c.points.size() > 1) {
            std::vector<Gdiplus::PointF> p;
            for (auto v : c.points)
                p.emplace_back(v.x, v.y);
            g.DrawLines(&pen, p.data(), p.size());
        }
    } else if (c.tool == Tool::Line || c.tool == Tool::Arrow) {
        if (c.points.size() < 2)
            return;
        if (c.tool == Tool::Arrow) {
            Gdiplus::AdjustableArrowCap cap(4, 5, TRUE);
            pen.SetCustomEndCap(&cap);
            g.DrawLine(&pen, (float)c.points[0].x, (float)c.points[0].y, (float)c.points.back().x,
                       (float)c.points.back().y);
        } else
            g.DrawLine(&pen, (float)c.points[0].x, (float)c.points[0].y, (float)c.points.back().x,
                       (float)c.points.back().y);
    } else if (c.tool == Tool::Rectangle) {
        if (c.fill)
            g.FillRectangle(&brush, x, y, w, h);
        g.DrawRectangle(&pen, x, y, w, h);
    } else if (c.tool == Tool::Ellipse) {
        if (c.fill)
            g.FillEllipse(&brush, x, y, w, h);
        g.DrawEllipse(&pen, x, y, w, h);
    } else if (c.tool == Tool::Highlight) {
        Gdiplus::SolidBrush high(Color(88, (c.color >> 16) & 255, (c.color >> 8) & 255, c.color & 255));
        g.FillRectangle(&high, x, y, w, h);
    } else if (c.tool == Tool::Text) {
        if (c.fill) {
            auto text = boxedText(c);
            if (text)
                g.DrawImage(text.get(), (int)r.left, (int)r.top);
            return;
        }
        Gdiplus::Font font(c.font.c_str(), c.fontSize, c.fontStyle, Gdiplus::UnitPixel);
        Gdiplus::StringFormat format;
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);
        g.DrawString(c.text.c_str(), -1, &font, Gdiplus::PointF(x, y), &format, &brush);
    }
}
inline Image render(Bitmap &original, const Document &d) {
    auto out = clone(original);
    auto skip = erased(d);
    for (size_t i = 0; i < d.cursor; i++) {
        auto &c = d.commands[i];
        if (skip[i] || c.tool == Tool::Crop || c.tool == Tool::Erase || c.tool == Tool::Reset)
            continue;
        if (c.tool == Tool::PixelSelect && !c.points.empty()) {
            auto source = bounded(c.rect, {0, 0, (LONG)out->GetWidth(), (LONG)out->GetHeight()});
            if (rw(source) <= 0 || rh(source) <= 0)
                continue;
            auto patch = crop(*out, source); // Copy before clearing, including overlapping moves.
            Graphics g(out.get());
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            Gdiplus::SolidBrush white(Color::White);
            require(g.FillRectangle(&white, (int)source.left, (int)source.top, rw(source), rh(source)) ==
                            Gdiplus::Ok &&
                        g.DrawImage(patch.get(), (int)c.points[0].x, (int)c.points[0].y,
                                    (int)patch->GetWidth(), (int)patch->GetHeight()) == Gdiplus::Ok,
                    "selected pixel move failed");
        } else if (c.tool == Tool::Mosaic)
            mosaic(*out, c.rect, (int)c.width);
        else {
            Graphics g(out.get());
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            drawCommand(g, c);
        }
    }
    return out;
}
inline Image output(Bitmap &original, const Document &d) {
    auto all = render(original, d);
    return crop(*all, viewRect(d));
}

// Overlap is expressed in rows common to the previous bottom and next top.
inline int overlap(Bitmap &a, Bitmap &b) {
    if (a.GetWidth() != b.GetWidth() || a.GetHeight() != b.GetHeight())
        return 0;
    int w = a.GetWidth(), h = a.GetHeight();
    if (h < 32)
        return 0;
    Gdiplus::BitmapData da{}, db{};
    Gdiplus::Rect r(0, 0, w, h);
    if (a.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &da) != Gdiplus::Ok)
        return 0;
    if (b.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &db) != Gdiplus::Ok) {
        a.UnlockBits(&da);
        return 0;
    }
    double best = 1e9;
    int found = 0;
    for (int ov = 16; ov < h; ov++) {
        double diff = 0;
        int n = 0;
        for (int row = 0; row < ov; row += std::max(1, ov / 24))
            for (int x = 8; x < w - 8; x += std::max(1, w / 80)) {
                auto ca = ((uint32_t *)((BYTE *)da.Scan0 + (h - ov + row) * da.Stride))[x],
                     cb = ((uint32_t *)((BYTE *)db.Scan0 + row * db.Stride))[x];
                diff += std::abs(int((ca >> 16) & 255) - int((cb >> 16) & 255)) +
                        std::abs(int((ca >> 8) & 255) - int((cb >> 8) & 255)) +
                        std::abs(int(ca & 255) - int(cb & 255));
                n++;
            }
        double score = n ? diff / (n * 3) : 1e9;
        if (score < best) {
            best = score;
            found = ov;
        }
    }
    a.UnlockBits(&da);
    b.UnlockBits(&db);
    return best < 7 ? found : 0;
}
inline bool sameFrame(Bitmap &a, Bitmap &b) {
    if (a.GetWidth() != b.GetWidth() || a.GetHeight() != b.GetHeight())
        return false;
    uint64_t diff = 0, n = 0;
    for (UINT y = 0; y < a.GetHeight(); y += 17)
        for (UINT x = 0; x < a.GetWidth(); x += 19) {
            Color ca, cb;
            a.GetPixel(x, y, &ca);
            b.GetPixel(x, y, &cb);
            diff += std::abs(int(ca.GetR()) - int(cb.GetR())) + std::abs(int(ca.GetG()) - int(cb.GetG())) +
                    std::abs(int(ca.GetB()) - int(cb.GetB()));
            n++;
        }
    return n && diff < 2 * n;
}
struct Settings {
    UINT modifiers = MOD_CONTROL | MOD_SHIFT, key = 'X';
    bool autoCopy = false, autoExport = false, cursor = false, startup = false;
    bool confirmRegion = false;
    int history = 100, fixedW = 800, fixedH = 600, delay = 0, jpeg = 95;
    std::wstring folder, external;
    uint32_t color = 0xffe5484d;
    int tool = 1, stroke = 3, fontSize = 20, format = 0, fontStyle = 0;
    bool fill = false;
    std::wstring font = L"맑은 고딕", pattern = L"캡처_%Y-%m-%d_%H-%M-%S";
};
inline const wchar_t *imageExtension(int format) {
    static const wchar_t *ext[] = {L".png", L".jpg", L".bmp", L".gif", L".tiff"};
    return ext[std::clamp(format, 0, 4)];
}
inline fs::path exportPath(const Settings &settings) {
    time_t now = time(nullptr);
    tm local{};
    localtime_s(&local, &now);
    wchar_t name[256]{};
    bool valid = true;
    for (size_t i = 0; i < settings.pattern.size(); i++)
        if (settings.pattern[i] == '%') {
            if (++i >= settings.pattern.size() || !wcschr(L"YmdHMS%", settings.pattern[i])) {
                valid = false;
                break;
            }
        }
    if (!valid || !wcsftime(name, 256, settings.pattern.c_str(), &local))
        wcscpy_s(name, L"캡처");
    std::wstring safe = name;
    for (auto &c : safe)
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c))
            c = '_';
    while (!safe.empty() && (safe.back() == '.' || safe.back() == ' '))
        safe.pop_back();
    if (safe.empty())
        safe = L"캡처";
    auto upper = safe;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
    if (upper == L"CON" || upper == L"PRN" || upper == L"AUX" || upper == L"NUL" ||
        (upper.size() == 4 && (upper.substr(0, 3) == L"COM" || upper.substr(0, 3) == L"LPT") &&
         upper[3] >= '1' && upper[3] <= '9'))
        safe = L"캡처_" + safe;
    auto folder = fs::path(settings.folder);
    auto candidate = folder / (safe + imageExtension(settings.format));
    for (int i = 1; fs::exists(candidate); i++)
        candidate = folder / (safe + L"_" + std::to_wstring(i) + imageExtension(settings.format));
    return candidate;
}
struct Store {
    fs::path root;
    Settings settings;
    std::vector<Document> docs;
    explicit Store(fs::path r) : root(std::move(r)) {
        fs::create_directories(root / L"captures");
        loadSettings();
        load();
    }
    fs::path png(const Document &d) const {
        return root / L"captures" / (d.id + L".png");
    }
    fs::path meta(const Document &d) const {
        return root / L"captures" / (d.id + L".lcd");
    }
    void loadSettings();
    void saveSettings();
    void save(Document &d);
    void load();
    Document &add(Bitmap &b);
    void remove(size_t index);
    Document read(const Document &d);
    void ensure(Document &d);
    void releaseEdits();
};
} // namespace lc
