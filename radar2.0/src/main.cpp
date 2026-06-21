#include <Arduino.h>
#include "DebugLog.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

#include <esp_display_panel.hpp>
#include "rom/rtc.h"

#include "RadarApp.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

namespace
{
RadarApp* g_radarApp = nullptr;
}

void setup()
{
    Serial.begin(AppConfig::kSerialBaud);

    RADAR_LOG_PRINTLN();
    RADAR_LOG_PRINTLN("=== BOOT ===");
    RADAR_LOG_PRINTF("Reset reason CPU0: %d\n", rtc_get_reset_reason(0));
    RADAR_LOG_PRINTF("Reset reason CPU1: %d\n", rtc_get_reset_reason(1));

    RADAR_LOG_PRINTLN("Creating board...");
    Board* board = new Board();
    if (board == nullptr) {
        RADAR_LOG_PRINTLN("ERROR: Board nullptr");
        return;
    }

    RADAR_LOG_PRINTLN("board->init()");
    if (!board->init()) {
        RADAR_LOG_PRINTLN("ERROR: board->init() failed");
        return;
    }

    auto lcd = board->getLCD();
    if (lcd == nullptr) {
        RADAR_LOG_PRINTLN("ERROR: LCD nullptr");
        return;
    }

    RADAR_LOG_PRINTLN("Config framebuffer number...");
    lcd->configFrameBufferNumber(2);

    RADAR_LOG_PRINTLN("board->begin()");
    if (!board->begin()) {
        RADAR_LOG_PRINTLN("ERROR: board->begin() failed");
        return;
    }

    auto backlight = board->getBacklight();
    if (backlight != nullptr) {
        RADAR_LOG_PRINTLN("Backlight 100%");
        backlight->setBrightness(100);
    } else {
        RADAR_LOG_PRINTLN("WARN: backlight nullptr");
    }

    lcd->setDisplayOnOff(true);

    RADAR_LOG_PRINTLN("RadarApp begin...");
    g_radarApp = new RadarApp(board);
    if (!g_radarApp->begin()) {
        RADAR_LOG_PRINTLN("ERROR: RadarApp::begin() failed");
        return;
    }

    RADAR_LOG_PRINTLN("Radar app ready");
}

void loop()
{
    if (g_radarApp != nullptr) {
        g_radarApp->loop();
    }
    // RadarApp ya cede CPU con vTaskDelay en cada vista; aquí solo un yield mínimo.
    vTaskDelay(1);
}
