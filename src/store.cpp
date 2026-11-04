#include "core.h"
#include <iomanip>
#include <sstream>
namespace lc {
namespace {
template <class T> void put(std::ostream &f, const T &v) {
    f.write((const char *)&v, sizeof(v));
}
template <class T> void get(std::istream &f, T &v) {
    f.read((char *)&v, sizeof(v));
    require((bool)f, "truncated metadata");
}
void str(std::ostream &f, const std::wstring &s) {
    uint32_t n = s.size();
    put(f, n);
    f.write((const char *)s.data(), n * sizeof(wchar_t));
}
std::wstring str(std::istream &f) {
    uint32_t n;
    get(f, n);
    require(n < 1000000, "invalid string length");
    std::wstring s(n, 0);
    f.read((char *)s.data(), n * sizeof(wchar_t));
    require((bool)f, "truncated string");
    return s;
}
std::wstring ini(const fs::path &p, const wchar_t *key, const wchar_t *fallback = L"") {
    std::vector<wchar_t> buf(32768);
    GetPrivateProfileStringW(L"LightCapture", key, fallback, buf.data(), buf.size(), p.c_str());
    return buf.data();
}
void logError(const fs::path &root, const std::wstring &file, const char *error) {
    std::ofstream f(root / L"recovery.log", std::ios::app);
    f << fs::path(file).filename().u8string() << ": " << error << "\n";
}
} // namespace
void Store::loadSettings() {
    auto p = root / L"settings.ini";
    settings.modifiers =
        GetPrivateProfileIntW(L"LightCapture", L"modifiers", MOD_CONTROL | MOD_SHIFT, p.c_str());
    settings.key = GetPrivateProfileIntW(L"LightCapture", L"key", 'X', p.c_str());
    if (settings.key == 0 || settings.key > 255 || settings.key == VK_F12) {
        settings.key = 'X';
        settings.modifiers = MOD_CONTROL | MOD_SHIFT;
    }
    settings.modifiers &= MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN;
    settings.autoCopy = false;
    settings.autoExport = false; // Migrate legacy automatic-export preferences to explicit Save.
    settings.cursor = GetPrivateProfileIntW(L"LightCapture", L"cursor", 0, p.c_str());
    settings.startup = GetPrivateProfileIntW(L"LightCapture", L"startup", 0, p.c_str());
    settings.history =
        std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"history", 100, p.c_str()), 100, 1000);
    settings.fixedW =
        std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"fixedW", 800, p.c_str()), 1, 32000);
    settings.fixedH =
        std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"fixedH", 600, p.c_str()), 1, 32000);
    settings.delay = std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"delay", 0, p.c_str()), 0, 30);
    settings.jpeg = std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"jpeg", 95, p.c_str()), 1, 100);
    settings.folder = ini(p, L"folder", (root / L"exports").c_str());
    settings.external = ini(p, L"external");
    settings.confirmRegion = GetPrivateProfileIntW(L"LightCapture", L"confirmRegion", 0, p.c_str());
    settings.color = GetPrivateProfileIntW(L"LightCapture", L"color", 0xffe5484d, p.c_str());
    settings.tool = std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"tool", 1, p.c_str()), 0,
                               (int)Tool::PixelSelect);
    if (settings.tool == (int)Tool::Reset)
        settings.tool = (int)Tool::Pen;
    settings.stroke = std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"stroke", 3, p.c_str()), 1, 80);
    settings.fontSize =
        std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"fontSize", 20, p.c_str()), 1, 200);
    settings.fontStyle = GetPrivateProfileIntW(L"LightCapture", L"fontStyle", 0, p.c_str()) & 15;
    settings.format = std::clamp((int)GetPrivateProfileIntW(L"LightCapture", L"format", 0, p.c_str()), 0, 4);
    settings.fill = GetPrivateProfileIntW(L"LightCapture", L"fill", 0, p.c_str());
    settings.font = ini(p, L"font", L"맑은 고딕");
    settings.pattern = ini(p, L"pattern", L"캡처_%Y-%m-%d_%H-%M-%S");
}
void Store::saveSettings() {
    auto p = root / L"settings.ini.partial";
    auto n = [&](const wchar_t *key, int value) {
        auto s = std::to_wstring(value);
        require(WritePrivateProfileStringW(L"LightCapture", key, s.c_str(), p.c_str()) != FALSE,
                "settings write failed");
    };
    n(L"modifiers", settings.modifiers);
    n(L"key", settings.key);
    n(L"autoCopy", 0);
    n(L"autoExport", 0);
    n(L"cursor", settings.cursor);
    n(L"startup", settings.startup);
    n(L"history", settings.history);
    n(L"fixedW", settings.fixedW);
    n(L"fixedH", settings.fixedH);
    n(L"delay", settings.delay);
    n(L"jpeg", settings.jpeg);
    n(L"confirmRegion", settings.confirmRegion);
    n(L"color", (int)settings.color);
    n(L"tool", settings.tool);
    n(L"stroke", settings.stroke);
    n(L"fontSize", settings.fontSize);
    n(L"fontStyle", settings.fontStyle);
    n(L"format", settings.format);
    n(L"fill", settings.fill);
    require(WritePrivateProfileStringW(L"LightCapture", L"font", settings.font.c_str(), p.c_str()),
            "font preference failed");
    require(WritePrivateProfileStringW(L"LightCapture", L"pattern", settings.pattern.c_str(), p.c_str()),
            "filename preference failed");
    require(WritePrivateProfileStringW(L"LightCapture", L"folder", settings.folder.c_str(), p.c_str()),
            "settings folder write failed");
    require(WritePrivateProfileStringW(L"LightCapture", L"external", settings.external.c_str(), p.c_str()),
            "settings editor write failed");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, p.c_str());
    atomicReplace(p, root / L"settings.ini");
}
void Store::save(Document &d) {
    require(d.loaded, "cannot save unloaded document");
    auto p = meta(d), tmp = p;
    tmp += L".partial";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        require((bool)f, "cannot open metadata");
        bool extended = std::any_of(d.commands.begin(), d.commands.end(), [](auto &c) {
            return c.tool == Tool::CanvasSize || c.tool == Tool::PasteImage || c.tool == Tool::CanvasPad;
        });
        uint32_t magic = extended ? 0x3344434c : 0x3244434c;
        put(f, magic);
        str(f, d.id);
        str(f, d.name);
        put(f, d.width);
        put(f, d.height);
        uint32_t count = d.commands.size(), cursor = d.cursor;
        put(f, count);
        put(f, cursor);
        std::vector<std::shared_ptr<const std::vector<BYTE>>> images;
        for (auto &c : d.commands) {
            int tool = (int)c.tool;
            put(f, tool);
            put(f, c.color);
            put(f, c.width);
            uint32_t fill = c.fill;
            put(f, fill);
            put(f, c.rect);
            put(f, c.fontSize);
            put(f, c.fontStyle);
            put(f, c.target);
            str(f, c.text);
            str(f, c.font);
            uint32_t n = c.points.size();
            put(f, n);
            f.write((char *)c.points.data(), n * sizeof(POINT));
            if (extended) {
                auto same = std::find(images.begin(), images.end(), c.imageData);
                if (c.imageData && same != images.end()) {
                    put(f, UINT32_MAX);
                    put(f, (uint32_t)(same - images.begin()));
                    images.push_back(c.imageData);
                    continue;
                }
                uint32_t bytes = c.imageData ? (uint32_t)c.imageData->size() : 0;
                put(f, bytes);
                if (bytes)
                    f.write((const char *)c.imageData->data(), bytes);
                images.push_back(c.imageData);
            }
        }
        f.flush();
        require((bool)f, "metadata write failed");
    }
    atomicReplace(tmp, p);
}
static Document readDocument(const fs::path &path) {
    Document d;
    std::ifstream f(path, std::ios::binary);
    uint32_t magic, count, cursor;
    get(f, magic);
    require(magic == 0x3144434c || magic == 0x3244434c || magic == 0x3344434c, "unknown metadata version");
    d.id = str(f);
    require(d.id == path.stem().wstring(), "metadata ID mismatch");
    d.name = str(f);
    get(f, d.width);
    get(f, d.height);
    require(d.width > 0 && d.height > 0 && d.width <= 100000 && d.height <= 100000, "invalid image size");
    get(f, count);
    get(f, cursor);
    require(count <= 10000 && cursor <= count, "invalid command count");
    d.cursor = cursor;
    LONG canvasW = d.width, canvasH = d.height;
    uint64_t embeddedBytes = 0;
    for (uint32_t i = 0; i < count; i++) {
        Command c;
        int t;
        uint32_t fill, n;
        get(f, t);
        require(t >= 0 && t <= (int)Tool::CanvasPad, "invalid tool");
        c.tool = (Tool)t;
        get(f, c.color);
        get(f, c.width);
        require(std::isfinite(c.width) && c.width > 0 && c.width <= 200, "invalid stroke");
        get(f, fill);
        c.fill = fill;
        get(f, c.rect);
        get(f, c.fontSize);
        if (magic != 0x3144434c) {
            get(f, c.fontStyle);
            require(c.fontStyle >= 0 && c.fontStyle <= 15, "invalid font style");
        }
        require(std::isfinite(c.fontSize) && c.fontSize >= 1 && c.fontSize <= 500, "invalid font size");
        get(f, c.target);
        c.text = str(f);
        c.font = str(f);
        get(f, n);
        require(n <= 1000000, "invalid point count");
        c.points.resize(n);
        f.read((char *)c.points.data(), n * sizeof(POINT));
        require((bool)f, "truncated points");
        if (magic == 0x3344434c) {
            uint32_t size;
            get(f, size);
            if (size == UINT32_MAX) {
                uint32_t index;
                get(f, index);
                require(index < d.commands.size() && d.commands[index].imageData,
                        "invalid embedded image reference");
                c.imageData = d.commands[index].imageData;
                size = 0;
            }
            embeddedBytes += size;
            require(size <= 64u * 1024 * 1024 && embeddedBytes <= 128ull * 1024 * 1024,
                    "embedded image bytes exceed limit");
            if (size) {
                auto bytes = std::make_shared<std::vector<BYTE>>(size);
                f.read((char *)bytes->data(), size);
                require((bool)f, "truncated embedded image");
                c.imageData = bytes;
            }
        }
        if (c.tool == Tool::Reset) {
            canvasW = d.width;
            canvasH = d.height;
        }
        if (c.tool == Tool::CanvasSize || c.tool == Tool::PasteImage) {
            auto extent = canvasExtent(c);
            bool origin = c.tool == Tool::PasteImage ? c.rect.left >= -32000 && c.rect.top >= -32000
                                                     : c.rect.left >= 0 && c.rect.top >= 0;
            require(origin && c.rect.right > 0 && c.rect.bottom > 0 &&
                        validCanvasSize(rw(c.rect), rh(c.rect)) &&
                        validCanvasSize(std::max(canvasW, extent.right), std::max(canvasH, extent.bottom)),
                    "invalid canvas extension or pasted image bounds");
            require(c.tool != Tool::PasteImage || (c.imageData && !c.imageData->empty()),
                    "pasted image bytes missing");
            canvasW = std::max(canvasW, extent.right);
            canvasH = std::max(canvasH, extent.bottom);
        }
        if (c.tool == Tool::CanvasPad) {
            require(c.rect.left == 0 && c.rect.top == 0 && validCanvasSize(rw(c.rect), rh(c.rect)) &&
                        c.points.size() == 3 && c.points[0].x >= 0 && c.points[0].y >= 0 &&
                        c.points[1].x > c.points[0].x && c.points[1].y > c.points[0].y &&
                        c.points[1].x <= canvasW && c.points[1].y <= canvasH && c.points[2].x >= 0 &&
                        c.points[2].y >= 0 && c.points[1].x - c.points[0].x + c.points[2].x <= rw(c.rect) &&
                        c.points[1].y - c.points[0].y + c.points[2].y <= rh(c.rect),
                    "invalid top left canvas padding");
            canvasW = rw(c.rect);
            canvasH = rh(c.rect);
        }
        if (c.tool == Tool::CanvasSize)
            require(c.points.size() == 2 && c.points[0].x == c.rect.left && c.points[0].y == c.rect.top &&
                        c.points[1].x > c.rect.left && c.points[1].y > c.rect.top &&
                        c.points[1].x <= c.rect.right && c.points[1].y <= c.rect.bottom,
                    "invalid canvas expansion source");
        if (c.tool == Tool::PixelSelect) {
            require(c.points.size() <= 2 && c.rect.left >= 0 && c.rect.top >= 0 &&
                        c.rect.right > c.rect.left && c.rect.bottom > c.rect.top && c.rect.right <= canvasW &&
                        c.rect.bottom <= canvasH,
                    "invalid pixel selection source");
            if (!c.points.empty()) {
                auto destination = pixelDestination(c);
                require(destination.left >= 0 && destination.top >= 0 &&
                            destination.right > destination.left && destination.bottom > destination.top &&
                            destination.right <= canvasW && destination.bottom <= canvasH,
                        "invalid pixel selection destination");
            }
        }
        d.commands.push_back(std::move(c));
    }
    return d;
}
void Store::load() {
    docs.clear();
    for (auto &e : fs::directory_iterator(root / L"captures")) {
        if (e.path().extension() != L".lcd")
            continue;
        try {
            Document d = readDocument(e.path());
            d.commands.clear();
            d.commands.shrink_to_fit();
            d.loaded = false;
            require(fs::exists(png(d)), "original image missing");
            docs.push_back(std::move(d));
        } catch (const std::exception &ex) {
            logError(root, e.path().wstring(), ex.what());
        }
    }
    std::sort(docs.begin(), docs.end(), [](auto &a, auto &b) { return a.id > b.id; });
    for (auto &e : fs::directory_iterator(root / L"captures")) {
        if (e.path().extension() != L".png")
            continue;
        auto metadata = e.path();
        metadata.replace_extension(L".lcd");
        if (fs::exists(metadata))
            continue;
        try {
            auto image = loadImage(e.path());
            Document d;
            d.id = e.path().stem().wstring();
            d.name = L"복구된 캡처 " + d.id;
            d.width = image->GetWidth();
            d.height = image->GetHeight();
            save(d);
            docs.push_back(std::move(d));
            logError(root, e.path().wstring(), "recovered orphan original");
        } catch (const std::exception &ex) {
            logError(root, e.path().wstring(), ex.what());
        }
    }
    std::sort(docs.begin(), docs.end(), [](auto &a, auto &b) { return a.id > b.id; });
}
Document Store::read(const Document &d) {
    return readDocument(meta(d));
}
void Store::ensure(Document &d) {
    if (!d.loaded)
        d = read(d);
}
void Store::releaseEdits() {
    for (auto &d : docs) {
        d.commands.clear();
        d.commands.shrink_to_fit();
        d.loaded = false;
    }
}
Document &Store::add(Bitmap &b, const std::wstring &name) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t id[100];
    swprintf_s(id, L"%04d%02d%02d_%02d%02d%02d_%03d_%08x", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond, t.wMilliseconds, GetTickCount());
    GUID guid{};
    require(CoCreateGuid(&guid) == S_OK, "capture ID failed");
    wchar_t suffix[40]{};
    StringFromGUID2(guid, suffix, 40);
    Document d;
    d.id = std::wstring(id) + suffix;
    d.name = (name.empty() ? L"캡처 " : name + L" ") + d.id;
    d.width = b.GetWidth();
    d.height = b.GetHeight();
    saveImage(b, png(d));
    try {
        save(d);
    } catch (...) {
        DeleteFileW(png(d).c_str());
        throw;
    }
    docs.insert(docs.begin(), std::move(d));
    while (docs.size() > (size_t)settings.history)
        remove(docs.size() - 1);
    return docs.front();
}
void Store::remove(size_t index) {
    if (index >= docs.size())
        return;
    auto d = docs[index];
    auto metadata = meta(d), image = png(d);
    auto removedMetadata = metadata.wstring() + L".deleted";
    auto removedImage = image.wstring() + L".deleted";
    // Unindex both files before removal. A failed original-file removal must never
    // leave a recoverable orphan PNG that silently reappears on the next launch.
    require(MoveFileExW(metadata.c_str(), removedMetadata.c_str(), MOVEFILE_REPLACE_EXISTING) ||
                GetLastError() == ERROR_FILE_NOT_FOUND,
            "metadata removal failed");
    if (!MoveFileExW(image.c_str(), removedImage.c_str(), MOVEFILE_REPLACE_EXISTING) &&
        GetLastError() != ERROR_FILE_NOT_FOUND) {
        MoveFileExW(removedMetadata.c_str(), metadata.c_str(), MOVEFILE_REPLACE_EXISTING);
        throw std::runtime_error("original image removal failed");
    }
    docs.erase(docs.begin() + index);
    DeleteFileW(removedMetadata.c_str());
    DeleteFileW(removedImage.c_str());
}
} // namespace lc
