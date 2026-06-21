#include "CountryResolver.h"

#include <cstdlib>

namespace
{
using CountryInfo = CountryResolver::CountryInfo;

struct IcaoRangeEntry
{
    uint32_t start;
    uint32_t end;
    const char* name;
    const char* code;
};

constexpr CountryInfo kUnknownCountry{"Unknown", "xy"};

// Tabla ICAO 24-bit address ranges (misma lógica que TinyRadar IcaoRange).
constexpr IcaoRangeEntry kIcaoRanges[] = {
    {0x004000, 0x0043FF, "Zimbabwe", "zw"},
    {0x006000, 0x006FFF, "Mozambique", "mz"},
    {0x008000, 0x00FFFF, "South Africa", "za"},
    {0x010000, 0x017FFF, "Egypt", "eg"},
    {0x018000, 0x01FFFF, "Libya", "ly"},
    {0x020000, 0x027FFF, "Morocco", "ma"},
    {0x028000, 0x02FFFF, "Tunisia", "tn"},
    {0x030000, 0x0303FF, "Botswana", "bw"},
    {0x032000, 0x032FFF, "Burundi", "bi"},
    {0x034000, 0x034FFF, "Cameroon", "cm"},
    {0x035000, 0x0353FF, "Comoros", "km"},
    {0x036000, 0x036FFF, "Congo", "cg"},
    {0x038000, 0x038FFF, "Cote d'Ivoire", "ci"},
    {0x03E000, 0x03EFFF, "Gabon", "ga"},
    {0x040000, 0x040FFF, "Ethiopia", "et"},
    {0x042000, 0x042FFF, "Equatorial Guinea", "gq"},
    {0x044000, 0x044FFF, "Ghana", "gh"},
    {0x046000, 0x046FFF, "Guinea", "gn"},
    {0x048000, 0x0483FF, "Guinea-Bissau", "gw"},
    {0x04A000, 0x04A3FF, "Lesotho", "ls"},
    {0x04C000, 0x04CFFF, "Kenya", "ke"},
    {0x050000, 0x050FFF, "Liberia", "lr"},
    {0x054000, 0x054FFF, "Madagascar", "mg"},
    {0x058000, 0x058FFF, "Malawi", "mw"},
    {0x05A000, 0x05A3FF, "Maldives", "mv"},
    {0x05C000, 0x05CFFF, "Mali", "ml"},
    {0x05E000, 0x05E3FF, "Mauritania", "mr"},
    {0x060000, 0x0603FF, "Mauritius", "mu"},
    {0x062000, 0x062FFF, "Niger", "ne"},
    {0x064000, 0x064FFF, "Nigeria", "ng"},
    {0x068000, 0x068FFF, "Uganda", "ug"},
    {0x06A000, 0x06A3FF, "Qatar", "qa"},
    {0x06C000, 0x06CFFF, "Central African Republic", "cf"},
    {0x06E000, 0x06EFFF, "Rwanda", "rw"},
    {0x070000, 0x070FFF, "Senegal", "sn"},
    {0x074000, 0x0743FF, "Seychelles", "sc"},
    {0x076000, 0x0763FF, "Sierra Leone", "sl"},
    {0x078000, 0x078FFF, "Somalia", "so"},
    {0x07A000, 0x07A3FF, "Eswatini", "sz"},
    {0x07C000, 0x07CFFF, "Sudan", "sd"},
    {0x080000, 0x080FFF, "Tanzania", "tz"},
    {0x084000, 0x084FFF, "Chad", "td"},
    {0x088000, 0x088FFF, "Togo", "tg"},
    {0x08A000, 0x08AFFF, "Zambia", "zm"},
    {0x08C000, 0x08CFFF, "DR Congo", "cd"},
    {0x090000, 0x090FFF, "Angola", "ao"},
    {0x094000, 0x0943FF, "Benin", "bj"},
    {0x096000, 0x0963FF, "Cabo Verde", "cv"},
    {0x098000, 0x0983FF, "Djibouti", "dj"},
    {0x09A000, 0x09AFFF, "Gambia", "gm"},
    {0x09C000, 0x09CFFF, "Burkina Faso", "bf"},
    {0x09E000, 0x09E3FF, "Sao Tome and Principe", "st"},
    {0x0A0000, 0x0A7FFF, "Algeria", "dz"},
    {0x0A8000, 0x0A8FFF, "Bahamas", "bs"},
    {0x0AA000, 0x0AA3FF, "Barbados", "bb"},
    {0x0AB000, 0x0AB3FF, "Belize", "bz"},
    {0x0AC000, 0x0ACFFF, "Colombia", "co"},
    {0x0AE000, 0x0AEFFF, "Costa Rica", "cr"},
    {0x0B0000, 0x0B0FFF, "Cuba", "cu"},
    {0x0B2000, 0x0B2FFF, "El Salvador", "sv"},
    {0x0B4000, 0x0B4FFF, "Guatemala", "gt"},
    {0x0B6000, 0x0B6FFF, "Guyana", "gy"},
    {0x0B8000, 0x0B8FFF, "Haiti", "ht"},
    {0x0BA000, 0x0BAFFF, "Honduras", "hn"},
    {0x0BC000, 0x0BC3FF, "Saint Vincent", "vc"},
    {0x0BE000, 0x0BEFFF, "Jamaica", "jm"},
    {0x0C0000, 0x0C0FFF, "Nicaragua", "ni"},
    {0x0C2000, 0x0C2FFF, "Panama", "pa"},
    {0x0C4000, 0x0C4FFF, "Dominican Republic", "do"},
    {0x0C6000, 0x0C6FFF, "Trinidad and Tobago", "tt"},
    {0x0C8000, 0x0C8FFF, "Suriname", "sr"},
    {0x0CA000, 0x0CA3FF, "Antigua and Barbuda", "ag"},
    {0x0CC000, 0x0CC3FF, "Grenada", "gd"},
    {0x0D0000, 0x0D7FFF, "Mexico", "mx"},
    {0x0D8000, 0x0DFFFF, "Venezuela", "ve"},
    {0x100000, 0x1FFFFF, "Russia", "ru"},
    {0x201000, 0x2013FF, "Namibia", "na"},
    {0x202000, 0x2023FF, "Eritrea", "er"},
    {0x300000, 0x33FFFF, "Italy", "it"},
    {0x340000, 0x37FFFF, "Spain", "es"},
    {0x380000, 0x3BFFFF, "France", "fr"},
    {0x3C0000, 0x3FFFFF, "Germany", "de"},
    {0x400000, 0x4001BF, "Bermuda", "bm"},
    {0x4001C0, 0x4001FF, "Cayman Islands", "ky"},
    {0x400300, 0x4003FF, "Turks and Caicos", "tc"},
    {0x424135, 0x4241F2, "Cayman Islands", "ky"},
    {0x424200, 0x4246FF, "Bermuda", "bm"},
    {0x424700, 0x424899, "Cayman Islands", "ky"},
    {0x424B00, 0x424BFF, "Isle of Man", "im"},
    {0x43BE00, 0x43BEFF, "Bermuda", "bm"},
    {0x43E700, 0x43EAFD, "Isle of Man", "im"},
    {0x43EAFE, 0x43EEFF, "Guernsey", "gg"},
    {0x400000, 0x43FFFF, "United Kingdom", "gb"},
    {0x440000, 0x447FFF, "Austria", "at"},
    {0x448000, 0x44FFFF, "Belgium", "be"},
    {0x450000, 0x457FFF, "Bulgaria", "bg"},
    {0x458000, 0x45FFFF, "Denmark", "dk"},
    {0x460000, 0x467FFF, "Finland", "fi"},
    {0x468000, 0x46FFFF, "Greece", "gr"},
    {0x470000, 0x477FFF, "Hungary", "hu"},
    {0x478000, 0x47FFFF, "Norway", "no"},
    {0x480000, 0x487FFF, "Netherlands", "nl"},
    {0x488000, 0x48FFFF, "Poland", "pl"},
    {0x490000, 0x497FFF, "Portugal", "pt"},
    {0x498000, 0x49FFFF, "Czechia", "cz"},
    {0x4A0000, 0x4A7FFF, "Romania", "ro"},
    {0x4A8000, 0x4AFFFF, "Sweden", "se"},
    {0x4B0000, 0x4B7FFF, "Switzerland", "ch"},
    {0x4B8000, 0x4BFFFF, "Turkey", "tr"},
    {0x4C0000, 0x4C7FFF, "Serbia", "rs"},
    {0x4C8000, 0x4C83FF, "Cyprus", "cy"},
    {0x4CA000, 0x4CAFFF, "Ireland", "ie"},
    {0x4CC000, 0x4CCFFF, "Iceland", "is"},
    {0x4D0000, 0x4D03FF, "Luxembourg", "lu"},
    {0x4D2000, 0x4D2FFF, "Malta", "mt"},
    {0x4D4000, 0x4D43FF, "Monaco", "mc"},
    {0x500000, 0x5003FF, "San Marino", "sm"},
    {0x501000, 0x5013FF, "Albania", "al"},
    {0x501C00, 0x501FFF, "Croatia", "hr"},
    {0x502C00, 0x502FFF, "Latvia", "lv"},
    {0x503C00, 0x503FFF, "Lithuania", "lt"},
    {0x504C00, 0x504FFF, "Moldova", "md"},
    {0x505C00, 0x505FFF, "Slovakia", "sk"},
    {0x506C00, 0x506FFF, "Slovenia", "si"},
    {0x507C00, 0x507FFF, "Uzbekistan", "uz"},
    {0x508000, 0x50FFFF, "Ukraine", "ua"},
    {0x510000, 0x5103FF, "Belarus", "by"},
    {0x511000, 0x5113FF, "Estonia", "ee"},
    {0x512000, 0x5123FF, "North Macedonia", "mk"},
    {0x513000, 0x5133FF, "Bosnia", "ba"},
    {0x514000, 0x5143FF, "Georgia", "ge"},
    {0x515000, 0x5153FF, "Tajikistan", "tj"},
    {0x516000, 0x5163FF, "Montenegro", "me"},
    {0x600000, 0x6003FF, "Armenia", "am"},
    {0x600800, 0x600BFF, "Azerbaijan", "az"},
    {0x601000, 0x6013FF, "Kyrgyzstan", "kg"},
    {0x601800, 0x601BFF, "Turkmenistan", "tm"},
    {0x680000, 0x6803FF, "Bhutan", "bt"},
    {0x681000, 0x6813FF, "Micronesia", "fm"},
    {0x682000, 0x6823FF, "Mongolia", "mn"},
    {0x683000, 0x6833FF, "Kazakhstan", "kz"},
    {0x684000, 0x6843FF, "Palau", "pw"},
    {0x700000, 0x700FFF, "Afghanistan", "af"},
    {0x702000, 0x702FFF, "Bangladesh", "bd"},
    {0x704000, 0x704FFF, "Myanmar", "mm"},
    {0x706000, 0x706FFF, "Kuwait", "kw"},
    {0x708000, 0x708FFF, "Laos", "la"},
    {0x70A000, 0x70AFFF, "Nepal", "np"},
    {0x70C000, 0x70C3FF, "Oman", "om"},
    {0x70E000, 0x70EFFF, "Cambodia", "kh"},
    {0x710000, 0x717FFF, "Saudi Arabia", "sa"},
    {0x718000, 0x71FFFF, "South Korea", "kr"},
    {0x720000, 0x727FFF, "North Korea", "kp"},
    {0x728000, 0x72FFFF, "Iraq", "iq"},
    {0x730000, 0x737FFF, "Iran", "ir"},
    {0x738000, 0x73FFFF, "Israel", "il"},
    {0x740000, 0x747FFF, "Jordan", "jo"},
    {0x748000, 0x74FFFF, "Lebanon", "lb"},
    {0x750000, 0x757FFF, "Malaysia", "my"},
    {0x758000, 0x75FFFF, "Philippines", "ph"},
    {0x760000, 0x767FFF, "Pakistan", "pk"},
    {0x768000, 0x76FFFF, "Singapore", "sg"},
    {0x770000, 0x777FFF, "Sri Lanka", "lk"},
    {0x778000, 0x77FFFF, "Syria", "sy"},
    {0x789000, 0x789FFF, "Hong Kong", "hk"},
    {0x780000, 0x7BFFFF, "China", "cn"},
    {0x7C0000, 0x7FFFFF, "Australia", "au"},
    {0x800000, 0x83FFFF, "India", "in"},
    {0x840000, 0x87FFFF, "Japan", "jp"},
    {0x880000, 0x887FFF, "Thailand", "th"},
    {0x888000, 0x88FFFF, "Vietnam", "vn"},
    {0x890000, 0x890FFF, "Yemen", "ye"},
    {0x894000, 0x894FFF, "Bahrain", "bh"},
    {0x895000, 0x8953FF, "Brunei", "bn"},
    {0x896000, 0x896FFF, "UAE", "ae"},
    {0x897000, 0x8973FF, "Solomon Islands", "sb"},
    {0x898000, 0x898FFF, "Papua New Guinea", "pg"},
    {0x899000, 0x8993FF, "Taiwan", "tw"},
    {0x8A0000, 0x8A7FFF, "Indonesia", "id"},
    {0x900000, 0x9003FF, "Marshall Islands", "mh"},
    {0x901000, 0x9013FF, "Cook Islands", "ck"},
    {0xC89000, 0xC89FFF, "Samoa", "ws"},
    {0xA00000, 0xAFFFFF, "United States", "us"},
    {0xC00000, 0xC3FFFF, "Canada", "ca"},
    {0xC80000, 0xC87FFF, "New Zealand", "nz"},
    {0xC88000, 0xC88FFF, "Fiji", "fj"},
    {0xC8A000, 0xC8A3FF, "Nauru", "nr"},
    {0xC8C000, 0xC8C3FF, "Saint Lucia", "lc"},
    {0xC8D000, 0xC8D3FF, "Tonga", "to"},
    {0xC8E000, 0xC8E3FF, "Kiribati", "ki"},
    {0xC90000, 0xC903FF, "Vanuatu", "vu"},
    {0xE00000, 0xE3FFFF, "Argentina", "ar"},
    {0xE40000, 0xE7FFFF, "Brazil", "br"},
    {0xE80000, 0xE80FFF, "Chile", "cl"},
    {0xE84000, 0xE84FFF, "Ecuador", "ec"},
    {0xE88000, 0xE88FFF, "Paraguay", "py"},
    {0xE8C000, 0xE8CFFF, "Peru", "pe"},
    {0xE90000, 0xE90FFF, "Uruguay", "uy"},
    {0xE94000, 0xE94FFF, "Bolivia", "bo"},
    {0xF00000, 0xF07FFF, "ICAO temp", "xy"},
    {0xF09000, 0xF093FF, "ICAO special", "xy"},
};

struct RegPrefixEntry
{
    const char* prefix;
    const char* name;
    const char* code;
};

constexpr RegPrefixEntry kRegPrefixes[] = {
    {"EC-", "Spain", "es"},
    {"F-", "France", "fr"},
    {"G-", "United Kingdom", "gb"},
    {"D-", "Germany", "de"},
    {"I-", "Italy", "it"},
    {"PH-", "Netherlands", "nl"},
    {"CS-", "Portugal", "pt"},
    {"OO-", "Belgium", "be"},
    {"HB-", "Switzerland", "ch"},
    {"OE-", "Austria", "at"},
    {"SE-", "Sweden", "se"},
    {"OH-", "Finland", "fi"},
    {"OY-", "Denmark", "dk"},
    {"LN-", "Norway", "no"},
    {"SP-", "Poland", "pl"},
    {"OK-", "Czechia", "cz"},
    {"HA-", "Hungary", "hu"},
    {"YR-", "Romania", "ro"},
    {"LZ-", "Bulgaria", "bg"},
    {"SX-", "Greece", "gr"},
    {"TC-", "Turkey", "tr"},
    {"EI-", "Ireland", "ie"},
    {"LX-", "Luxembourg", "lu"},
    {"9H-", "Malta", "mt"},
    {"N", "United States", "us"},
    {"C-", "Canada", "ca"},
    {"VH-", "Australia", "au"},
    {"ZK-", "New Zealand", "nz"},
    {"JA-", "Japan", "jp"},
    {"B-", "China/Taiwan", "cn"},
    {"HL-", "South Korea", "kr"},
    {"HS-", "Thailand", "th"},
    {"9V-", "Singapore", "sg"},
    {"VT-", "India", "in"},
    {"LV-", "Argentina", "ar"},
    {"PP-", "Brazil", "br"},
    {"CC-", "Chile", "cl"},
    {"XA-", "Mexico", "mx"},
    {"HK-", "Colombia", "co"},
};

CountryInfo lookupIcao(uint32_t code)
{
    for (const IcaoRangeEntry& entry : kIcaoRanges) {
        if (code >= entry.start && code <= entry.end) {
            return CountryInfo{entry.name, entry.code};
        }
    }
    return kUnknownCountry;
}

char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

CountryInfo lookupIsoCode(const char* code)
{
    if (code == nullptr || code[0] == '\0' || code[1] == '\0') {
        return kUnknownCountry;
    }

    const char normalized[3] = {
        lowerAscii(code[0]),
        lowerAscii(code[1]),
        '\0'};

    for (const IcaoRangeEntry& entry : kIcaoRanges) {
        if (entry.code[0] == normalized[0] && entry.code[1] == normalized[1]) {
            return CountryInfo{entry.name, entry.code};
        }
    }
    return kUnknownCountry;
}

struct CountryAliasEntry
{
    const char* alias;
    const char* code;
};

constexpr CountryAliasEntry kCountryAliases[] = {
    {"es", "es"}, {"esp", "es"}, {"spain", "es"}, {"espana", "es"},
    {"fr", "fr"}, {"fra", "fr"}, {"france", "fr"},
    {"de", "de"}, {"deu", "de"}, {"germany", "de"}, {"alemania", "de"},
    {"it", "it"}, {"ita", "it"}, {"italy", "it"}, {"italia", "it"},
    {"gb", "gb"}, {"gbr", "gb"}, {"uk", "gb"}, {"united kingdom", "gb"},
    {"us", "us"}, {"usa", "us"}, {"united states", "us"},
    {"ie", "ie"}, {"irl", "ie"}, {"ireland", "ie"},
    {"pt", "pt"}, {"prt", "pt"}, {"portugal", "pt"},
    {"nl", "nl"}, {"nld", "nl"}, {"netherlands", "nl"},
    {"be", "be"}, {"bel", "be"}, {"belgium", "be"},
    {"ch", "ch"}, {"che", "ch"}, {"switzerland", "ch"},
    {"at", "at"}, {"aut", "at"}, {"austria", "at"},
    {"pl", "pl"}, {"pol", "pl"}, {"poland", "pl"},
    {"se", "se"}, {"swe", "se"}, {"sweden", "se"},
    {"no", "no"}, {"nor", "no"}, {"norway", "no"},
    {"dk", "dk"}, {"dnk", "dk"}, {"denmark", "dk"},
    {"fi", "fi"}, {"fin", "fi"}, {"finland", "fi"},
    {"gr", "gr"}, {"grc", "gr"}, {"greece", "gr"},
    {"tr", "tr"}, {"tur", "tr"}, {"turkey", "tr"},
    {"ru", "ru"}, {"rus", "ru"}, {"russia", "ru"},
    {"ua", "ua"}, {"ukr", "ua"}, {"ukraine", "ua"},
    {"mx", "mx"}, {"mex", "mx"}, {"mexico", "mx"},
    {"br", "br"}, {"bra", "br"}, {"brazil", "br"},
    {"ar", "ar"}, {"arg", "ar"}, {"argentina", "ar"},
    {"ca", "ca"}, {"can", "ca"}, {"canada", "ca"},
    {"au", "au"}, {"aus", "au"}, {"australia", "au"},
    {"nz", "nz"}, {"nzl", "nz"}, {"new zealand", "nz"},
    {"jp", "jp"}, {"jpn", "jp"}, {"japan", "jp"},
    {"cn", "cn"}, {"chn", "cn"}, {"china", "cn"},
    {"in", "in"}, {"ind", "in"}, {"india", "in"},
    {"ae", "ae"}, {"are", "ae"}, {"uae", "ae"}, {"united arab emirates", "ae"},
};
}

String CountryResolver::sanitizeHexId(const String& hex)
{
    String sanitized;
    sanitized.reserve(hex.length());
    for (size_t i = 0; i < hex.length(); ++i) {
        const char c = hex[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            sanitized += c;
        }
    }
    return sanitized;
}

CountryInfo CountryResolver::countryFromIcaoHex(const String& hex)
{
    const String sanitized = sanitizeHexId(hex);
    if (sanitized.isEmpty()) {
        return kUnknownCountry;
    }
    const uint32_t code = static_cast<uint32_t>(strtoul(sanitized.c_str(), nullptr, 16));
    return lookupIcao(code);
}

CountryInfo CountryResolver::countryFromIsoCode(const String& code)
{
    return lookupIsoCode(code.c_str());
}

CountryInfo CountryResolver::countryFromCountryText(const String& text)
{
    if (text.isEmpty()) {
        return kUnknownCountry;
    }

    String normalized = text;
    normalized.trim();
    if (normalized.length() == 2) {
        return lookupIsoCode(normalized.c_str());
    }

    normalized.toLowerCase();
    for (const CountryAliasEntry& alias : kCountryAliases) {
        if (normalized.equals(alias.alias)) {
            return lookupIsoCode(alias.code);
        }
    }

    for (const IcaoRangeEntry& entry : kIcaoRanges) {
        if (normalized.equalsIgnoreCase(entry.name)) {
            return CountryInfo{entry.name, entry.code};
        }
    }

    return kUnknownCountry;
}

String normalizeRegistration(const String& registration)
{
    String reg = registration;
    reg.trim();
    reg.toUpperCase();
    if (reg.isEmpty() || reg.indexOf('-') >= 0) {
        return reg;
    }

    static const char* kDashAfter2[] = {
        "EC", "EI", "PH", "CS", "OO", "HB", "OE", "SE", "OH", "OY", "LN", "SP", "OK", "HA", "YR",
        "LZ", "SX", "TC", "LX", "9H", "VH", "ZK", "JA", "HL", "HS", "9V", "VT", "LV", "PP", "CC",
        "XA", "HK", nullptr,
    };
    for (int i = 0; kDashAfter2[i] != nullptr; ++i) {
        const String prefix = kDashAfter2[i];
        if (reg.startsWith(prefix) && reg.length() > static_cast<unsigned int>(prefix.length())) {
            return prefix + "-" + reg.substring(prefix.length());
        }
    }

    return reg;
}

CountryInfo CountryResolver::countryFromRegistration(const String& registration)
{
    if (registration.isEmpty()) {
        return kUnknownCountry;
    }

    String reg = normalizeRegistration(registration);

    for (const RegPrefixEntry& entry : kRegPrefixes) {
        const String prefix = entry.prefix;
        if (reg.startsWith(prefix)) {
            return CountryInfo{entry.name, entry.code};
        }
    }

    return kUnknownCountry;
}

CountryInfo CountryResolver::resolveCountry(const Plane& plane)
{
    // Si la API ya nos ha dado codigo ISO y es valido, no rehacemos matricula/rango ICAO.
    // Esto evita trabajo repetido en popup, banderas y repintados.
    if (!plane.countryCode.isEmpty()) {
        const CountryInfo fromCode = countryFromIsoCode(plane.countryCode);
        if (strcmp(fromCode.code, "xy") != 0) {
            return fromCode;
        }
    }

    const CountryInfo fromReg = countryFromRegistration(plane.registration);
    if (strcmp(fromReg.code, "xy") != 0) {
        return fromReg;
    }

    const CountryInfo fromHex = countryFromIcaoHex(sanitizeHexId(plane.id));
    if (strcmp(fromHex.code, "xy") != 0) {
        return fromHex;
    }

    return countryFromCountryText(plane.countryName);
}

void CountryResolver::applyToPlane(Plane& plane)
{
    const CountryInfo chosen = resolveCountry(plane);

    if (strcmp(chosen.code, "xy") != 0) {
        plane.countryName = chosen.name;
        plane.countryCode = chosen.code;
        return;
    }

    if (plane.countryName.isEmpty()) {
        plane.countryName = kUnknownCountry.name;
    }
    if (plane.countryCode.isEmpty()) {
        plane.countryCode = kUnknownCountry.code;
    }
}
