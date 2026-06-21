#pragma once

#include <cstdint>

#include <esp_display_panel.hpp>

enum class ImageProgressStyle : uint8_t
{
    Focus,
    ShortBreak,
    LongBreak,
    Paused
};

struct ImageFrame565View
{
    const uint16_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t canvasWidth = 0;
    uint16_t canvasHeight = 0;
    uint16_t offsetX = 0;
    uint16_t offsetY = 0;
    const char* title = nullptr;
    const char* detail = nullptr;
    const char* timeLabel = nullptr;
    int16_t screenOffsetY = 0;
    bool progressVisible = false;
    ImageProgressStyle progressStyle = ImageProgressStyle::Focus;
    uint8_t progressPercent = 0;
};

namespace ImageSource
{
enum class Interaction
{
    Touch,
    ViewChanged,
    OptionChanged,
    IdlePause
};

enum class Mood
{
    Neutral,
    Focus,
    Paused,
    Break,
    Sleep,
    Angry,
    Hydrate,
    Stretch
};

enum class Moment
{
    PomodoroPaused,
    PomodoroReset,
    PomodoroCompleted
};

void reserveMemory();
bool begin(esp_panel::board::Board& board);
void suspend();
void end();
void setMood(Mood mood);
void playMoment(Moment moment, uint32_t nowMs);
void notifyInteraction(Interaction interaction, uint32_t nowMs);
ImageFrame565View currentImageFrame(uint32_t nowMs);
}
