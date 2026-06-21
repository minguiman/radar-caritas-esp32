#pragma once

#include <Arduino.h>
#include <cstdint>

namespace AircraftAssets
{
inline constexpr uint16_t kImageWidth = 190;
inline constexpr uint16_t kImageHeight = 100;
inline constexpr uint16_t kTransparentKey = 0xF81F;

enum class Kind : uint8_t
{
    AirbusA320 = 0,
    Boeing737800 = 1,
    EmbraerE195 = 2,
    CRJ1000 = 3,
    ATR72600 = 4,
    AirbusA330 = 5,
    Boeing7879 = 6,
    AirbusA350900 = 7,
    Boeing777300ER = 8,
    AirbusA340 = 9,
    AirbusA380 = 10,
    Boeing747 = 11,
    B752 = 12,
    P06T = 13,
    BE20 = 14,
    SIRA = 15,
    GA6C = 16,
    G2CA = 17,
    EC35 = 18,
    C56X = 19,
    Fallback = 20,
    Count = 21,
    Unknown = 255
};

struct Image
{
    const uint16_t* pixels;
    uint16_t width;
    uint16_t height;
    const char* label;
};

Kind kindForAircraftType(const String& aircraftType);
const Image* imageForKind(Kind kind);
const Image* imageForAircraftType(const String& aircraftType);
}
