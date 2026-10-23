#include "app.h"
#include <imm.h>
namespace lc {
static std::wstring inputText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring text(n + 1, L'\0');
    GetWindowTextW(h, text.data(), n + 1);
    text.resize(n);
    return text;
}
RECT App::textScreenRect() {
    auto b = imageBounds(), v = viewRect(*doc()), r = textDraft.rect;
    return {b.left + LONG(std::lround((r.left - v.left) * zoom)),
            b.top + LONG(std::lround((r.top - v.top) * zoom)),
            b.left + LONG(std::lround((r.right - v.left) * zoom)),
            b.top + LONG(std::lround((r.bottom - v.top) * zoom))};
}
void App::startText(Command c) {
    finishText();
    if (!doc() || !original)
        return;
    auto v = viewRect(*doc());
    c.tool = Tool::Text;
    c.fill = true;
    c.rect = bounded(c.rect, v);
    int minW = std::min(rw(v), std::max(24, (int)c.fontSize * 2));
    int minH = std::min(rh(v), std::max(24, (int)std::ceil(c.fontSize * 1.6f)));
    if (rw(c.rect) < minW)
        c.rect.right = std::min<LONG>(v.right, c.rect.left + std::max(minW, 240));
    if (rh(c.rect) < minH)
        c.rect.bottom = std::min<LONG>(v.bottom, c.rect.top + std::max(minH, 72));
    if (rw(c.rect) < minW)
        c.rect.left = std::max<LONG>(v.left, c.rect.right - minW);
    if (rh(c.rect) < minH)
        c.rect.top = std::max<LONG>(v.top, c.rect.bottom - minH);
    if (rw(c.rect) < 1 || rh(c.rect) < 1)
        return;
    textDraft = std::move(c);
    auto visible = *doc();
    if (textDraft.target >= 0) {
        Command erase;
        erase.tool = Tool::Erase;
        erase.target = textDraft.target;
        commit(visible, erase);
    }
    textBackdrop = render(*original, visible);
    auto rect = textScreenRect();
    textInput = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_NOHIDESEL | ES_AUTOVSCROLL,
        rect.left, rect.top, rw(rect), rh(rect), canvas, (HMENU)ID_TEXT_INPUT, instance, nullptr);
    require(textInput != nullptr, "inline text editor creation failed");
    SetWindowSubclass(textInput, textProc, 1, (DWORD_PTR)this);
    SendMessageW(textInput, EM_SETLIMITTEXT, 500000, 0);
    updateTextStyle();
    SetWindowTextW(textInput, textDraft.text.c_str());
    SendMessageW(textInput, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    if (!testMode)
        SetFocus(textInput);
    selectedObject = -1;
    InvalidateRect(canvas, nullptr, FALSE);
    statusText(L"이미지 위에서 입력 중 · Enter 줄바꿈 · Ctrl+Enter 또는 바깥 클릭으로 확정 · Esc 취소");
}
void App::updateTextStyle() {
    if (!textInput)
        return;
    auto old = inputFont;
    inputFont = CreateFontW(-std::max(1, (int)std::lround(textDraft.fontSize * zoom)), 0, 0, 0,
                            (textDraft.fontStyle & Gdiplus::FontStyleBold) ? FW_BOLD : FW_NORMAL,
                            (textDraft.fontStyle & Gdiplus::FontStyleItalic) != 0,
                            (textDraft.fontStyle & Gdiplus::FontStyleUnderline) != 0,
                            (textDraft.fontStyle & Gdiplus::FontStyleStrikeout) != 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                            textDraft.font.c_str());
    require(inputFont != nullptr, "inline text font creation failed");
    SendMessageW(textInput, WM_SETFONT, (WPARAM)inputFont, TRUE);
    if (old)
        DeleteObject(old);
    positionText();
}
void App::positionText() {
    if (!textInput || !doc() || positioningText)
        return;
    positioningText = true;
    auto r = textScreenRect();
    HDC dc = GetDC(canvas), mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1, rw(r)), std::max(1, rh(r)));
    auto old = SelectObject(mem, bitmap);
    {
        Graphics g(mem);
        g.Clear(Color(255, 231, 234, 239));
        auto b = imageBounds(), v = viewRect(*doc());
        g.SetInterpolationMode(zoom >= 1 ? Gdiplus::InterpolationModeNearestNeighbor
                                         : Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(textBackdrop.get(), Gdiplus::Rect(b.left - r.left, b.top - r.top, rw(b), rh(b)), v.left,
                    v.top, rw(v), rh(v), Gdiplus::UnitPixel);
    }
    auto brush = CreatePatternBrush(bitmap);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(canvas, dc);
    if (textBrush)
        DeleteObject(textBrush);
    textBrush = brush;
    SetWindowPos(textInput, HWND_TOP, r.left, r.top, std::max(1, rw(r)), std::max(1, rh(r)), SWP_NOACTIVATE);
    SendMessageW(textInput, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    RECT format{0, 0, std::max(1, rw(r)), std::max(1, rh(r))};
    SendMessageW(textInput, EM_SETRECTNP, 0, (LPARAM)&format);
    InvalidateRect(textInput, nullptr, TRUE);
    InvalidateRect(canvas, nullptr, FALSE);
    positioningText = false;
}
void App::growText() {
    if (!textInput || positioningText || finishingText || !doc())
        return;
    auto text = inputText(textInput);
    auto screen = textScreenRect();
    HDC dc = GetDC(textInput);
    auto old = SelectObject(dc, inputFont);
    RECT measure{0, 0, std::max(1, rw(screen)), 0};
    DrawTextW(dc, text.c_str(), (int)text.size(), &measure,
              DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_EXPANDTABS);
    SelectObject(dc, old);
    ReleaseDC(textInput, dc);
    LONG needed = (LONG)std::ceil((rh(measure) + 2) / zoom);
    LONG bottom = std::min<LONG>(viewRect(*doc()).bottom, textDraft.rect.top + needed);
    if (bottom > textDraft.rect.bottom) {
        textDraft.rect.bottom = bottom;
        positionText();
    }
}
void App::finishText(bool accept) {
    if (!textInput || finishingText)
        return;
    finishingText = true;
    auto h = textInput;
    if (accept) {
        auto context = ImmGetContext(h);
        if (context) {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
            ImmReleaseContext(h, context);
        }
    }
    auto c = textDraft;
    c.text = inputText(h);
    textInput = nullptr;
    RemoveWindowSubclass(h, textProc, 1);
    DestroyWindow(h);
    if (textBrush)
        DeleteObject(textBrush);
    textBrush = nullptr;
    if (inputFont)
        DeleteObject(inputFont);
    inputFont = nullptr;
    textBackdrop.reset();
    finishingText = false;
    if (accept && doc()) {
        if (!c.text.empty())
            edit(c);
        else if (c.target >= 0) {
            Command erased;
            erased.tool = Tool::Erase;
            erased.target = c.target;
            edit(erased);
        }
    }
    InvalidateRect(canvas, nullptr, FALSE);
}
LRESULT CALLBACK App::textProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto a = (App *)data;
    if (m == WM_KEYDOWN) {
        if (w == VK_ESCAPE) {
            a->finishText(false);
            if (!a->testMode)
                SetFocus(a->canvas);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == VK_RETURN) {
            a->finishText();
            if (!a->testMode)
                SetFocus(a->canvas);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'S') {
            a->command(ID_SAVE);
            return 0;
        }
    } else if (m == WM_NCDESTROY)
        RemoveWindowSubclass(h, textProc, id);
    return DefSubclassProc(h, m, w, l);
}
} // namespace lc
