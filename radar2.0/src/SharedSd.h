#pragma once

#include "ExpanderSDFS.h"

#include <esp_display_panel.hpp>

namespace SharedSd
{
void setBoard(esp_panel::board::Board& board);
bool begin(esp_panel::board::Board& board);
bool ensureMounted();
bool isMounted();
void end();
expander_sd::FileSystem& fs();
}
