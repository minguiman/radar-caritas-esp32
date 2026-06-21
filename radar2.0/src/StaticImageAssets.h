#pragma once

#include <cstdint>

namespace StaticImageAssets
{
inline constexpr int kImageWidth = 480;
inline constexpr int kImageHeight = 480;

extern const uint16_t kStaticImage565[kImageWidth * kImageHeight];
}
