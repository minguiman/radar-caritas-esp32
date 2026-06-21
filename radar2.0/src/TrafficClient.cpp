#include "TrafficClient.h"

#include "AppConfig.h"
#include "CountryResolver.h"
#include "MathUtils.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <utility>
#include <math.h>

namespace
{
struct PsramJsonAllocator
{
    void* allocate(size_t size)
    {
        void* pointer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (pointer == nullptr) {
            pointer = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        }
        return pointer;
    }

    void deallocate(void* pointer)
    {
        heap_caps_free(pointer);
    }
};

struct BoundingBox
{
    double minLat = 0.0;
    double minLon = 0.0;
    double maxLat = 0.0;
    double maxLon = 0.0;
};

constexpr uint32_t kReadsbHttpTimeoutMs = 12'000;
constexpr uint32_t kOpenSkyHttpTimeoutMs = 10'000;
constexpr uint32_t kAirplanesLiveHttpTimeoutMs = 12'000;

BoundingBox buildBoundingBox(double latitude, double longitude, float rangeKm)
{
    constexpr double kLatKmPerDegree = 111.0;
    constexpr double kLonKmPerDegreeEquator = 111.320;

    const double latitudeRadians = latitude * PI / 180.0;
    const double latDelta = static_cast<double>(rangeKm) / kLatKmPerDegree;
    const double lonScale = fmax(0.15, fabs(cos(latitudeRadians)));
    const double lonDelta = static_cast<double>(rangeKm) / (kLonKmPerDegreeEquator * lonScale);

    BoundingBox box;
    box.minLat = fmax(-90.0, latitude - latDelta);
    box.maxLat = fmin(90.0, latitude + latDelta);
    box.minLon = fmax(-180.0, longitude - lonDelta);
    box.maxLon = fmin(180.0, longitude + lonDelta);
    return box;
}


bool parseJsonNumberFieldFast(const String& json, const char* key, double& out)
{
    const int keyIndex = json.indexOf(key);
    if (keyIndex < 0) {
        return false;
    }
    int colon = json.indexOf(':', keyIndex + strlen(key));
    if (colon < 0) {
        return false;
    }
    ++colon;
    while (colon < static_cast<int>(json.length()) && isspace(static_cast<unsigned char>(json[colon]))) {
        ++colon;
    }
    if (colon >= static_cast<int>(json.length()) || json[colon] == 'n') {
        return false;
    }

    char* endPtr = nullptr;
    out = strtod(json.c_str() + colon, &endPtr);
    return endPtr != (json.c_str() + colon) && isfinite(out);
}

bool pointInsideBoundingBox(double lat, double lon, const BoundingBox& box)
{
    return lat >= box.minLat && lat <= box.maxLat && lon >= box.minLon && lon <= box.maxLon;
}

Plane parsePlane(JsonObjectConst acObject)
{
    Plane plane;
    plane.id = acObject["hex"] | "";
    plane.id.trim();
    plane.callsign = acObject["flight"] | "";
    plane.callsign.trim();
    plane.aircraftType = acObject["t"] | "";
    plane.aircraftType.trim();
    plane.squawk = acObject["squawk"] | "";
    plane.squawk.trim();
    plane.registration = acObject["r"] | "";
    plane.registration.trim();
    const String countryField = acObject["country"] | "";
    if (countryField.length() == 2) {
        const CountryResolver::CountryInfo fromIso = CountryResolver::countryFromIsoCode(countryField);
        if (strcmp(fromIso.code, "xy") != 0) {
            plane.countryCode = fromIso.code;
            plane.countryName = fromIso.name;
        } else {
            plane.countryName = countryField;
        }
    } else if (!countryField.isEmpty()) {
        plane.countryName = countryField;
        plane.countryName.trim();
    }
    const int dbFlags = acObject["dbFlags"] | 0;
    plane.military = (dbFlags & 1) != 0;
    plane.longitude = acObject["lon"] | 0.0;
    plane.latitude = acObject["lat"] | 0.0;

    double altFeet = 0.0;
    JsonVariantConst altBaro = acObject["alt_baro"];
    if (altBaro.is<float>() || altBaro.is<double>() || altBaro.is<long>() || altBaro.is<int>()) {
        altFeet = altBaro.as<double>();
    } else if (altBaro.is<const char*>()) {
        const char* altText = altBaro.as<const char*>();
        if (altText != nullptr && strcmp(altText, "ground") != 0) {
            altFeet = atof(altText);
        }
    } else {
        altFeet = acObject["alt_geom"] | 0.0;
    }

    plane.altitudeMeters = static_cast<float>(altFeet * 0.3048);
    const double groundSpeedKnots = acObject["gs"] | 0.0;
    plane.velocityMps = static_cast<float>(groundSpeedKnots * 0.514444);
    plane.headingDeg = acObject["track"] | 0.0f;

    const char* squawk = acObject["squawk"] | "";
    const char* emergency = acObject["emergency"] | "";
    plane.emergency =
        (strcmp(squawk, "7500") == 0 || strcmp(squawk, "7600") == 0 || strcmp(squawk, "7700") == 0)
        || (emergency[0] != '\0' && strcmp(emergency, "none") != 0);

    plane.valid = !plane.id.isEmpty();
    if (plane.valid && plane.callsign.isEmpty()) {
        plane.callsign = plane.id;
        plane.callsign.toUpperCase();
    }
    // El pais se resuelve una sola vez en RadarModel::applyFetchResult(),
    // despues de calcular distancia/rango y antes de guardar el contacto.
    return plane;
}

Plane parseOpenSkyState(JsonArrayConst state)
{
    Plane plane;
    if (state.size() < 17) {
        return plane;
    }

    plane.id = state[0] | "";
    plane.callsign = state[1] | "";
    plane.callsign.trim();
    plane.squawk = state[14] | "";
    plane.squawk.trim();
    plane.longitude = state[5] | 0.0;
    plane.latitude = state[6] | 0.0;

    if (!state[13].isNull()) {
        plane.altitudeMeters = state[13].as<float>();
    } else if (!state[7].isNull()) {
        plane.altitudeMeters = state[7].as<float>();
    }

    plane.velocityMps = state[9] | 0.0f;
    plane.headingDeg = state[10] | 0.0f;
    plane.valid = !plane.id.isEmpty();
    if (plane.valid && plane.callsign.isEmpty()) {
        plane.callsign = plane.id;
        plane.callsign.toUpperCase();
    }
    // El pais se resuelve una sola vez en RadarModel::applyFetchResult().
    return plane;
}

void addReadsbFilter(DynamicJsonDocument& filter, const char* key)
{
    JsonArray arrayFilter = filter.createNestedArray(key);
    JsonObject planeFilter = arrayFilter.createNestedObject();
    planeFilter["hex"] = true;
    planeFilter["flight"] = true;
    planeFilter["lat"] = true;
    planeFilter["lon"] = true;
    planeFilter["alt_baro"] = true;
    planeFilter["alt_geom"] = true;
    planeFilter["gs"] = true;
    planeFilter["track"] = true;
    planeFilter["t"] = true;
    planeFilter["squawk"] = true;
    planeFilter["emergency"] = true;
    planeFilter["r"] = true;
    planeFilter["country"] = true;
    planeFilter["dbFlags"] = true;
}

bool planeWithinRangeKm(const Plane& plane, double ownshipLat, double ownshipLon, float rangeKm)
{
    if (rangeKm <= 0.0f || isnan(ownshipLat) || isnan(ownshipLon)) {
        return true;
    }
    const double distanceKm = MathUtils::haversineDistanceKm(
        ownshipLat,
        ownshipLon,
        plane.latitude,
        plane.longitude);
    return distanceKm <= static_cast<double>(rangeKm);
}

bool parseReadsbPlaneObject(const String& objectJson, double ownshipLat, double ownshipLon,
    float rangeKm, FetchResult& result)
{
    ++result.rawStateCount;

    // Prefiltro muy barato antes de ArduinoJson: si no hay lat/lon o cae fuera
    // del bounding box, evitamos deserializar el objeto completo.
    double fastLat = 0.0;
    double fastLon = 0.0;
    if (!parseJsonNumberFieldFast(objectJson, "\"lat\"", fastLat)
        || !parseJsonNumberFieldFast(objectJson, "\"lon\"", fastLon)) {
        return true;
    }

    const bool filterByRange = rangeKm > 0.0f && !isnan(ownshipLat) && !isnan(ownshipLon);
    if (filterByRange) {
        const BoundingBox box = buildBoundingBox(ownshipLat, ownshipLon, rangeKm);
        if (!pointInsideBoundingBox(fastLat, fastLon, box)) {
            return true;
        }
    }

    DynamicJsonDocument doc(AppConfig::kReadsbPlaneJsonDocBytes);
    const DeserializationError error = deserializeJson(doc, objectJson);
    if (error) {
        return false;
    }

    JsonObjectConst acObject = doc.as<JsonObjectConst>();
    Plane plane = parsePlane(acObject);
    if (!plane.valid) {
        return true;
    }

    // Evita reconsultar lat/lon en caso de objetos raros: ya se validaron arriba.
    plane.latitude = fastLat;
    plane.longitude = fastLon;

    if (filterByRange && !planeWithinRangeKm(plane, ownshipLat, ownshipLon, rangeKm)) {
        return true;
    }

    ++result.validPositionCount;
    if (result.planes.size() < static_cast<size_t>(AppConfig::kMaxVisiblePlanes)) {
        result.planes.push_back(std::move(plane));
    }
    return true;
}

bool parseReadsbStream(Stream& stream, const String& sourceName, FetchResult& result,
    size_t /*jsonDocBytes*/, double ownshipLat, double ownshipLon, float rangeKm)
{
    // IMPORTANTE:
    // No parseamos el JSON completo en un DynamicJsonDocument grande. En ESP32,
    // con LVGL/framebuffers/HTTPS activos, reservar 160-256 KB suele acabar en
    // ArduinoJson::NoMemory aunque haya WiFi. En su lugar buscamos el array
    // "ac"/"aircraft" y deserializamos cada avión individualmente con un doc pequeño.
    constexpr size_t kWindowMax = 32;
    constexpr size_t kMaxObjectJsonBytes = AppConfig::kReadsbPlaneObjectBufferBytes;
    constexpr uint32_t kStreamIdleStopMs = 2500;

    result.sourceName = sourceName;
    result.rawStateCount = 0;
    result.validPositionCount = 0;
    result.planes.clear();
    result.planes.reserve(AppConfig::kMaxVisiblePlanes);

    String compactWindow;
    compactWindow.reserve(kWindowMax);

    String objectJson;
    objectJson.reserve(768);

    bool inAircraftArray = false;
    bool collectingObject = false;
    bool inString = false;
    bool escaped = false;
    bool objectOverflow = false;
    int braceDepth = 0;
    uint32_t skippedObjects = 0;
    uint32_t parseErrors = 0;
    uint32_t lastByteMs = millis();

    while ((millis() - lastByteMs) < kStreamIdleStopMs
        && result.planes.size() < static_cast<size_t>(AppConfig::kMaxVisiblePlanes)) {
        const int value = stream.read();
        if (value < 0) {
            delay(1);
            continue;
        }

        lastByteMs = millis();
        const char c = static_cast<char>(value);

        if (!inAircraftArray) {
            if (!isspace(static_cast<unsigned char>(c))) {
                compactWindow += c;
                if (compactWindow.length() > kWindowMax) {
                    compactWindow.remove(0, compactWindow.length() - kWindowMax);
                }
                if (compactWindow.endsWith("\"ac\":[") || compactWindow.endsWith("\"aircraft\":[")) {
                    inAircraftArray = true;
                }
            }
            continue;
        }

        if (!collectingObject) {
            if (c == '{') {
                collectingObject = true;
                inString = false;
                escaped = false;
                objectOverflow = false;
                braceDepth = 1;
                objectJson = "{";
            } else if (c == ']') {
                break;
            }
            continue;
        }

        if (objectJson.length() < kMaxObjectJsonBytes) {
            objectJson += c;
        } else {
            objectOverflow = true;
        }

        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = inString;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }

        if (c == '{') {
            ++braceDepth;
        } else if (c == '}') {
            --braceDepth;
            if (braceDepth == 0) {
                collectingObject = false;
                if (objectOverflow) {
                    ++skippedObjects;
                } else if (!parseReadsbPlaneObject(objectJson, ownshipLat, ownshipLon, rangeKm, result)) {
                    ++parseErrors;
                }
                objectJson = "";
            }
        }
    }

    if (!inAircraftArray) {
        result.ok = false;
        result.statusText = sourceName + " JSON no aircraft array";
        return false;
    }

    result.ok = true;
    result.statusText = !result.planes.empty()
        ? String(result.planes.size()) + " aircraft " + sourceName + " stream"
        : sourceName + " OK, no aircraft stream";
    if (skippedObjects > 0 || parseErrors > 0) {
        result.statusText += " skip=" + String(skippedObjects) + " parseErr=" + String(parseErrors);
    }
    return true;
}

bool fetchReadsbRadius(const String& sourceName, const String& url, FetchResult& result,
    size_t jsonDocBytes, double ownshipLat, double ownshipLon, float rangeKm)
{
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(kReadsbHttpTimeoutMs);
    http.setTimeout(kReadsbHttpTimeoutMs);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        result.sourceName = sourceName;
        result.statusText = sourceName + " begin failed";
        return false;
    }

    http.addHeader("User-Agent", "RadarEsp32/0.1");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    const int code = http.GET();
    if (code <= 0) {
        result.sourceName = sourceName;
        result.statusText = sourceName + " error " + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        result.sourceName = sourceName;
        result.statusText = sourceName + " HTTP " + String(code);
        http.end();
        return false;
    }

    const bool ok = parseReadsbStream(
        http.getStream(),
        sourceName,
        result,
        jsonDocBytes,
        ownshipLat,
        ownshipLon,
        rangeKm);
    http.end();
    return ok;
}

bool fetchAirplanesLivePoint(double ownshipLat, double ownshipLon, float rangeKm, FetchResult& result)
{
    const double rangeNm = static_cast<double>(rangeKm) * 0.539957;
    const int radiusNm = max(1, min(250, static_cast<int>(round(rangeNm))));
    const String url = String("https://api.airplanes.live/v2/point/")
        + String(ownshipLat, 6)
        + "/"
        + String(ownshipLon, 6)
        + "/"
        + String(radiusNm);

    return fetchReadsbRadius(
        AppConfig::kTrafficSourceAirplanesLive,
        url,
        result,
        AppConfig::kReadsbJsonDocBytes,
        ownshipLat,
        ownshipLon,
        rangeKm);
}

bool fetchAdsbLol(double ownshipLat, double ownshipLon, float rangeKm, FetchResult& result)
{
    const double rangeNm = static_cast<double>(rangeKm) * 0.539957;
    const int distNm = max(1, static_cast<int>(round(rangeNm)));
    const String url = String("https://api.adsb.lol/v2/lat/")
        + String(ownshipLat, 6)
        + "/lon/"
        + String(ownshipLon, 6)
        + "/dist/"
        + String(distNm);

    return fetchReadsbRadius(
        "adsb.lol",
        url,
        result,
        AppConfig::kReadsbJsonDocBytes,
        ownshipLat,
        ownshipLon,
        rangeKm);
}

bool fetchAdsbFi(double ownshipLat, double ownshipLon, float rangeKm, FetchResult& result)
{
    const double rangeNm = static_cast<double>(rangeKm) * 0.539957;
    const int distNm = max(1, static_cast<int>(round(rangeNm)));
    const String url = String("https://opendata.adsb.fi/api/v2/lat/")
        + String(ownshipLat, 6)
        + "/lon/"
        + String(ownshipLon, 6)
        + "/dist/"
        + String(distNm);

    return fetchReadsbRadius(
        "adsb.fi",
        url,
        result,
        AppConfig::kReadsbJsonDocBytes,
        ownshipLat,
        ownshipLon,
        rangeKm);
}

bool fetchOpenSky(double ownshipLat, double ownshipLon, float rangeKm, const String& previousReason, FetchResult& result)
{
    const BoundingBox box = buildBoundingBox(ownshipLat, ownshipLon, rangeKm);
    const String url = String("https://opensky-network.org/api/states/all?lamin=")
        + String(box.minLat, 6)
        + "&lomin="
        + String(box.minLon, 6)
        + "&lamax="
        + String(box.maxLat, 6)
        + "&lomax="
        + String(box.maxLon, 6);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(kOpenSkyHttpTimeoutMs);
    http.setTimeout(kOpenSkyHttpTimeoutMs);
    if (!http.begin(client, url)) {
        result.sourceName = "OpenSky";
        result.statusText = previousReason + " | OpenSky begin failed";
        return false;
    }

    http.addHeader("User-Agent", "RadarEsp32/0.1");
    const int code = http.GET();
    if (code <= 0) {
        result.sourceName = "OpenSky";
        result.statusText = previousReason + " | OpenSky error " + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        result.sourceName = "OpenSky";
        result.statusText = previousReason + " | OpenSky HTTP " + String(code);
        http.end();
        return false;
    }

    BasicJsonDocument<PsramJsonAllocator> doc(160 * 1024);
    const DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();

    if (error) {
        result.sourceName = "OpenSky";
        result.statusText = previousReason + " | OpenSky JSON " + error.c_str();
        return false;
    }

    JsonArray states = doc["states"].as<JsonArray>();
    result.sourceName = "OpenSky";
    result.rawStateCount = states.size();
    result.validPositionCount = 0;
    result.planes.clear();
    result.planes.reserve(std::min(static_cast<size_t>(result.rawStateCount),
        static_cast<size_t>(AppConfig::kMaxVisiblePlanes)));

    for (JsonArrayConst state : states) {
        if (state.size() < 17 || state[5].isNull() || state[6].isNull()) {
            continue;
        }

        Plane plane = parseOpenSkyState(state);
        if (!plane.valid) {
            continue;
        }
        if (!planeWithinRangeKm(plane, ownshipLat, ownshipLon, rangeKm)) {
            continue;
        }

        ++result.validPositionCount;
        result.planes.push_back(plane);
        if (result.planes.size() >= static_cast<size_t>(AppConfig::kMaxVisiblePlanes)) {
            break;
        }
    }

    result.ok = true;
    result.statusText = !result.planes.empty()
        ? String(result.planes.size()) + " aircraft OpenSky | fallback tras " + previousReason
        : previousReason + " | OpenSky OK, no aircraft";
    return true;
}

String weatherCodeText(int code)
{
    switch (code) {
        case 0: return "CLEAR SKY";
        case 1: return "MAINLY CLEAR";
        case 2: return "PARTLY CLOUDY";
        case 3: return "OVERCAST";
        case 45:
        case 48: return "FOG";
        case 51:
        case 53:
        case 55: return "DRIZZLE";
        case 56:
        case 57: return "FREEZING DRIZZLE";
        case 61:
        case 63:
        case 65: return "RAIN";
        case 66:
        case 67: return "FREEZING RAIN";
        case 71:
        case 73:
        case 75: return "SNOW";
        case 77: return "SNOW GRAINS";
        case 80:
        case 81:
        case 82: return "RAIN SHOWERS";
        case 85:
        case 86: return "SNOW SHOWERS";
        case 95: return "THUNDERSTORM";
        case 96:
        case 99: return "STORM HAIL";
        default: return "WEATHER";
    }
}

String metNoSymbolText(String symbol)
{
    symbol.toUpperCase();
    const int underscore = symbol.indexOf('_');
    if (underscore >= 0) {
        symbol = symbol.substring(0, underscore);
    }
    symbol.replace("-", "");
    if (symbol.indexOf("CLEAR") >= 0 || symbol.indexOf("FAIR") >= 0) return "CLEAR SKY";
    if (symbol.indexOf("PARTLY") >= 0) return "PARTLY CLOUDY";
    if (symbol.indexOf("CLOUDY") >= 0) return "CLOUDY";
    if (symbol.indexOf("FOG") >= 0) return "FOG";
    if (symbol.indexOf("THUNDER") >= 0) return "THUNDERSTORM";
    if (symbol.indexOf("SLEET") >= 0) return "SLEET";
    if (symbol.indexOf("SNOW") >= 0) return "SNOW";
    if (symbol.indexOf("RAIN") >= 0) return "RAIN";
    return "WEATHER";
}

bool fetchMetNoWeather(double latitude, double longitude, WeatherResult& result)
{
    const String url = String("https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=")
        + String(latitude, 6)
        + "&lon="
        + String(longitude, 6);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(7000);
    http.setTimeout(7000);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        result.statusText = "met.no begin failed";
        return false;
    }

    http.addHeader("User-Agent", "RadarEsp32Weather/1.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    const int code = http.GET();
    if (code <= 0) {
        result.statusText = "met.no error " + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        result.statusText = "met.no HTTP " + String(code);
        http.end();
        return false;
    }

    DynamicJsonDocument filter(768);
    JsonArray seriesFilter = filter["properties"].createNestedArray("timeseries");
    JsonObject first = seriesFilter.createNestedObject();
    first["data"]["instant"]["details"]["air_temperature"] = true;
    first["data"]["next_1_hours"]["summary"]["symbol_code"] = true;
    first["data"]["next_6_hours"]["summary"]["symbol_code"] = true;

    DynamicJsonDocument doc(3072);
    const DeserializationError error = deserializeJson(
        doc,
        http.getStream(),
        DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        result.statusText = String("met.no JSON ") + error.c_str();
        return false;
    }

    JsonObject firstPoint = doc["properties"]["timeseries"][0];
    JsonVariant temp = firstPoint["data"]["instant"]["details"]["air_temperature"];
    if (temp.isNull()) {
        result.statusText = "met.no no temp";
        return false;
    }

    String symbol = firstPoint["data"]["next_1_hours"]["summary"]["symbol_code"] | "";
    if (symbol.isEmpty()) {
        symbol = firstPoint["data"]["next_6_hours"]["summary"]["symbol_code"] | "";
    }

    result.temperatureC = temp.as<float>();
    result.weatherCode = 0;
    result.conditionText = symbol.isEmpty() ? String("WEATHER") : metNoSymbolText(symbol);
    result.ok = true;
    result.statusText = "met.no OK";
    return true;
}

bool fetchOpenMeteoCurrentWeather(double latitude, double longitude, WeatherResult& result)
{
    const String url = String("https://api.open-meteo.com/v1/forecast?latitude=")
        + String(latitude, 6)
        + "&longitude="
        + String(longitude, 6)
        + "&current_weather=true"
        + "&timezone=auto";

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        result.statusText = "open-meteo current_weather begin failed";
        return false;
    }

    http.addHeader("User-Agent", "RadarEsp32/0.1");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    const int code = http.GET();
    if (code <= 0) {
        result.statusText = "open-meteo current_weather error " + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        result.statusText = "open-meteo current_weather HTTP " + String(code);
        http.end();
        return false;
    }

    DynamicJsonDocument filter(256);
    filter["current_weather"]["temperature"] = true;
    filter["current_weather"]["weathercode"] = true;

    DynamicJsonDocument doc(768);
    const DeserializationError error = deserializeJson(
        doc,
        http.getStream(),
        DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        result.statusText = String("open-meteo current_weather JSON ") + error.c_str();
        return false;
    }

    result.temperatureC = doc["current_weather"]["temperature"] | 0.0f;
    result.weatherCode = doc["current_weather"]["weathercode"] | -1;
    result.conditionText = weatherCodeText(result.weatherCode);
    result.ok = result.weatherCode >= 0;
    result.statusText = result.ok ? "open-meteo current_weather OK" : "open-meteo current_weather no data";
    return result.ok;
}

template <typename TClient>
bool fetchOpenMeteoCurrentFieldsFromUrl(TClient& client, const String& url, const String& sourceName, WeatherResult& result)
{
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        result.statusText = sourceName + " begin failed";
        return false;
    }

    http.addHeader("User-Agent", "RadarEsp32/0.1");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    const int code = http.GET();
    if (code <= 0) {
        result.statusText = sourceName + " error " + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        result.statusText = sourceName + " HTTP " + String(code);
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();
    if (payload.isEmpty()) {
        result.statusText = sourceName + " empty response";
        return false;
    }

    DynamicJsonDocument filter(256);
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;

    DynamicJsonDocument doc(1024);
    const DeserializationError error = deserializeJson(
        doc,
        payload,
        DeserializationOption::Filter(filter));

    if (error) {
        result.statusText = sourceName + " JSON " + error.c_str();
        return false;
    }

    JsonVariant temp = doc["current"]["temperature_2m"];
    JsonVariant codeValue = doc["current"]["weather_code"];
    if (temp.isNull() || codeValue.isNull()) {
        result.statusText = sourceName + " no data";
        return false;
    }

    result.temperatureC = temp.as<float>();
    result.weatherCode = codeValue.as<int>();
    result.conditionText = weatherCodeText(result.weatherCode);
    result.ok = true;
    result.statusText = sourceName + " OK";
    return true;
}

bool fetchOpenMeteoCurrentFieldsHttp(double latitude, double longitude, WeatherResult& result)
{
    const String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
        + String(latitude, 6)
        + "&longitude="
        + String(longitude, 6)
        + "&current=temperature_2m,weather_code"
        + "&timezone=auto";

    WiFiClient client;
    return fetchOpenMeteoCurrentFieldsFromUrl(client, url, "open-meteo http current", result);
}

bool fetchOpenMeteoCurrentFields(double latitude, double longitude, WeatherResult& result)
{
    const String url = String("https://api.open-meteo.com/v1/forecast?latitude=")
        + String(latitude, 6)
        + "&longitude="
        + String(longitude, 6)
        + "&current=temperature_2m,weather_code"
        + "&timezone=auto";

    WiFiClientSecure client;
    client.setInsecure();
    return fetchOpenMeteoCurrentFieldsFromUrl(client, url, "open-meteo https current", result);
}

bool fetchWttrWeather(double latitude, double longitude, WeatherResult& result)
{
    const String url = String("http://wttr.in/")
        + String(latitude, 6)
        + ","
        + String(longitude, 6)
        + "?format=j1";

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        result.statusText = "wttr begin failed";
        return false;
    }

    http.addHeader("User-Agent", "RadarEsp32/0.1");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    const int code = http.GET();
    if (code <= 0) {
        result.statusText = "wttr error " + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        result.statusText = "wttr HTTP " + String(code);
        http.end();
        return false;
    }

    DynamicJsonDocument filter(384);
    JsonArray currentFilter = filter.createNestedArray("current_condition");
    JsonObject current0 = currentFilter.createNestedObject();
    current0["temp_C"] = true;
    JsonArray descFilter = current0.createNestedArray("weatherDesc");
    JsonObject desc0 = descFilter.createNestedObject();
    desc0["value"] = true;

    DynamicJsonDocument doc(1536);
    const DeserializationError error = deserializeJson(
        doc,
        http.getStream(),
        DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        result.statusText = String("wttr JSON ") + error.c_str();
        return false;
    }

    JsonObject current = doc["current_condition"][0];
    result.temperatureC = current["temp_C"].as<float>();
    result.weatherCode = 0;
    result.conditionText = current["weatherDesc"][0]["value"] | "WEATHER";
    result.conditionText.toUpperCase();
    result.ok = current["temp_C"].is<const char*>() || current["temp_C"].is<float>() || current["temp_C"].is<int>();
    result.statusText = result.ok ? "wttr OK" : "wttr no data";
    return result.ok;
}
}

FetchResult TrafficClient::fetchSnapshot(double ownshipLat, double ownshipLon, float rangeKm) const
{
    FetchResult result;
    result.sourceName = AppConfig::kTrafficSourceAirplanesLive;

    if (fetchAirplanesLivePoint(ownshipLat, ownshipLon, rangeKm, result)) {
        return result;
    }

    const String airplanesLiveReason = result.statusText;
    if (fetchAdsbFi(ownshipLat, ownshipLon, rangeKm, result)) {
        result.statusText += " | fallback tras " + airplanesLiveReason;
        return result;
    }

    const String adsbFiReason = result.statusText;
    if (fetchAdsbLol(ownshipLat, ownshipLon, rangeKm, result)) {
        result.statusText += " | fallback tras " + airplanesLiveReason + " | " + adsbFiReason;
        return result;
    }

    const String adsbLolReason = result.statusText;
    fetchOpenSky(ownshipLat, ownshipLon, rangeKm,
        airplanesLiveReason + " | " + adsbFiReason + " | " + adsbLolReason, result);
    return result;
}

WeatherResult TrafficClient::fetchWeather(double latitude, double longitude) const
{
    WeatherResult result;
    if (!isfinite(latitude) || !isfinite(longitude)) {
        result.statusText = "invalid weather coordinates";
        return result;
    }

    if (fetchOpenMeteoCurrentFieldsHttp(latitude, longitude, result)) {
        return result;
    }

    const String firstReason = result.statusText;
    if (fetchOpenMeteoCurrentFields(latitude, longitude, result)) {
        result.statusText += " | fallback tras " + firstReason;
        return result;
    }

    const String secondReason = result.statusText;
    if (fetchMetNoWeather(latitude, longitude, result)) {
        result.statusText += " | fallback tras " + firstReason + " | " + secondReason;
        return result;
    }

    const String thirdReason = result.statusText;
    if (fetchOpenMeteoCurrentWeather(latitude, longitude, result)) {
        result.statusText += " | fallback tras " + firstReason + " | " + secondReason + " | " + thirdReason;
        return result;
    }

    const String fourthReason = result.statusText;
    if (fetchWttrWeather(latitude, longitude, result)) {
        result.statusText += " | fallback tras " + firstReason + " | " + secondReason + " | " + thirdReason + " | " + fourthReason;
        return result;
    }

    result.statusText = firstReason + " | " + secondReason + " | " + thirdReason + " | " + fourthReason + " | " + result.statusText;
    return result;
}
