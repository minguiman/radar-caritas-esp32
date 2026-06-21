#pragma once

#include <cstdint>

namespace DayClockAssets
{
inline constexpr uint16_t kImageWidth = 480;
inline constexpr uint16_t kImageHeight = 174;
inline constexpr uint8_t kImageCount = 3;
inline constexpr uint16_t kBackgroundWidth = 480;
inline constexpr uint16_t kBackgroundHeight = 480;

struct Image
{
    const uint16_t* pixels;
    const char* label;
};

const Image& imageForMinute(uint8_t minute);
const uint16_t* backgroundPixels();
}
