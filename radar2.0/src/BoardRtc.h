#pragma once

#include <Arduino.h>
#include <time.h>

class BoardRtc
{
public:
    bool begin();
    bool readTime(time_t& epochUtc) const;
    bool writeTime(time_t epochUtc) const;

    bool available() const { return m_available; }

private:
    bool readRegisters(uint8_t reg, uint8_t* data, size_t length) const;
    bool writeRegisters(uint8_t reg, const uint8_t* data, size_t length) const;
    bool writeRegister(uint8_t reg, uint8_t value) const;

    bool m_available = false;
};
