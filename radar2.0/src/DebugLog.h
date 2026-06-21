#pragma once

#include <Arduino.h>

#ifndef RADAR_ENABLE_DEBUG_LOGS
#define RADAR_ENABLE_DEBUG_LOGS 0
#endif

#if RADAR_ENABLE_DEBUG_LOGS
#define RADAR_LOG_PRINT(...) Serial.print(__VA_ARGS__)
#define RADAR_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define RADAR_LOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define RADAR_LOG_PRINT(...) do {} while (0)
#define RADAR_LOG_PRINTLN(...) do {} while (0)
#define RADAR_LOG_PRINTF(...) do {} while (0)
#endif
