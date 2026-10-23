#include "core.h"
#include <chrono>
#include <iostream>
using namespace lc;
static int passed = 0, failed = 0;
static void check(bool condition, const char *label) {
    if (condition) {
        passed++;
        std::cout << "PASS " << label << "\n";
    } else {
        failed++;
        std::cout << "FAIL " << label << "\n";
    }
}
static Color pixel(Bitmap &b, int x, int y) {
    Color c;
    b.GetPixel(x, y, &c);
    return c;
}
static bool red(Color c) {
    return c.GetR() > 180 && c.GetG() < 100 && c.GetB() < 100;
}
static Image fixture(int w, int h, int offset = 0) {
    auto b = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            b->SetPixel(x, y,
                        Color(255, (x * 13 + (y + offset) * 7) % 256, (x * 3 + (y + offset) * 17) % 256,
                              (x * 19 + (y + offset) * 3) % 256));
    return b;
}
int wmain(int argc, wchar_t **argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &si, nullptr);
    fs::path root = argc > 1 ? argv[1] : L"test-data";
    fs::create_directories(root);
    {
        try {
            auto norm = normalized({300, 250}, {10, 20});
            check(rw(norm) == 290 && rh(norm) == 230 && norm.left == 10, "reverse drag normalization");
            check(rw(bounded({-20, -20, 30, 40}, {0, 0, 100, 100})) == 30, "selection clipping");
            Bitmap base(160, 120, PixelFormat32bppARGB);
            {
                Graphics g(&base);
                g.Clear(Color::White);
            }
            Document d;
            d.width = 160;
            d.height = 120;
            Command box;
            box.tool = Tool::Rectangle;
            box.rect = {20, 20, 70, 70};
            box.color = 0xfff00000;
            box.fill = true;
            commit(d, box);
            auto rendered = output(base, d);
            check(red(pixel(*rendered, 40, 40)), "rectangle renders real pixel");
            check(pixel(*rendered, 2, 2).GetR() == 255, "annotation leaves outside intact");
            d.cursor = 0;
            rendered = output(base, d);
            check(pixel(*rendered, 40, 40).GetG() == 255, "undo restores original pixel");
            d.cursor = 1;
            rendered = output(base, d);
            check(red(pixel(*rendered, 40, 40)), "redo restores annotation");
            Command move = box;
            move.target = 0;
            move.rect = {90, 20, 140, 70};
            commit(d, move);
            rendered = output(base, d);
            check(pixel(*rendered, 40, 40).GetG() == 255 && red(pixel(*rendered, 110, 40)),
                  "object move hides old location");
            d.cursor--;
            rendered = output(base, d);
            check(red(pixel(*rendered, 40, 40)) && pixel(*rendered, 110, 40).GetG() == 255,
                  "object move undone in one step");
            d.cursor++;
            Command erase;
            erase.tool = Tool::Erase;
            erase.target = 1;
            commit(d, erase);
            rendered = output(base, d);
            check(pixel(*rendered, 110, 40).GetG() == 255, "eraser removes annotation");
            d.cursor--;
            check(d.commands.size() == 3, "undo retains redo history");
            Command c;
            c.tool = Tool::Crop;
            c.rect = {80, 10, 150, 90};
            commit(d, c);
            check(d.commands.size() == 3 && d.commands.back().tool == Tool::Crop,
                  "new edit truncates redo branch");
            rendered = output(base, d);
            check(rendered->GetWidth() == 70 && rendered->GetHeight() == 80, "crop output dimensions");
            check(red(pixel(*rendered, 30, 30)), "cropped annotation alignment");
            auto resetDoc = d;
            Command reset;
            reset.tool = Tool::Reset;
            commit(resetDoc, reset);
            auto resetImage = output(base, resetDoc);
            check(resetImage->GetWidth() == 160 && pixel(*resetImage, 40, 40).GetG() == 255,
                  "original reset restores dimensions and pixels");
            resetDoc.cursor--;
            resetImage = output(base, resetDoc);
            check(resetImage->GetWidth() == 70 && red(pixel(*resetImage, 30, 30)),
                  "original reset undo restores edited state");
            auto photo = fixture(160, 120);
            Document md;
            md.width = 160;
            md.height = 120;
            Command mos;
            mos.tool = Tool::Mosaic;
            mos.width = 12;
            mos.rect = {24, 24, 84, 84};
            commit(md, mos);
            auto masked = output(*photo, md);
            check(pixel(*masked, 25, 25).GetValue() == pixel(*masked, 30, 30).GetValue(),
                  "mosaic flatten block");
            check(pixel(*masked, 2, 2).GetValue() == pixel(*photo, 2, 2).GetValue(),
                  "mosaic preserves outside");
            Document tools;
            tools.width = 160;
            tools.height = 120;
            for (auto t : {Tool::Pen, Tool::Line, Tool::Arrow, Tool::Ellipse, Tool::Highlight, Tool::Text}) {
                Command cmd;
                cmd.tool = t;
                cmd.points = {{8, 12}, {44, 26}, {60, 40}};
                cmd.rect = {15, 15, 100, 70};
                cmd.text = L"한글 Text\n둘째 줄";
                commit(tools, cmd);
            }
            auto allTools = output(base, tools);
            saveImage(*allTools, root / L"editor-tools.png");
            check(fs::file_size(root / L"editor-tools.png") > 100, "all editor tools produce image");
            for (auto ext : {L".png", L".jpg", L".bmp", L".gif", L".tiff"}) {
                fs::path p = root / (std::wstring(L"roundtrip") + ext);
                saveImage(*rendered, p);
                auto back = loadImage(p);
                check(back->GetWidth() == 70 && back->GetHeight() == 80, "format save/decode dimensions");
            }
            auto p1 = fixture(128, 200, 0), p2 = fixture(128, 200, 80);
            check(overlap(*p1, *p2) == 120, "scroll overlap recovers exact shift");
            check(sameFrame(*p1, *p1) && !sameFrame(*p1, *p2), "scroll stop distinguishes changed frame");
            Store store(root / L"store");
            auto &added = store.add(base);
            added.commands = d.commands;
            added.cursor = d.cursor;
            store.save(added);
            std::wstring id = added.id;
            store.settings.key = 'Q';
            store.settings.modifiers = MOD_ALT | MOD_CONTROL;
            store.settings.fixedW = 901;
            store.settings.history = 100;
            store.settings.folder = (root / L"한글 exports").wstring();
            store.saveSettings();
            {
                Store restored(root / L"store");
                check(!restored.docs[0].loaded && restored.docs[0].commands.empty(),
                      "startup keeps edit payload on disk");
                restored.ensure(restored.docs[0]);
                check(restored.docs.size() == 1 && restored.docs[0].id == id, "history persisted");
                check(restored.docs[0].commands.size() == 3 && restored.docs[0].cursor == 3,
                      "edit commands persisted");
                check(restored.settings.key == 'Q' && restored.settings.fixedW == 901, "settings persisted");
                auto src = loadImage(restored.png(restored.docs[0]));
                auto out = output(*src, restored.docs[0]);
                check(red(pixel(*out, 30, 30)) && out->GetWidth() == 70, "restart rendered state matches");
            }
            {
                std::ofstream f(root / L"store" / L"captures" / L"broken.lcd", std::ios::binary);
                f << "bad";
            }
            Store recovery(root / L"store");
            check(recovery.docs.size() == 1 && fs::exists(root / L"store" / L"recovery.log"),
                  "corrupt metadata isolated and logged");
            auto external = root / L"keep-export.png";
            saveImage(base, external);
            recovery.remove(0);
            check(recovery.docs.empty() && fs::exists(external), "delete internal capture preserves export");
            {
                auto &orphan = recovery.add(base);
                DeleteFileW(recovery.meta(orphan).c_str());
            }
            Store recoveredOrphan(root / L"store");
            check(recoveredOrphan.docs.size() == 1 &&
                      fs::exists(recoveredOrphan.meta(recoveredOrphan.docs[0])),
                  "orphan original reconstructed after missing metadata");
            Store boundedHistory(root / L"bounded-history");
            Bitmap tiny(32, 24, PixelFormat32bppARGB);
            {
                Graphics g(&tiny);
                g.Clear(Color::Blue);
            }
            for (int i = 0; i < 101; i++)
                boundedHistory.add(tiny);
            check(boundedHistory.docs.size() == 100, "101 captures retain exactly 100 history items");
            std::vector<std::wstring> ids;
            for (auto &entry : boundedHistory.docs)
                ids.push_back(entry.id);
            std::sort(ids.begin(), ids.end());
            check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "rapid capture IDs are unique");
            Store historyReload(root / L"bounded-history");
            check(historyReload.docs.size() == 100, "100 history items survive restart");
            auto &invalid = historyReload.docs[0];
            auto metadata = historyReload.meta(invalid);
            {
                std::ifstream in(metadata, std::ios::binary);
                std::string bytes((std::istreambuf_iterator<char>(in)), {});
                bytes.resize(bytes.size() / 2);
                std::ofstream partial(fs::path(metadata.wstring() + L".partial"), std::ios::binary);
                partial << bytes;
            }
            Store partialIgnored(root / L"bounded-history");
            check(partialIgnored.docs.size() == 100,
                  "incomplete atomic temporary file does not replace good history");
            Bitmap transparent(30, 30, PixelFormat32bppARGB);
            {
                Graphics g(&transparent);
                g.Clear(Color::Transparent);
            }
            saveImage(transparent, root / L"alpha.png");
            auto alpha = loadImage(root / L"alpha.png");
            check(pixel(*alpha, 2, 2).GetA() == 0, "transparent PNG preserves alpha");
            saveImage(transparent, root / L"alpha.jpg");
            auto flattened = loadImage(root / L"alpha.jpg");
            check(pixel(*flattened, 2, 2).GetR() > 245 && pixel(*flattened, 2, 2).GetG() > 245,
                  "JPEG transparent background becomes white");
            Settings naming;
            naming.folder = root.wstring();
            naming.pattern = L"CON";
            check(exportPath(naming).filename().wstring() == L"캡처_CON.png",
                  "reserved Windows filename handled");
            naming.pattern = L"%q";
            check(exportPath(naming).filename().wstring() == L"캡처.png", "invalid filename token handled");
            naming.pattern = L"roundtrip";
            check(exportPath(naming).filename().wstring() == L"roundtrip_1.png",
                  "export naming does not overwrite existing file");
            Command boxed;
            boxed.tool = Tool::Text;
            boxed.fill = true;
            boxed.rect = {10, 10, 90, 95};
            boxed.fontSize = 18;
            boxed.color = 0xff2563eb;
            boxed.text = L"MMMM MMMM MMMM MMMM MMMM MMMM";
            auto textLayer = boxedText(boxed);
            bool upperInk = false, lowerInk = false, clearPixels = false;
            for (int y = 0; y < 85; ++y)
                for (int x = 0; x < 80; ++x) {
                    auto c = pixel(*textLayer, x, y);
                    if (c.GetA() > 50) {
                        if (y < 22)
                            upperInk = true;
                        if (y > 25)
                            lowerInk = true;
                    } else if (c.GetA() == 0)
                        clearPixels = true;
                }
            check(upperInk && lowerInk, "boxed text wraps within dragged width");
            check(clearPixels, "boxed text background remains transparent");
            auto textPhoto = fixture(120, 110);
            Document textDoc;
            textDoc.width = 120;
            textDoc.height = 110;
            commit(textDoc, boxed);
            auto composed = render(*textPhoto, textDoc);
            check(pixel(*composed, 100, 20).GetValue() == pixel(*textPhoto, 100, 20).GetValue() &&
                      pixel(*composed, 9, 40).GetValue() == pixel(*textPhoto, 9, 40).GetValue(),
                  "boxed text cannot paint outside its region");
            bool backgroundPreserved = false;
            for (int y = 0; y < 85 && !backgroundPreserved; ++y)
                for (int x = 0; x < 80; ++x)
                    if (pixel(*textLayer, x, y).GetA() == 0) {
                        backgroundPreserved = pixel(*composed, x + 10, y + 10).GetValue() ==
                                              pixel(*textPhoto, x + 10, y + 10).GetValue();
                        break;
                    }
            check(backgroundPreserved, "textPhoto pixels remain visible between text glyphs");
            auto legacyRoot = root / L"legacy-settings";
            fs::create_directories(legacyRoot);
            auto legacyIni = legacyRoot / L"settings.ini";
            WritePrivateProfileStringW(L"LightCapture", L"autoExport", L"1", legacyIni.c_str());
            Store legacy(legacyRoot);
            check(!legacy.settings.autoExport, "legacy automatic export setting is disabled on load");
            legacy.settings.autoExport = true;
            legacy.saveSettings();
            check(GetPrivateProfileIntW(L"LightCapture", L"autoExport", 1, legacyIni.c_str()) == 0,
                  "settings persistence cannot re-enable automatic export");
            // Resident/image operations do not retain full-frame buffers between iterations.
            PROCESS_MEMORY_COUNTERS_EX before{sizeof(before)}, after{sizeof(after)};
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&before, sizeof(before));
            for (int i = 0; i < 1000; i++) {
                auto tmp = render(base, tools);
                auto result = crop(*tmp, {0, 0, 80, 60});
            }
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&after, sizeof(after));
            check(after.PrivateUsage < before.PrivateUsage + 12 * 1024 * 1024,
                  "1000 render cycles bounded allocation");
            std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
        } catch (const std::exception &e) {
            std::cerr << "FATAL " << e.what() << "\n";
            failed++;
        }
    }
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return failed ? 1 : 0;
}
