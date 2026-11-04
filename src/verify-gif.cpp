#include "app.h"
#include <psapi.h>
namespace lc {
static GifFrame gifFixture(int w, int h, int index, ULONGLONG time, bool gradient = false) {
    GifFrame frame;
    frame.time = time;
    frame.pixels.resize((size_t)w * h, 0xfffafafa);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t c = 0xfffafafa;
            if (gradient)
                c = 0xff000000 | ((x * 255 / w) << 16) | ((y * 255 / h) << 8) | ((x + y) * 255 / (w + h));
            else if (y < h / 6)
                c = 0xff1761d7;
            else if (x > w / 12 && x < w * 3 / 4 && y % 19 < 2)
                c = 0xff172033;
            if (x >= (index * 7) % (w - 32) && x < (index * 7) % (w - 32) + 24 && y >= h / 2 &&
                y < h / 2 + 20)
                c = 0xffe5484d;
            frame.pixels[(size_t)y * w + x] = c;
        }
    return frame;
}
static void gifOwnPreview(HWND window, const fs::path &path) {
    RECT r{}, outer{};
    GetClientRect(window, &r);
    GetWindowRect(window, &outer);
    POINT origin{};
    ClientToScreen(window, &origin);
    HDC dc = GetDC(window), memory = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, rw(r), rh(r));
    auto old = SelectObject(memory, bitmap);
    FillRect(memory, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetViewportOrgEx(memory, outer.left - origin.x, outer.top - origin.y, nullptr);
    SendMessageW(window, WM_PRINT, (WPARAM)memory, PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    SelectObject(memory, old);
    Image image(Bitmap::FromHBITMAP(bitmap, nullptr));
    saveImage(*image, path);
    image.reset();
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, dc);
}
void App::verifyGif() {
    int checks = 0;
    auto check = [&](bool ok, const char *name) {
        require(ok, name);
        ++checks;
    };
    check(GetDlgItem(main, ID_GIF) != nullptr, "GIF toolbar button missing");
    for (int profile = 0; profile < 3; ++profile) {
        auto small = gifSize(320, 180, profile), wide = gifSize(3840, 2160, profile),
             tall = gifSize(1080, 1920, profile);
        check(small.cx == 320 && small.cy == 180 && wide.cx == GifProfiles[profile].edge &&
                  std::abs(wide.cy * 16 - wide.cx * 9) <= 16 && tall.cy == GifProfiles[profile].edge,
              "GIF profile upscaled or lost aspect ratio/size cap");
    }
    auto animation = store.root / L"gif-delta-test.gif";
    GifStats deltaStats;
    {
        GifWriter writer(animation, 320, 180);
        for (int i = 0; i < 36; ++i)
            writer.append(gifFixture(320, 180, i, 1000 + (ULONGLONG)i * 1000 / 12));
        writer.finish(4000);
        deltaStats = writer.stats;
        check(writer.stats.frames == 36 && writer.stats.duration == 3000,
              "GIF duration or frame count incorrect");
        check(writer.stats.pixels < 320ull * 180 * 36 / 4,
              "GIF delta compression did not reduce encoded pixels");
    }
    GifPreview preview(animation);
    check(preview.width == 320 && preview.height == 180 && preview.count == 36,
          "GIF preview dimensions/count wrong");
    bool exact = true;
    ULONGLONG duration = 0;
    for (int i = 0; i < 36; ++i) {
        auto expected = gifFixture(320, 180, i, 0);
        for (size_t j = 0; j < expected.pixels.size(); ++j)
            exact &= (preview.pixels[j] & 0xffffff) == (expected.pixels[j] & 0xffffff);
        duration += preview.delay;
        if (i != 35)
            preview.advance();
    }
    check(exact, "GIF delta composition lost colors, left cursor trails, or disposed frames incorrectly");
    check(duration == 3000, "GIF centisecond timing drifted from real timestamps");
    preview.advance();
    check(preview.next == 1 && preview.pixels == gifFixture(320, 180, 0, 0).pixels,
          "GIF loop failed to reset canvas");
    auto stationary = store.root / L"gif-static-test.gif";
    {
        GifWriter writer(stationary, 320, 180);
        for (int i = 0; i < 120; ++i)
            writer.append(gifFixture(320, 180, 0, 1000 + i * 100));
        writer.finish(13000);
        check(writer.stats.frames == 1 && writer.stats.duration == 12000,
              "stationary GIF samples were not merged");
        check(writer.stats.bytes < 12000, "stationary GIF is unexpectedly large");
    }
    auto quality = store.root / L"gif-gradient-test.gif";
    {
        GifWriter writer(quality, 640, 360);
        for (int i = 0; i < 12; ++i)
            writer.append(gifFixture(640, 360, i, 1000 + i * 100));
        writer.finish(2200);
    }
    GifPreview gradient(quality);
    auto expectedGradient = gifFixture(640, 360, 0, 0, true);
    // A separate gradient recording exercises adaptive 256-color quantization.
    auto gradientFile = store.root / L"gif-color-test.gif";
    {
        GifWriter writer(gradientFile, 640, 360);
        writer.append(expectedGradient);
        writer.finish(100);
    }
    GifPreview colors(gradientFile);
    double error = 0;
    for (size_t i = 0; i < colors.pixels.size(); ++i)
        for (int shift : {0, 8, 16})
            error += std::abs((int)((colors.pixels[i] >> shift) & 255) -
                              (int)((expectedGradient.pixels[i] >> shift) & 255));
    error /= colors.pixels.size() * 3;
    check(error < 10, "GIF adaptive palette color error too large");
    auto sessionFile = store.root / L"gif-queue-test.gif";
    UINT queuePeak = 0, dropped = 0;
    {
        GifSession session(sessionFile, 320, 180);
        for (int i = 0; i < 100; ++i)
            session.submit(gifFixture(320, 180, i, 1000 + i * 20));
        session.finish(3100);
        auto deadline = GetTickCount64() + 10000;
        while (!session.done && GetTickCount64() < deadline)
            Sleep(5);
        check(session.done && session.error.empty() && session.stats.frames > 0,
              "GIF background encoder failed");
        queuePeak = session.queuePeak;
        dropped = session.dropped;
        check(queuePeak <= 3, "GIF worker queue exceeded memory bound");
        GifPreview queued(session.file);
        check(queued.width == 320 && session.stats.duration == 2100,
              "dropped GIF samples changed playback duration");
    }
    check(!fs::exists(sessionFile), "GIF session leaked temporary file");
    auto cancelledFile = store.root / L"gif-cancel-test.gif";
    {
        GifSession session(cancelledFile, 320, 180);
        session.submit(gifFixture(320, 180, 0, 1000));
        session.finish(1100, true);
    }
    check(!fs::exists(cancelledFile), "cancelled GIF leaked temporary file");
    auto badFile = store.root / L"missing-gif-directory" / L"failure.gif";
    {
        GifSession session(badFile, 320, 180);
        session.finish(1000);
        auto deadline = GetTickCount64() + 5000;
        while (!session.done && GetTickCount64() < deadline)
            Sleep(5);
        check(session.done && !session.error.empty(), "GIF write failure did not reach UI");
    }
    // Connect the existing region-selection completion handler to a synthetic recording source.
    previouslyVisible = false;
    busy = true;
    gifRequested = true;
    mode = Mode::Rectangle;
    virtualRect = {-100, 50, 220, 230};
    selection = {0, 0, 320, 180};
    snapshot = std::make_unique<Bitmap>(320, 180, PixelFormat32bppARGB);
    auto docsBefore = store.docs.size();
    finishCapture();
    check(gifState == GifState::Ready && gifWindow && !snapshot && !overlay && busy && gifRect.left == -100 &&
              gifRect.top == 50 && store.docs.size() == docsBefore,
          "GIF region selection created a still capture or lost virtual-screen offset");
    gifOwnPreview(gifWindow, store.root / L"gif-ready-preview.png");
    gifFrameProvider = [](RECT, SIZE size, ULONGLONG time) {
        return gifFixture(size.cx, size.cy, (int)((time - 4000) / 100), time);
    };
    startGif(1000);
    tickGif(3999);
    check(gifState == GifState::Countdown && !gifSession, "GIF countdown captured early");
    tickGif(4000);
    check(gifState == GifState::Recording && gifSession && !gifBuffer,
          "test GIF used a real desktop capture buffer");
    for (ULONGLONG time = 4100; time <= 4900; time += 100) {
        tickGif(time);
        Sleep(2);
    }
    stopGif(false, 5000);
    auto deadline = GetTickCount64() + 10000;
    while (gifState == GifState::Finishing && GetTickCount64() < deadline) {
        Sleep(5);
        tickGif(5000);
    }
    check(gifState == GifState::Preview && gifPreview && gifSession && gifSession->stats.duration == 1000,
          "GIF stop did not create playable preview");
    auto temporary = gifSession->file;
    savePathPicker = [](const std::wstring &) { return std::wstring{}; };
    saveGif();
    check(gifState == GifState::Preview && fs::exists(temporary), "cancelled GIF save discarded recording");
    auto saved = store.root / L"gif-saved-test.gif";
    savePathPicker = [&](const std::wstring &) { return saved.wstring(); };
    saveGif();
    check(fs::file_size(saved) == gifSession->stats.bytes, "saved GIF differs from finalized animation");
    GifPreview savedPreview(saved);
    check(savedPreview.count == gifPreview->count, "GIF save lost animation frames");
    savePathPicker = [&](const std::wstring &) { return badFile.wstring(); };
    saveGif();
    check(gifState == GifState::Preview && gifPreview && fs::exists(temporary) && fs::exists(saved),
          "failed GIF save discarded preview or existing saved file");
    gifNotice.clear();
    layoutGif();
    gifOwnPreview(gifWindow, store.root / L"gif-preview.png");
    SetWindowPos(gifWindow, nullptr, 0, 0, 640, 440, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    gifOwnPreview(gifWindow, store.root / L"gif-preview-small.png");
    closeGif(false);
    check(gifState == GifState::Closed && !busy && !gifWindow && !gifSession && !gifPreview &&
              !fs::exists(temporary),
          "GIF close retained resources or temporary data");
    prepareGif({0, 0, 320, 180});
    startGif(1000);
    stopGif(true, 2000);
    check(gifState == GifState::Closed && !gifSession && !busy, "GIF countdown cancel started encoding");
    prepareGif({0, 0, 320, 180});
    startGif(1000);
    tickGif(4000);
    Sleep(5);
    stopGif(true, 4100);
    deadline = GetTickCount64() + 5000;
    while (gifState == GifState::Finishing && GetTickCount64() < deadline) {
        Sleep(5);
        tickGif(4100);
    }
    check(gifState == GifState::Closed && !busy, "GIF recording cancel retained busy state");
    prepareGif({0, 0, 320, 180});
    startGif(1000);
    tickGif(4000);
    Sleep(5);
    tickGif(4000 + GifDurationLimit);
    deadline = GetTickCount64() + 5000;
    while (gifState == GifState::Finishing && GetTickCount64() < deadline) {
        Sleep(5);
        tickGif(4000 + GifDurationLimit);
    }
    check(gifState == GifState::Preview && gifSession->stats.duration == GifDurationLimit &&
              !gifNotice.empty(),
          "GIF duration limit failed to stop and preserve recording");
    closeGif(false);
    prepareGif({0, 0, 320, 180});
    startGif(1000);
    tickGif(4000);
    Sleep(5);
    gifSession->limit = true;
    tickGif(4100);
    deadline = GetTickCount64() + 5000;
    while (gifState == GifState::Finishing && GetTickCount64() < deadline) {
        Sleep(5);
        tickGif(4100);
    }
    check(gifState == GifState::Preview && !gifNotice.empty(), "GIF file size limit did not create preview");
    closeGif(false);
    prepareGif({0, 0, 320, 180});
    auto time = GetTickCount64();
    startGif(time - 3000);
    tickGif(time);
    Sleep(5);
    mainProc(main, WM_HOTKEY, 67, 0);
    deadline = GetTickCount64() + 5000;
    while (gifState == GifState::Finishing && GetTickCount64() < deadline) {
        Sleep(5);
        tickGif();
    }
    check(gifState == GifState::Preview, "GIF F8 stop hotkey routing failed");
    auto oldArea = gifRect;
    gifProc(gifWindow, WM_COMMAND, 17, 0);
    check(gifState == GifState::Ready && EqualRect(&gifRect, &oldArea),
          "GIF re-record failed to preserve area");
    time = GetTickCount64();
    startGif(time - 3000);
    tickGif(time);
    Sleep(5);
    mainProc(main, WM_HOTKEY, hotId, 0);
    deadline = GetTickCount64() + 5000;
    while (gifState == GifState::Finishing && GetTickCount64() < deadline) {
        Sleep(5);
        tickGif();
    }
    check(gifState == GifState::Preview, "GIF configured capture hotkey stop routing failed");
    closeGif(false);
    prepareGif({0, 0, 320, 180});
    startGif(1000);
    mainProc(main, WM_HOTKEY, 68, 0);
    check(gifState == GifState::Closed && !busy, "GIF Escape hotkey cancel routing failed");
    gifFrameProvider = {};
    savePathPicker = {};
    std::ofstream report(store.root / L"gif-verification.json");
    report << "{\n  \"checks_passed\": " << checks << ",\n  \"delta_frames\": " << deltaStats.frames
           << ",\n  \"delta_bytes\": " << deltaStats.bytes
           << ",\n  \"delta_encoded_pixels\": " << deltaStats.pixels
           << ",\n  \"gradient_mean_absolute_channel_error\": " << error
           << ",\n  \"queue_peak\": " << queuePeak << ",\n  \"overload_dropped_samples\": " << dropped
           << ",\n  \"timing_delta_composition_loop_static_merge\": true,\n"
              "  \"region_countdown_record_stop_preview_save_cancel\": true,\n"
              "  \"profiles_no_upscale_aspect_size_limits\": true,\n"
              "  \"bounded_queue_streaming_and_cleanup\": true,\n"
              "  \"duration_size_limits_stop_hotkeys_rerecord_save_failure\": true,\n"
              "  \"computer_use\": false,\n  \"desktop_capture_performed\": false,\n"
              "  \"system_clipboard_modified\": false\n}\n";
}
} // namespace lc
