#pragma once

#include "PlaneData.h"

struct WeatherResult
{
    bool ok = false;
    float temperatureC = 0.0f;
    int weatherCode = -1;
    String conditionText;
    String statusText;
};

class TrafficClient
{
public:
    FetchResult fetchSnapshot(double ownshipLat, double ownshipLon, float rangeKm) const;
    WeatherResult fetchWeather(double latitude, double longitude) const;
};
