#pragma once
#include "core.h"
namespace lc {
namespace theme {
inline constexpr uint32_t Window = 0xffffffff, Sidebar = 0xfff8fafd, Canvas = 0xffedf1f6;
inline constexpr uint32_t Ink = 0xff172033, Muted = 0xff526176, Line = 0xffdce3ed;
inline constexpr uint32_t Blue = 0xff1761d7, BlueHover = 0xff124fae, Tint = 0xffeaf2ff;
inline void rounded(Graphics &g, Gdiplus::RectF rect, float radius, uint32_t fill, uint32_t border = 0) {
    Gdiplus::GraphicsPath path;
    float d = radius * 2;
    path.AddArc(rect.X, rect.Y, d, d, 180, 90);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270, 90);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0, 90);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush{Color(fill)};
    g.FillPath(&brush, &path);
    if (border) {
        Gdiplus::Pen pen{Color(border), 1};
        g.DrawPath(&pen, &path);
    }
}
} // namespace theme
} // namespace lc
