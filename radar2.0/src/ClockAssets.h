#pragma once

#include <cstdint>

namespace ClockAssets
{
inline constexpr int kClockFrameWidth = 480;
inline constexpr int kClockFrameHeight = 480;
inline constexpr int kClockBackgroundWidth = 1024;
inline constexpr int kClockBackgroundHeight = 1024;

extern const uint16_t kClockFrame565[kClockFrameWidth * kClockFrameHeight];
extern const uint16_t kClockBackground565[kClockBackgroundWidth * kClockBackgroundHeight];
extern const uint16_t kClockFrameTransparentStart[kClockFrameHeight];
extern const uint16_t kClockFrameTransparentEnd[kClockFrameHeight];
}
