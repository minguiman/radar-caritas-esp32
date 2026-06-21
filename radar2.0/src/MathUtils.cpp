#include "MathUtils.h"

#include <cmath>

namespace
{
constexpr double kEarthRadiusKm = 6371.0;
constexpr double kPi = 3.14159265358979323846;
}

double MathUtils::degreesToRadians(double degrees)
{
    return degrees * (kPi / 180.0);
}

double MathUtils::radiansToDegrees(double radians)
{
    return radians * (180.0 / kPi);
}

double MathUtils::haversineDistanceKm(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1Rad = degreesToRadians(lat1);
    const double lat2Rad = degreesToRadians(lat2);
    const double deltaLat = degreesToRadians(lat2 - lat1);
    const double deltaLon = degreesToRadians(lon2 - lon1);

    const double a = std::sin(deltaLat / 2.0) * std::sin(deltaLat / 2.0)
        + std::cos(lat1Rad) * std::cos(lat2Rad)
        * std::sin(deltaLon / 2.0) * std::sin(deltaLon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusKm * c;
}

double MathUtils::initialBearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1Rad = degreesToRadians(lat1);
    const double lat2Rad = degreesToRadians(lat2);
    const double deltaLon = degreesToRadians(lon2 - lon1);

    const double y = std::sin(deltaLon) * std::cos(lat2Rad);
    const double x = std::cos(lat1Rad) * std::sin(lat2Rad)
        - std::sin(lat1Rad) * std::cos(lat2Rad) * std::cos(deltaLon);

    double bearing = radiansToDegrees(std::atan2(y, x));
    if (bearing < 0.0) {
        bearing += 360.0;
    }
    return bearing;
}

int MathUtils::metersToFlightLevel(float altitudeMeters)
{
    return static_cast<int>(metersToFeet(altitudeMeters) / 100.0f);
}

float MathUtils::metersToFeet(float altitudeMeters)
{
    return altitudeMeters * 3.28084f;
}

