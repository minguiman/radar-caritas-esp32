#pragma once

#include <Arduino.h>

#include <vector>
#include <cstdint>

struct Plane
{
    String id;
    String callsign;
    String registration;
    String countryName;
    String countryCode;
    String aircraftType;
    String squawk;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitudeMeters = 0.0f;
    float altitudeFeet = 0.0f;
    int flightLevel = 0;
    float velocityMps = 0.0f;
    float headingDeg = 0.0f;
    float distanceKm = 0.0f;
    float bearingDeg = 0.0f;

    // Proyección normalizada del contacto sobre el radar.
    // Se calcula al recibir datos o al cambiar el rango visual.
    // El renderer solo multiplica por el radio en píxeles.
    float radarScreenXNorm = 0.0f;
    float radarScreenYNorm = 0.0f;

    // Datos compactos para render/calculo de labels. Se preparan una vez al
    // recibir la API para que el renderer no cree Strings ni vuelva a parsear
    // el ICAO/callsign en cada frame.
    uint32_t radarKey = 0;
    uint8_t radarLabelTextLen = 0;
    char radarLabelText[9] = {};

    bool valid = false;
    bool emergency = false;
    bool military = false;
};

struct VisiblePlane
{
    Plane plane;
    float brightness = 0.0f;
    uint32_t lastSweepHitMs = 0;
    bool labelActive = false;
};

struct FetchResult
{
    std::vector<Plane> planes;
    bool ok = false;
    int rawStateCount = 0;
    int validPositionCount = 0;
    String sourceName;
    String statusText;
};
