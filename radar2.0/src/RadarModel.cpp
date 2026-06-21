#include "RadarModel.h"

#include "AppConfig.h"
#include "CountryResolver.h"
#include "MathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstring>

namespace
{
constexpr float kPi = 3.14159265358979323846f;

void updatePlaneRadarProjection(Plane& plane, float rangeKm)
{
    const float safeRangeKm = std::max(1.0f, rangeKm);
    const float ratio = std::min(1.0f, plane.distanceKm / safeRangeKm);
    const float radians = (plane.bearingDeg - 90.0f) * (kPi / 180.0f);

    plane.radarScreenXNorm = cosf(radians) * ratio;
    plane.radarScreenYNorm = sinf(radians) * ratio;
}

void updatePlaneListRadarProjection(std::vector<Plane>& planes, float rangeKm)
{
    for (Plane& plane : planes) {
        updatePlaneRadarProjection(plane, rangeKm);
    }
}

void updateVisiblePlaneListRadarProjection(std::vector<VisiblePlane>& planes, float rangeKm)
{
    for (VisiblePlane& visible : planes) {
        updatePlaneRadarProjection(visible.plane, rangeKm);
    }
}

void buildCompactRadarLabel(Plane& plane)
{
    plane.radarLabelText[0] = '\0';
    plane.radarLabelTextLen = 0;

    const String* source = &plane.callsign;
    if (source->isEmpty()) {
        source = &plane.id;
    }

    uint8_t n = 0;
    bool seenNonSpace = false;
    for (size_t i = 0; i < source->length() && n < 7; ++i) {
        char c = source->charAt(i);
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
        if (c == ' ') {
            if (!seenNonSpace) {
                continue;
            }
            break;
        }
        seenNonSpace = true;
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
            plane.radarLabelText[n++] = c;
        }
    }

    if (n == 0) {
        const char* noid = "NOID";
        while (*noid != '\0' && n < 7) {
            plane.radarLabelText[n++] = *noid++;
        }
    }

    plane.radarLabelText[n] = '\0';
    plane.radarLabelTextLen = n;
}

void updatePlaneRenderCache(Plane& plane, float rangeKm, uint32_t (*keyFromId)(const String&))
{
    updatePlaneRadarProjection(plane, rangeKm);
    plane.radarKey = keyFromId != nullptr ? keyFromId(plane.id) : 0;
    buildCompactRadarLabel(plane);
}
}

RadarModel::RadarModel(double ownshipLat, double ownshipLon, float rangeKm)
    : m_ownshipLat(ownshipLat)
    , m_ownshipLon(ownshipLon)
    , m_rangeKm(rangeKm)
    , m_fetchRangeKm(rangeKm)
{
    // Reservas fijas desde el arranque: evita realojos del heap durante el
    // barrido/render cuando llegan snapshots nuevos de la API.
    m_pendingPlanes.reserve(AppConfig::kMaxVisiblePlanes);
    m_pendingPlaneKeys.reserve(AppConfig::kMaxVisiblePlanes);
    m_pendingKeysSorted.reserve(AppConfig::kMaxVisiblePlanes);
    m_visiblePlanes.reserve(AppConfig::kMaxVisiblePlanes);
    m_visiblePlaneKeys.reserve(AppConfig::kMaxVisiblePlanes);
}

uint32_t RadarModel::planeKeyFromId(const String& planeId)
{
    const String clean = CountryResolver::sanitizeHexId(planeId);
    if (clean.isEmpty()) {
        return 0;
    }

    char* end = nullptr;
    const unsigned long parsed = strtoul(clean.c_str(), &end, 16);
    if (end == clean.c_str()) {
        return 0;
    }

    // ICAO24 son 24 bits. Dejamos 0 como clave invalida/fallback.
    return static_cast<uint32_t>(parsed) & 0x00FFFFFFUL;
}

void RadarModel::setOwnship(double ownshipLat, double ownshipLon, float rangeKm, bool resetApiStatus)
{
    m_ownshipLat = ownshipLat;
    m_ownshipLon = ownshipLon;
    m_rangeKm = rangeKm;
    m_fetchRangeKm = rangeKm;
    m_pendingPlanes.clear();
    m_pendingPlaneKeys.clear();
    m_pendingKeysSorted.clear();
    m_visiblePlanes.clear();
    m_visiblePlaneKeys.clear();
    m_previousFrameMs = 0;
    m_nowMs = 0;
    m_sweepAngleDeg = 0.0f;
    if (resetApiStatus) {
        m_apiOk = false;
        m_rawStateCount = 0;
        m_validPositionCount = 0;
    }
}

void RadarModel::eraseVisiblePlanesOutsideRange()
{
    size_t write = 0;
    const bool keysInSync = m_visiblePlaneKeys.size() == m_visiblePlanes.size();
    for (size_t read = 0; read < m_visiblePlanes.size(); ++read) {
        if (m_visiblePlanes[read].plane.distanceKm > m_rangeKm) {
            continue;
        }
        if (write != read) {
            m_visiblePlanes[write] = m_visiblePlanes[read];
            if (keysInSync) {
                m_visiblePlaneKeys[write] = m_visiblePlaneKeys[read];
            }
        }
        ++write;
    }
    m_visiblePlanes.resize(write);
    if (keysInSync) {
        m_visiblePlaneKeys.resize(write);
    } else {
        m_visiblePlaneKeys.clear();
        m_visiblePlaneKeys.reserve(m_visiblePlanes.size());
        for (const VisiblePlane& visible : m_visiblePlanes) {
            m_visiblePlaneKeys.push_back(visible.plane.radarKey != 0 ? visible.plane.radarKey : planeKeyFromId(visible.plane.id));
        }
    }
}

void RadarModel::setRangeKm(float rangeKm)
{
    // Cambio solo visual de escala/distancia del radar.
    // No borramos los últimos aviones recibidos para que la pantalla no se quede
    // en blanco mientras llega la siguiente respuesta de la API.
    m_rangeKm = rangeKm;
    m_fetchRangeKm = std::max(m_fetchRangeKm, m_rangeKm);

    // La escala visual ha cambiado: recalculamos la proyección una vez aquí,
    // no en cada frame de render.
    updatePlaneListRadarProjection(m_pendingPlanes, m_rangeKm);
    updateVisiblePlaneListRadarProjection(m_visiblePlanes, m_rangeKm);

    // Los contactos ya visibles que quedan fuera del nuevo rango visual se quitan
    // del radar normal, pero pendingPlanes se conserva. En Dragon Ball eso permite
    // seguir dibujando flechas de borde hacia aviones fuera del círculo visible.
    eraseVisiblePlanesOutsideRange();
}

void RadarModel::setFetchRangeKm(float fetchRangeKm)
{
    // El rango visual sigue siendo m_rangeKm. Este rango solo indica hasta qué
    // distancia aceptamos aviones recibidos por la API. En el radar normal será
    // igual al rango visual; en Dragon Ball puede ser mayor para poder dibujar
    // flechas hacia bolas fuera del círculo visible.
    m_fetchRangeKm = std::max(m_rangeKm, fetchRangeKm);

    m_pendingPlanes.erase(
        std::remove_if(m_pendingPlanes.begin(), m_pendingPlanes.end(), [this](const Plane& plane) {
            return plane.distanceKm > m_fetchRangeKm;
        }),
        m_pendingPlanes.end());
    updatePlaneListRadarProjection(m_pendingPlanes, m_rangeKm);
    rebuildPendingIdIndex();

    updateVisiblePlaneListRadarProjection(m_visiblePlanes, m_rangeKm);
    eraseVisiblePlanesOutsideRange();
}

void RadarModel::setSweepPeriodSeconds(uint8_t seconds)
{
    if (seconds < AppConfig::kMinApiRefreshSeconds) {
        seconds = AppConfig::kMinApiRefreshSeconds;
    }
    if (seconds > AppConfig::kMaxApiRefreshSeconds) {
        seconds = AppConfig::kMaxApiRefreshSeconds;
    }
    m_sweepPeriodSeconds = seconds;
    m_sweepDegreesPerSecond = 360.0f / static_cast<float>(seconds);
    const uint32_t sweepMs = static_cast<uint32_t>(seconds) * 1000UL;
    m_contactHoldMs = (sweepMs * 3UL) / 4UL;
    m_contactFadeMs = sweepMs - m_contactHoldMs;
}

void RadarModel::applyFetchResult(const FetchResult& fetchResult, bool militaryPriority)
{
    m_apiOk = fetchResult.ok;
    m_rawStateCount = fetchResult.rawStateCount;
    m_validPositionCount = fetchResult.validPositionCount;

    // Si la consulta falla, mantenemos los últimos aviones recibidos. Así al
    // cambiar opciones o si la API tarda/falla, el radar no se queda en blanco
    // hasta que llegue una respuesta nueva válida.
    if (fetchResult.ok) {
        m_pendingPlanes.clear();
        m_pendingPlaneKeys.clear();
        m_pendingKeysSorted.clear();

        if (m_pendingPlanes.capacity() < static_cast<size_t>(AppConfig::kMaxVisiblePlanes)) {
            m_pendingPlanes.reserve(AppConfig::kMaxVisiblePlanes);
        }
        for (Plane plane : fetchResult.planes) {
            plane.distanceKm = static_cast<float>(MathUtils::haversineDistanceKm(
                m_ownshipLat,
                m_ownshipLon,
                plane.latitude,
                plane.longitude));
            plane.bearingDeg = static_cast<float>(MathUtils::initialBearingDeg(
                m_ownshipLat,
                m_ownshipLon,
                plane.latitude,
                plane.longitude));
            plane.altitudeFeet = MathUtils::metersToFeet(plane.altitudeMeters);
            plane.flightLevel = MathUtils::metersToFlightLevel(plane.altitudeMeters);
            updatePlaneRenderCache(plane, m_rangeKm, &RadarModel::planeKeyFromId);

            if (plane.valid && plane.distanceKm <= m_fetchRangeKm) {
                // Pais resuelto una sola vez por snapshot. TrafficClient ya no lo repite
                // y el popup/flag reutilizan countryCode/countryName de este Plane.
                CountryResolver::applyToPlane(plane);
                m_pendingPlanes.push_back(plane);
            }
        }

        std::sort(m_pendingPlanes.begin(), m_pendingPlanes.end(),
            [militaryPriority](const Plane& left, const Plane& right) {
                if (militaryPriority && left.military != right.military) {
                    return left.military && !right.military;
                }
                return left.distanceKm < right.distanceKm;
            });

        if (static_cast<int>(m_pendingPlanes.size()) > AppConfig::kMaxVisiblePlanes) {
            m_pendingPlanes.resize(AppConfig::kMaxVisiblePlanes);
        }
        rebuildPendingIdIndex();
    }
}

void RadarModel::rebuildPendingIdIndex()
{
    m_pendingPlaneKeys.clear();
    m_pendingPlaneKeys.reserve(m_pendingPlanes.size());
    m_pendingKeysSorted.clear();
    m_pendingKeysSorted.reserve(m_pendingPlanes.size());

    for (const Plane& plane : m_pendingPlanes) {
        const uint32_t key = plane.radarKey != 0 ? plane.radarKey : planeKeyFromId(plane.id);
        m_pendingPlaneKeys.push_back(key);
        if (key != 0) {
            m_pendingKeysSorted.push_back(key);
        }
    }

    std::sort(m_pendingKeysSorted.begin(), m_pendingKeysSorted.end());
    m_pendingKeysSorted.erase(std::unique(m_pendingKeysSorted.begin(), m_pendingKeysSorted.end()), m_pendingKeysSorted.end());
}

bool RadarModel::isPendingPlane(uint32_t planeKey, const String& planeId) const
{
    if (planeKey != 0) {
        return std::binary_search(m_pendingKeysSorted.begin(), m_pendingKeysSorted.end(), planeKey);
    }

    // Fallback muy raro: id no parseable. Conserva compatibilidad sin duplicar Strings.
    for (const Plane& pending : m_pendingPlanes) {
        if (pending.id == planeId) {
            return true;
        }
    }
    return false;
}

void RadarModel::tick(uint32_t nowMs, bool updateSweepHits)
{
    float deltaSeconds = m_previousFrameMs == 0
        ? 0.0f
        : static_cast<float>(nowMs - m_previousFrameMs) / 1000.0f;

    // Usamos tiempo real transcurrido para el barrido.
    // Antes se limitaba deltaSeconds para evitar saltos grandes tras un bloqueo,
    // pero eso podia hacer que la linea se ralentizase o avanzase irregularmente
    // cuando habia pequenos picos de render/red. Si un frame tarda mas, la linea
    // avanza lo que corresponde al tiempo real y mantiene velocidad constante.

    m_previousFrameMs = nowMs;
    m_nowMs = nowMs;
    m_sweepAngleDeg = fmodf(m_sweepAngleDeg + (deltaSeconds * m_sweepDegreesPerSecond), 360.0f);

    if (!updateSweepHits) {
        return;
    }

    for (auto& plane : m_visiblePlanes) {
        const uint32_t elapsedSinceHit = nowMs - plane.lastSweepHitMs;
        if (elapsedSinceHit <= m_contactHoldMs) {
            plane.brightness = 1.0f;
            continue;
        }

        const uint32_t fadeElapsed = elapsedSinceHit - m_contactHoldMs;
        if (fadeElapsed >= m_contactFadeMs) {
            plane.brightness = 0.10f;
            plane.labelActive = false;
            continue;
        }

        const float fadeProgress = static_cast<float>(fadeElapsed) / static_cast<float>(m_contactFadeMs);
        plane.brightness = std::max(0.10f, 1.0f - (fadeProgress * 0.90f));
    }

    updateVisiblePlanes(nowMs);
}

void RadarModel::tickNow(uint32_t nowMs)
{
    m_previousFrameMs = nowMs;
    m_nowMs = nowMs;
}

void RadarModel::settleAfterHidden(uint32_t nowMs)
{
    // Al volver desde otras vistas no queremos que el radar parezca que
    // "re-escanea" todos los contactos ni que el primer frame acumule todo el
    // tiempo oculto como un salto enorme del barrido. Envejecemos visualmente
    // los contactos con el tiempo real transcurrido, congelamos el delta del
    // siguiente tick y desactivamos labels hasta el siguiente snapshot/norte.
    m_previousFrameMs = nowMs;
    m_nowMs = nowMs;

    for (auto& plane : m_visiblePlanes) {
        const uint32_t elapsedSinceHit = nowMs - plane.lastSweepHitMs;
        if (elapsedSinceHit <= m_contactHoldMs) {
            // Si venimos de otra pantalla, evitamos que un full-frame de retorno
            // haga que todos los contactos se vean con brillo maximo. El
            // siguiente paso real del barrido los volvera a destacar uno a uno.
            plane.brightness = std::min(plane.brightness, 0.38f);
            plane.labelActive = false;
            continue;
        }

        const uint32_t fadeElapsed = elapsedSinceHit - m_contactHoldMs;
        if (fadeElapsed >= m_contactFadeMs) {
            plane.brightness = 0.10f;
            plane.labelActive = false;
            continue;
        }

        const float fadeProgress = static_cast<float>(fadeElapsed) / static_cast<float>(m_contactFadeMs);
        plane.brightness = std::max(0.10f, 1.0f - (fadeProgress * 0.90f));
        plane.labelActive = false;
    }
}

float RadarModel::sweepAngleDeg() const
{
    return m_sweepAngleDeg;
}

uint8_t RadarModel::sweepPeriodSeconds() const
{
    return m_sweepPeriodSeconds;
}

bool RadarModel::apiOk() const
{
    return m_apiOk;
}

int RadarModel::rawStateCount() const
{
    return m_rawStateCount;
}

int RadarModel::validPositionCount() const
{
    return m_validPositionCount;
}

uint32_t RadarModel::nowMs() const
{
    return m_nowMs;
}

float RadarModel::rangeKm() const
{
    return m_rangeKm;
}

float RadarModel::fetchRangeKm() const
{
    return m_fetchRangeKm;
}

const std::vector<Plane>& RadarModel::pendingPlanes() const
{
    return m_pendingPlanes;
}

const std::vector<VisiblePlane>& RadarModel::visiblePlanes() const
{
    return m_visiblePlanes;
}

void RadarModel::updateVisiblePlanes(uint32_t nowMs)
{
    if (m_pendingPlaneKeys.size() != m_pendingPlanes.size()) {
        rebuildPendingIdIndex();
    }
    if (m_visiblePlaneKeys.size() != m_visiblePlanes.size()) {
        m_visiblePlaneKeys.clear();
        m_visiblePlaneKeys.reserve(m_visiblePlanes.size());
        for (const VisiblePlane& visible : m_visiblePlanes) {
            m_visiblePlaneKeys.push_back(visible.plane.radarKey != 0 ? visible.plane.radarKey : planeKeyFromId(visible.plane.id));
        }
    }

    for (size_t pendingIndex = 0; pendingIndex < m_pendingPlanes.size(); ++pendingIndex) {
        const auto& pendingPlane = m_pendingPlanes[pendingIndex];
        const uint32_t pendingKey = m_pendingPlaneKeys[pendingIndex];

        // pendingPlanes puede contener aviones recibidos fuera del rango visual
        // cuando Dragon Ball usa fetch ampliado. El radar normal/visible solo
        // debe adquirir contactos dentro de m_rangeKm.
        if (pendingPlane.distanceKm > m_rangeKm) {
            continue;
        }

        float angleDelta = fabsf(m_sweepAngleDeg - pendingPlane.bearingDeg);
        if (angleDelta > 180.0f) {
            angleDelta = 360.0f - angleDelta;
        }
        if (angleDelta > AppConfig::kSweepAcquireWindowDeg) {
            continue;
        }

        const int existingIndex = indexOfVisiblePlane(pendingKey, pendingPlane.id);
        if (existingIndex >= 0) {
            auto& visiblePlane = m_visiblePlanes[static_cast<size_t>(existingIndex)];
            visiblePlane.plane = pendingPlane;
            m_visiblePlaneKeys[static_cast<size_t>(existingIndex)] = pendingKey;
            visiblePlane.brightness = 1.0f;
            visiblePlane.lastSweepHitMs = nowMs;
            visiblePlane.labelActive = true;
            continue;
        }

        VisiblePlane visiblePlane;
        visiblePlane.plane = pendingPlane;
        visiblePlane.brightness = 1.0f;
        visiblePlane.lastSweepHitMs = nowMs;
        visiblePlane.labelActive = true;
        m_visiblePlanes.push_back(visiblePlane);
        m_visiblePlaneKeys.push_back(pendingKey);
    }

    size_t write = 0;
    for (size_t read = 0; read < m_visiblePlanes.size(); ++read) {
        const uint32_t visibleKey = read < m_visiblePlaneKeys.size()
            ? m_visiblePlaneKeys[read]
            : (m_visiblePlanes[read].plane.radarKey != 0 ? m_visiblePlanes[read].plane.radarKey : planeKeyFromId(m_visiblePlanes[read].plane.id));
        const bool remove = !isPendingPlane(visibleKey, m_visiblePlanes[read].plane.id)
            || m_visiblePlanes[read].brightness <= 0.0f;
        if (remove) {
            continue;
        }
        if (write != read) {
            m_visiblePlanes[write] = m_visiblePlanes[read];
            if (write < m_visiblePlaneKeys.size()) {
                m_visiblePlaneKeys[write] = visibleKey;
            }
        }
        ++write;
    }
    m_visiblePlanes.resize(write);
    m_visiblePlaneKeys.resize(write);
}

int RadarModel::indexOfVisiblePlane(uint32_t planeKey, const String& planeId) const
{
    const bool keysInSync = m_visiblePlaneKeys.size() == m_visiblePlanes.size();
    if (planeKey != 0 && keysInSync) {
        for (size_t i = 0; i < m_visiblePlaneKeys.size(); ++i) {
            if (m_visiblePlaneKeys[i] == planeKey) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    for (size_t i = 0; i < m_visiblePlanes.size(); ++i) {
        if (m_visiblePlanes[i].plane.id == planeId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
