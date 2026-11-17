#include "A31301.h"
#include <Wire.h>

A31301::A31301(uint8_t i2c_address)
    : i2c_address(i2c_address), wire(nullptr), x(0), y(0), z(0) {}

void A31301::begin(TwoWire &wireBus) {
    wire = &wireBus;
}

bool A31301::isConnected() {
    wire->beginTransmission(i2c_address);
    return (wire->endTransmission() == 0);
}

int16_t A31301::readX() {
    int16_t x = readRegister(0x1E, 0x1F);
    if (x & 0x4000) {  // Check if the 15th bit is set
        x |= 0x8000;   // Set the 16th bit to 1 (negative sign)
    }
    return x;
}

int16_t A31301::readY() {
    int16_t y = readRegister(0x20, 0x21);
    if (y & 0x4000) {  // Check if the 15th bit is set
        y |= 0x8000;   // Set the 16th bit to 1 (negative sign)
    }
    return y;
}

int16_t A31301::readZ() {
    int16_t z = readRegister(0x22, 0x23);
    if (z & 0x4000) {  // Check if the 15th bit is set
        z |= 0x8000;   // Set the 16th bit to 1 (negative sign)
    }
    return z;
}

int16_t A31301::readRegister(uint8_t msb_reg, uint8_t lsb_reg) {
    wire->beginTransmission(i2c_address);
    wire->write(msb_reg);
    wire->endTransmission(true);
    wire->requestFrom(i2c_address, (uint8_t)1);
    uint8_t msb = wire->read();

    wire->beginTransmission(i2c_address);
    wire->write(lsb_reg);
    wire->endTransmission(true);
    wire->requestFrom(i2c_address, (uint8_t)1);
    uint8_t lsb = wire->read();

    return (int16_t)((msb << 8) | lsb);
}

void A31301::writeRegister16(uint8_t reg, uint16_t value) {
    wire->beginTransmission(i2c_address);
    wire->write(reg);
    wire->write(value & 0xFF);       // Write LSB
    wire->write((value >> 8) & 0xFF); // Write MSB
    wire->endTransmission();
}

void A31301::writeRegister32(uint8_t reg, uint32_t value) {
    wire->beginTransmission(i2c_address);
    wire->write(reg);
    wire->write(value & 0xFF);        // Write LSB
    wire->write((value >> 8) & 0xFF);  // Write second byte
    wire->write((value >> 16) & 0xFF); // Write third byte
    wire->write((value >> 24) & 0xFF); // Write MSB
    wire->endTransmission();
}

int16_t A31301::readRegister16(uint8_t reg) {
    wire->beginTransmission(i2c_address);
    wire->write(reg);
    wire->endTransmission();
    wire->requestFrom(i2c_address, (uint8_t)2);
    int16_t value = wire->read();
    value |= wire->read() << 8;
    return value;
}

void A31301::writeRegister(uint8_t reg, uint8_t value) {
    wire->beginTransmission(i2c_address);
    wire->write(reg);
    wire->write(value);
    wire->endTransmission();
}

float A31301::readMagnitude() {
    int16_t rawMagnitude = readRegister16(0x26) & 0x7FFF; // Use only the lower 15 bits
    return static_cast<float>(rawMagnitude);
}

float A31301::readAngle() {
    int16_t rawAngle = readRegister16(0x24) & 0x7FFF; // Use only the lower 15 bits
    return rawAngle * 0.0109863; // Convert to degrees
}

float A31301::calculateXYAngle() {
    return readRegister16(0x28) & 0x7FFF;
}

float A31301::calculateZAngle() {
    return readRegister16(0x2A) & 0x7FFF;
}

uint8_t A31301::getAddress() {
    return i2c_address;
}

void A31301::setCordicSelDir(uint8_t value) {
    // Writing to CORDIC_SEL_DIR register
    // Assuming the register address is 0x3A
    uint8_t reg_value = readRegister16(0x3A);
    reg_value = (reg_value & ~0x07) | (value & 0x07); // Clear the bits and set the new value
    writeRegister(0x3A, reg_value);
}

void A31301::changeI2CAddress(uint8_t newAddress) {
    // Ensure newAddress is a 7-bit address
    uint8_t addressToWrite = (newAddress << 1) & 0xFE; // Shift left to match 7-bit I2C address format

    // Combine the access key into a single 32-bit value
    uint32_t accessKey = 0x43555354; // ACCESS_KEY: 0x43, 0x55, 0x53, 0x54

    // Read current value of the EEPROM register (0x15)
    uint16_t eepromValue = readRegister16(0x15);
    // Update only bits 16:10 with the new address
    eepromValue = (eepromValue & 0x03FF) | (addressToWrite << 10);

    // Write the updated value back to the EEPROM register (0x15)
    writeRegister16(0x15, eepromValue);

    // Write the access code to unlock EEPROM as a single 32-bit value
    wire->beginTransmission(i2c_address);
    wire->write(0x3E);
    wire->write(accessKey & 0xFF);        // Write LSB
    wire->write((accessKey >> 8) & 0xFF); // Write second byte
    wire->write((accessKey >> 16) & 0xFF);// Write third byte
    wire->write((accessKey >> 24) & 0xFF);// Write MSB
    wire->endTransmission();

    delay(200);  // Add delay to allow EEPROM write to complete

    wire->beginTransmission(i2c_address);
    wire->write(0x15);
    wire->write(eepromValue & 0xFF);       // Write LSB
    wire->write((eepromValue >> 8) & 0xFF); // Write MSB
    wire->endTransmission();

    // Update the class instance's address
    i2c_address = newAddress;
}