#include "SharedSd.h"

#include "AppConfig.h"

#include <SPI.h>
#include <chip/esp_expander_base.hpp>

using namespace esp_panel::board;

namespace
{
SPIClass g_sdSpi(FSPI);
expander_sd::FileSystem g_sd;
Board* g_board = nullptr;
bool g_mounted = false;

bool initSdChipSelect(void* context)
{
    auto* expander = static_cast<esp_expander::Base*>(context);
    if (expander == nullptr) {
        return false;
    }

    expander->pinMode(AppConfig::kLcdCsExpanderPin, OUTPUT);
    expander->digitalWrite(AppConfig::kLcdCsExpanderPin, HIGH);
    expander->pinMode(AppConfig::kSdCsExpanderPin, OUTPUT);
    expander->digitalWrite(AppConfig::kSdCsExpanderPin, HIGH);
    return true;
}

void writeSdChipSelect(void* context, bool high)
{
    auto* expander = static_cast<esp_expander::Base*>(context);
    if (expander != nullptr) {
        expander->digitalWrite(AppConfig::kSdCsExpanderPin, high ? HIGH : LOW);
    }
}
}

namespace SharedSd
{
void setBoard(Board& board)
{
    g_board = &board;
}

bool begin(Board& board)
{
    setBoard(board);
    return ensureMounted();
}

bool ensureMounted()
{
    if (g_mounted) {
        return true;
    }
    if (g_board == nullptr) {
        return false;
    }

    auto* ioExpander = g_board->getIO_Expander();
    if (ioExpander == nullptr) {
        return false;
    }
    if (!ioExpander->isOverState(esp_expander::Base::State::BEGIN) && !ioExpander->begin()) {
        return false;
    }

    auto* expander = ioExpander->getBase();
    if (expander == nullptr) {
        return false;
    }

    g_sdSpi.begin(AppConfig::kSdSpiSckPin, AppConfig::kSdSpiMisoPin, AppConfig::kSdSpiMosiPin, -1);

    expander_sd::ChipSelectControl chipSelect{
        .context = expander,
        .init = &initSdChipSelect,
        .write = &writeSdChipSelect,
    };

    g_mounted = g_sd.begin(chipSelect, g_sdSpi, AppConfig::kSdSpiHz, "/sd", 8, false);
    return g_mounted;
}

bool isMounted()
{
    return g_mounted;
}

void end()
{
    if (!g_mounted) {
        return;
    }
    g_sd.end();
    g_mounted = false;
}

expander_sd::FileSystem& fs()
{
    return g_sd;
}
}
