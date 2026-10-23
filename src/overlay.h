#pragma once
#include "core.h"
namespace lc {
// Resources exist only while the capture selector is open.
class OverlayRenderer {
    struct Surface {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ old = nullptr;
        uint32_t *pixels = nullptr;
        void create(int w, int h);
        void clear();
    } source, dimmed, frame;
    int width = 0, height = 0;
    HPEN border = nullptr, cross = nullptr;
    HBRUSH panelBrush = nullptr;
    HFONT font = nullptr;

  public:
    ~OverlayRenderer() {
        clear();
    }
    OverlayRenderer() = default;
    OverlayRenderer(const OverlayRenderer &) = delete;
    OverlayRenderer &operator=(const OverlayRenderer &) = delete;
    void prepare(Bitmap &image);
    void clear();
    RECT panel(POINT mouse) const;
    HRGN damage(RECT before, POINT oldMouse, RECT after, POINT mouse, bool freeform) const;
    void paint(HDC dc, HRGN dirty, RECT selection, POINT mouse, POINT origin, Mode mode,
               const std::vector<POINT> &polygon, bool ready);
};
} // namespace lc
