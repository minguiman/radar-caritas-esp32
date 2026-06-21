#pragma once

#include "PlaneData.h"

namespace CountryResolver
{
struct CountryInfo
{
    const char* name;
    const char* code;
};

CountryInfo countryFromIcaoHex(const String& hex);
CountryInfo countryFromRegistration(const String& registration);
CountryInfo countryFromIsoCode(const String& code);
CountryInfo countryFromCountryText(const String& text);
String sanitizeHexId(const String& hex);
void applyToPlane(Plane& plane);
CountryInfo resolveCountry(const Plane& plane);
}
