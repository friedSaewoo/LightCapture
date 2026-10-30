#pragma once
#include "core.h"
namespace lc {
struct ClipboardImage {
    std::vector<BYTE> png, dib, dibv5;
};
ClipboardImage makeClipboardImage(Bitmap &image);
bool publishClipboardImage(HWND owner, const ClipboardImage &image);
Image decodeClipboardDib(const std::vector<BYTE> &bytes);
Image readClipboardImage(HWND owner);
} // namespace lc
