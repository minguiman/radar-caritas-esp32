#pragma once

#include "RuntimeConfig.h"

namespace esp_panel::board
{
class Board;
}

class ConfigStore
{
public:
    void begin(esp_panel::board::Board& board);
    bool load(RuntimeConfig& config) const;
    bool save(const RuntimeConfig& config) const;
    bool saveAlarm(bool enabled, uint8_t hour, uint8_t minute) const;
    int loadLastWifiProfileIndex() const;
    bool saveLastWifiProfileIndex(int profileIndex) const;
    void clear() const;

private:
    bool loadFromSd(RuntimeConfig& config) const;
    bool saveToSd(const RuntimeConfig& config) const;
    bool loadFromNvs(RuntimeConfig& config) const;
    bool saveToNvs(const RuntimeConfig& config) const;
    bool loadLastWifiProfileIndexFromSd(int& profileIndex) const;
    bool saveLastWifiProfileIndexToSd(int profileIndex) const;
    esp_panel::board::Board* m_board = nullptr;
};
