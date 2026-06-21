#pragma once

#include <cstdint>

namespace DayClockFontAssets
{
struct Glyph
{
    char code;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    uint8_t yOffset;
    uint32_t offset;
};

struct Font
{
    uint8_t lineHeight;
    uint8_t glyphCount;
    const Glyph* glyphs;
    const uint8_t* alpha;
};

const Font& titleFont();
const Font& digitalFont();
const Glyph* glyphFor(const Font& font, char code);
}
