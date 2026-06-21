#pragma once

namespace MathUtils
{
double degreesToRadians(double degrees);
double radiansToDegrees(double radians);
double haversineDistanceKm(double lat1, double lon1, double lat2, double lon2);
double initialBearingDeg(double lat1, double lon1, double lat2, double lon2);
int metersToFlightLevel(float altitudeMeters);
float metersToFeet(float altitudeMeters);
}

