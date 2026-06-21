#pragma once

#include "AppConfig.h"
#include "PlaneData.h"

#include <Arduino.h>

#include <cstdint>
#include <vector>

class RadarModel
{
public:
    RadarModel(double ownshipLat, double ownshipLon, float rangeKm);

    void setOwnship(double ownshipLat, double ownshipLon, float rangeKm, bool resetApiStatus = true);
    void setRangeKm(float rangeKm);
    void setFetchRangeKm(float fetchRangeKm);
    void setSweepPeriodSeconds(uint8_t seconds);
    void applyFetchResult(const FetchResult& fetchResult, bool militaryPriority = false);
    void tick(uint32_t nowMs, bool updateSweepHits = true);
    void tickNow(uint32_t nowMs);
    void settleAfterHidden(uint32_t nowMs);

    float sweepAngleDeg() const;
    uint8_t sweepPeriodSeconds() const;
    bool apiOk() const;
    int rawStateCount() const;
    int validPositionCount() const;
    uint32_t nowMs() const;
    float rangeKm() const;
    float fetchRangeKm() const;
    const std::vector<Plane>& pendingPlanes() const;
    const std::vector<VisiblePlane>& visiblePlanes() const;

private:
    void updateVisiblePlanes(uint32_t nowMs);
    int indexOfVisiblePlane(uint32_t planeKey, const String& planeId) const;
    bool isPendingPlane(uint32_t planeKey, const String& planeId) const;
    void eraseVisiblePlanesOutsideRange();
    static uint32_t planeKeyFromId(const String& planeId);

    double m_ownshipLat = 0.0;
    double m_ownshipLon = 0.0;
    float m_rangeKm = 0.0f;
    float m_fetchRangeKm = 0.0f;
    uint32_t m_previousFrameMs = 0;
    uint32_t m_nowMs = 0;
    float m_sweepAngleDeg = 0.0f;
    float m_sweepDegreesPerSecond = 360.0f / static_cast<float>(AppConfig::kDefaultApiRefreshSeconds);
    uint8_t m_sweepPeriodSeconds = AppConfig::kDefaultApiRefreshSeconds;
    uint32_t m_contactHoldMs = AppConfig::kContactHoldMs;
    uint32_t m_contactFadeMs = AppConfig::kContactFadeMs;
    bool m_apiOk = false;
    int m_rawStateCount = 0;
    int m_validPositionCount = 0;
    std::vector<Plane> m_pendingPlanes;
    // Misma longitud/orden que m_pendingPlanes. Evita parsear/comparar String en el bucle caliente.
    std::vector<uint32_t> m_pendingPlaneKeys;
    // Indice ordenado de ICAO24 numerico para saber si un visible sigue existiendo.
    std::vector<uint32_t> m_pendingKeysSorted;
    std::vector<VisiblePlane> m_visiblePlanes;
    // Misma longitud/orden que m_visiblePlanes. Evita comparar String en indexOfVisiblePlane().
    std::vector<uint32_t> m_visiblePlaneKeys;

    void rebuildPendingIdIndex();
};
