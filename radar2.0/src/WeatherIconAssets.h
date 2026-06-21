#pragma once

#include <cstdint>

namespace WeatherIconAssets
{
inline constexpr uint8_t kIconWidth = 48;
inline constexpr uint8_t kIconHeight = 48;

enum class Kind : uint8_t
{
    Clear = 0,
    Cloudy = 1,
    Rain = 2,
    Snow = 3,
    Fog = 4,
    Thunder = 5,
    Unavailable = 6,
};

const uint8_t* alphaForKind(Kind kind);
}
