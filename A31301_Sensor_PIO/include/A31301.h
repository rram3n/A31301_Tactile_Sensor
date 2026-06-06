#ifndef A31301_H
#define A31301_H

#include <Wire.h>

class A31301 {
public:
    A31301(uint8_t i2c_address);  // Constructor with only I2C address
    void begin(TwoWire &wireBus);  // Begin method to set the TwoWire reference
    bool isConnected();
    int16_t readX();
    int16_t readY();
    int16_t readZ();
    float calculateXYAngle();
    float calculateZAngle();
    float readMagnitude();
    float readAngle();
    void changeI2CAddress(uint8_t new_address);
    uint8_t getAddress();
    void setCordicSelDir(uint8_t value);

private:
    uint8_t i2c_address;
    TwoWire* wire;  // Pointer to TwoWire instance
    int16_t readRegister(uint8_t msb_reg, uint8_t lsb_reg);
    void writeRegister(uint8_t reg, uint8_t value);
    int16_t readRegister16(uint8_t reg);
    void writeRegister16(uint8_t reg, uint16_t value);
    void writeRegister32(uint8_t reg, uint32_t value);

    int16_t x, y, z;
};

#endif