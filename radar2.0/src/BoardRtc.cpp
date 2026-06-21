#include "BoardRtc.h"

#include <Arduino.h>
#include <driver/i2c.h>
#include <sys/time.h>

namespace
{
constexpr i2c_port_t kRtcI2cPort = I2C_NUM_0;
constexpr uint8_t kRtcAddress = 0x51;
constexpr TickType_t kI2cTimeoutTicks = pdMS_TO_TICKS(8);

constexpr uint8_t kRegControl1 = 0x00;
constexpr uint8_t kRegControl2 = 0x01;
constexpr uint8_t kRegSeconds = 0x04;
constexpr uint8_t kOscillatorStoppedMask = 0x80;

uint8_t bcdToDec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

uint8_t decToBcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool isLeapYear(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

int daysInMonth(int year, int month)
{
    static constexpr uint8_t kDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

bool utcPartsToEpoch(int year, int month, int day, int hour, int minute, int second, time_t& epochUtc)
{
    if (year < 2024 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > daysInMonth(year, month)) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;

    const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    epochUtc = static_cast<time_t>((days * 86400)
        + (static_cast<int64_t>(hour) * 3600)
        + (static_cast<int64_t>(minute) * 60)
        + second);
    return true;
}
}

bool BoardRtc::begin()
{
    uint8_t control[2] = {};
    m_available = readRegisters(kRegControl1, control, sizeof(control));
    if (!m_available) {
        Serial.println("WARN: PCF85063 RTC not detected on shared I2C0");
        return false;
    }

    writeRegister(kRegControl1, 0x00);
    writeRegister(kRegControl2, 0x00);
    Serial.println("PCF85063 RTC ready at 0x51");
    return true;
}

bool BoardRtc::readTime(time_t& epochUtc) const
{
    uint8_t raw[7] = {};
    if (!readRegisters(kRegSeconds, raw, sizeof(raw))) {
        return false;
    }
    if ((raw[0] & kOscillatorStoppedMask) != 0) {
        return false;
    }

    const int second = bcdToDec(raw[0] & 0x7F);
    const int minute = bcdToDec(raw[1] & 0x7F);
    const int hour = bcdToDec(raw[2] & 0x3F);
    const int day = bcdToDec(raw[3] & 0x3F);
    const int month = bcdToDec(raw[5] & 0x1F);
    const int year = 2000 + bcdToDec(raw[6]);

    return utcPartsToEpoch(year, month, day, hour, minute, second, epochUtc);
}

bool BoardRtc::writeTime(time_t epochUtc) const
{
    tm utcInfo{};
    if (gmtime_r(&epochUtc, &utcInfo) == nullptr) {
        return false;
    }

    const int year = utcInfo.tm_year + 1900;
    if (year < 2024 || year > 2099) {
        return false;
    }

    uint8_t raw[7] = {};
    raw[0] = decToBcd(static_cast<uint8_t>(utcInfo.tm_sec));
    raw[1] = decToBcd(static_cast<uint8_t>(utcInfo.tm_min));
    raw[2] = decToBcd(static_cast<uint8_t>(utcInfo.tm_hour));
    raw[3] = decToBcd(static_cast<uint8_t>(utcInfo.tm_mday));
    raw[4] = decToBcd(static_cast<uint8_t>(utcInfo.tm_wday));
    raw[5] = decToBcd(static_cast<uint8_t>(utcInfo.tm_mon + 1));
    raw[6] = decToBcd(static_cast<uint8_t>(year - 2000));

    return writeRegisters(kRegSeconds, raw, sizeof(raw));
}

bool BoardRtc::readRegisters(uint8_t reg, uint8_t* data, size_t length) const
{
    return i2c_master_write_read_device(
        kRtcI2cPort,
        kRtcAddress,
        &reg,
        1,
        data,
        length,
        kI2cTimeoutTicks) == ESP_OK;
}

bool BoardRtc::writeRegisters(uint8_t reg, const uint8_t* data, size_t length) const
{
    uint8_t payload[8] = {};
    if (length + 1 > sizeof(payload)) {
        return false;
    }
    payload[0] = reg;
    memcpy(payload + 1, data, length);
    return i2c_master_write_to_device(
        kRtcI2cPort,
        kRtcAddress,
        payload,
        length + 1,
        kI2cTimeoutTicks) == ESP_OK;
}

bool BoardRtc::writeRegister(uint8_t reg, uint8_t value) const
{
    return writeRegisters(reg, &value, 1);
}
