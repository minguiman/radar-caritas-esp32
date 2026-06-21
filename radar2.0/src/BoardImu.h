#pragma once

#include <Arduino.h>

class BoardImu
{
public:
    bool begin();
    void update();

    bool available() const { return m_available; }
    float tiltX() const { return m_tiltX; }
    float tiltY() const { return m_tiltY; }
    float rotationRad() const { return m_rotationRad; }

private:
    bool configure(uint8_t address);
    bool readRegisters(uint8_t reg, uint8_t* data, size_t length) const;
    bool writeRegister(uint8_t reg, uint8_t value) const;

    uint8_t m_address = 0;
    bool m_available = false;
    uint32_t m_lastSampleMs = 0;
    float m_tiltX = 0.0f;
    float m_tiltY = 0.0f;
    float m_rotationRad = 0.0f;
};
