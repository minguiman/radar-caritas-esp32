#pragma once

#include "PlaneData.h"

#include <cstdint>

namespace CountryFlags
{
inline constexpr uint8_t kFlagWidth = 32;
inline constexpr uint8_t kFlagHeight = 22;
inline constexpr uint16_t kTransparentKey = 0xF81F;

struct FlagImage
{
    const char* code;
    const uint16_t* pixels;
};

const FlagImage* flagForCountryCode(const char* code);
const FlagImage* flagForPlane(const Plane& plane);
}
