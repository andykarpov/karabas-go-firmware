#include "MultiMatrixDisplay.h"

MultiMatrixDisplay::MultiMatrixDisplay() 
  : Adafruit_GFX(16, 8), 
    _wide(2), 
    _high(1) {
    
    uint8_t totalMatrices = _wide * _high;
    
    _addresses = new uint8_t[totalMatrices];
    _matrices = new Adafruit_BicolorMatrix[totalMatrices];
    
    for (uint8_t i = 0; i < totalMatrices; i++) {
        _addresses[i] = 0x70 + i;
    }
}

MultiMatrixDisplay::~MultiMatrixDisplay() {
    delete[] _addresses;
    delete[] _matrices;
}

bool MultiMatrixDisplay::begin() {
    bool result = true;
    uint8_t totalMatrices = _wide * _high;
    for (uint8_t i = 0; i < totalMatrices; i++) {
        if (!_matrices[i].begin(_addresses[i]))
            result = false;
    }
    return result;
}

uint8_t MultiMatrixDisplay::readKeys() {
    uint8_t keyState = 0;
    
    Wire.beginTransmission(_addresses[0]);
    Wire.write(0x40); // Read from the first data reg
    Wire.endTransmission();

    Wire.requestFrom(_addresses[0], 2); // Read first 2 bytes from the chip
    if (Wire.available() >= 2) {
        keyState = Wire.read(); // Lower 8 bits for row 0
        Wire.read(); // read and ignore the second byte
    }
    return keyState;
}

void MultiMatrixDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= width() || y < 0 || y >= height()) return;

    uint8_t matrixX = x / 8;
    uint8_t matrixY = y / 8;
    
    uint8_t matrixIndex = matrixY * _wide + matrixX;

    uint8_t localX = x % 8;
    uint8_t localY = y % 8;

    _matrices[matrixIndex].drawPixel(localX, localY, color);
}

void MultiMatrixDisplay::writeDisplay() {
    uint8_t totalMatrices = _wide * _high;
    for (uint8_t i = 0; i < totalMatrices; i++) {
        _matrices[i].writeDisplay();
    }
}

void MultiMatrixDisplay::clear() {
    uint8_t totalMatrices = _wide * _high;
    for (uint8_t i = 0; i < totalMatrices; i++) {
        _matrices[i].clear();
    }
}

void MultiMatrixDisplay::setBrightness(uint8_t brightness) {
    uint8_t totalMatrices = _wide * _high;
    for (uint8_t i = 0; i < totalMatrices; i++) {
        _matrices[i].setBrightness(brightness);
    }
}
