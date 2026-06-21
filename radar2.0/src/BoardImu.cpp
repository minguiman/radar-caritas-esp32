#include "BoardImu.h"

#include <Arduino.h>
#include <driver/i2c.h>
#include <algorithm>
#include <cmath>

namespace
{
constexpr i2c_port_t kImuI2cPort = I2C_NUM_0;
constexpr TickType_t kI2cTimeoutTicks = pdMS_TO_TICKS(8);
constexpr uint8_t kImuAddressLow = 0x6B;
constexpr uint8_t kImuAddressHigh = 0x6A;

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegRevision = 0x01;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl3 = 0x04;
constexpr uint8_t kRegCtrl5 = 0x06;
constexpr uint8_t kRegCtrl6 = 0x07;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegAccXLow = 0x35;

constexpr uint8_t kCtrl1Config = 0x40;
constexpr uint8_t kCtrl2Acc4g60Hz = 0x17;
constexpr uint8_t kCtrl3Gyro64dps60Hz = 0x76;
constexpr uint8_t kCtrl5AccGyroLpf = 0x11;
constexpr uint8_t kCtrl6DisableAe = 0x00;
constexpr uint8_t kCtrl7Running = 0x43;

constexpr uint32_t kSampleIntervalMs = 50;
constexpr float kAccelScale4g = 4.0f / 32768.0f;
constexpr float kNormalTiltLimitG = 0.70f;
constexpr float kFilterAlpha = 0.18f;
constexpr float kRotationFilterAlpha = 0.16f;
constexpr float kFlatRotationReturnAlpha = 0.08f;
constexpr float kMinScreenPlaneGravityG = 0.22f;
constexpr float kUprightGravityAngleRad = 1.57079632679f;
constexpr float kRollTrimRad = 0.0f;
constexpr float kNormalTiltDirection = -1.0f;

float wrapRadians(float angle)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.0f;
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}
}

bool BoardImu::begin()
{
    m_available = configure(kImuAddressLow) || configure(kImuAddressHigh);
    if (m_available) {
        Serial.printf("IMU ready at 0x%02X\n", m_address);
        m_rotationRad = 0.0f;
        m_tiltX = 0.0f;
        m_tiltY = 0.0f;
    } else {
        Serial.println("WARN: QMI8658 not detected on shared I2C0");
    }
    return m_available;
}

void BoardImu::update()
{
    if (!m_available) {
        return;
    }

    const uint32_t nowMs = millis();
    if ((nowMs - m_lastSampleMs) < kSampleIntervalMs) {
        return;
    }
    m_lastSampleMs = nowMs;

    uint8_t raw[6] = {};
    if (!readRegisters(kRegAccXLow, raw, sizeof(raw))) {
        return;
    }

    const int16_t axRaw = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8) | raw[0]);
    const int16_t ayRaw = static_cast<int16_t>((static_cast<uint16_t>(raw[3]) << 8) | raw[2]);
    const int16_t azRaw = static_cast<int16_t>((static_cast<uint16_t>(raw[5]) << 8) | raw[4]);

    const float ax = static_cast<float>(axRaw) * kAccelScale4g;
    const float ay = static_cast<float>(ayRaw) * kAccelScale4g;
    const float az = static_cast<float>(azRaw) * kAccelScale4g;

    const float planeGravity = sqrtf((ax * ax) + (ay * ay));
    if (planeGravity > kMinScreenPlaneGravityG) {
        const float screenGravityAngle = atan2f(ax, ay);
        const float targetRotation = wrapRadians(kUprightGravityAngleRad - screenGravityAngle + kRollTrimRad);
        m_rotationRad += wrapRadians(targetRotation - m_rotationRad) * kRotationFilterAlpha;
    } else {
        m_rotationRad += wrapRadians(kRollTrimRad - m_rotationRad) * kFlatRotationReturnAlpha;
    }

    const float targetX = 0.0f;
    const float targetY = std::clamp((az * kNormalTiltDirection) / kNormalTiltLimitG, -1.0f, 1.0f);
    m_tiltX += (targetX - m_tiltX) * kFilterAlpha;
    m_tiltY += (targetY - m_tiltY) * kFilterAlpha;
}

bool BoardImu::configure(uint8_t address)
{
    m_address = address;

    uint8_t probe[2] = {};
    if (!readRegisters(kRegWhoAmI, probe, sizeof(probe))) {
        return false;
    }

    if ((probe[0] == 0x00 && probe[1] == 0x00) || (probe[0] == 0xFF && probe[1] == 0xFF)) {
        return false;
    }

    if (!writeRegister(kRegCtrl1, kCtrl1Config)) {
        return false;
    }
    delay(2);
    if (!writeRegister(kRegCtrl7, kCtrl7Running)) {
        return false;
    }
    if (!writeRegister(kRegCtrl6, kCtrl6DisableAe)) {
        return false;
    }
    if (!writeRegister(kRegCtrl2, kCtrl2Acc4g60Hz)) {
        return false;
    }
    if (!writeRegister(kRegCtrl3, kCtrl3Gyro64dps60Hz)) {
        return false;
    }
    if (!writeRegister(kRegCtrl5, kCtrl5AccGyroLpf)) {
        return false;
    }
    delay(10);

    uint8_t verify[7] = {};
    if (!readRegisters(kRegCtrl1, verify, sizeof(verify))) {
        return false;
    }

    Serial.printf("QMI8658 probe who=0x%02X rev=0x%02X addr=0x%02X\n",
        probe[0], probe[1], address);
    Serial.printf("QMI8658 ctrl1=0x%02X ctrl2=0x%02X ctrl3=0x%02X ctrl5=0x%02X ctrl6=0x%02X ctrl7=0x%02X\n",
        verify[0], verify[1], verify[2], verify[4], verify[5], verify[6]);
    return true;
}

bool BoardImu::readRegisters(uint8_t reg, uint8_t* data, size_t length) const
{
    return i2c_master_write_read_device(
        kImuI2cPort,
        m_address,
        &reg,
        1,
        data,
        length,
        kI2cTimeoutTicks) == ESP_OK;
}

bool BoardImu::writeRegister(uint8_t reg, uint8_t value) const
{
    uint8_t payload[2] = {reg, value};
    return i2c_master_write_to_device(
        kImuI2cPort,
        m_address,
        payload,
        sizeof(payload),
        kI2cTimeoutTicks) == ESP_OK;
}
