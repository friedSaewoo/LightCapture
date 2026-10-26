#pragma once
#include "core.h"
namespace lc {
struct PaletteColor {
    uint32_t value;
    const wchar_t *name;
};
inline constexpr PaletteColor Palette[] = {
    {0xff111827, L"검정"},   {0xff6b7280, L"회색"},   {0xff8b4513, L"갈색"},      {0xffe5484d, L"빨강"},
    {0xfff97316, L"주황"},   {0xffffd600, L"노랑"},   {0xff16a34a, L"초록"},      {0xff06b6d4, L"청록"},
    {0xff2563eb, L"파랑"},   {0xff9333ea, L"보라"},   {0xffffffff, L"흰색"},      {0xffd1d5db, L"연한 회색"},
    {0xffd6b38d, L"황갈색"}, {0xfff9a8d4, L"분홍"},   {0xfffdba74, L"살구"},      {0xfffef08a, L"연한 노랑"},
    {0xffa3e635, L"연두"},   {0xffa5f3fc, L"하늘색"}, {0xff93c5fd, L"연한 파랑"}, {0xffd8b4fe, L"연한 보라"}};
inline constexpr int PaletteCount = sizeof(Palette) / sizeof(Palette[0]);
inline constexpr const wchar_t *ToolNames[] = {L"개체 선택 / 이동",
                                               L"펜",
                                               L"직선",
                                               L"화살표",
                                               L"사각형",
                                               L"타원",
                                               L"형광펜",
                                               L"텍스트",
                                               L"모자이크",
                                               L"자르기",
                                               L"지우개",
                                               L"",
                                               L"사각형 선택 / 이동"};
inline constexpr Tool ToolbarTools[] = {Tool::PixelSelect, Tool::Select,    Tool::Pen,     Tool::Line,
                                        Tool::Arrow,       Tool::Rectangle, Tool::Ellipse, Tool::Highlight,
                                        Tool::Text,        Tool::Mosaic,    Tool::Crop,    Tool::Erase};
} // namespace lc
