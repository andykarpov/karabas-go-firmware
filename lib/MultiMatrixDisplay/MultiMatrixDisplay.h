#ifndef MULTI_MATRIX_DISPLAY_H
#define MULTI_MATRIX_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

/**
 * A wrapper on the Adafruit Led Backpack library to provide a single interface to multiple bi-color matricies
 */
class MultiMatrixDisplay : public Adafruit_GFX {
public:
    /**
     * Virtual display constructor.
     * 
     */
    MultiMatrixDisplay();

    /**
     * Destructir.
     */
    ~MultiMatrixDisplay();

    /**
     * Init all i2c matricies.
     */
    bool begin();

    /**
     * Override the base method to dwaw a pixel on the virtual canvas.
     */
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;

    /**
     * Send buffer to all ht16k33 chips at once.
     */
    void writeDisplay();

    /**
     * Clear the virtual canvas.
     */
    void clear();

    /**
     * Set the brightness to the virtual display.
     * @param brightness Value from 0 to 15.
     */
    void setBrightness(uint8_t brightness);

private:
    uint8_t _wide;                  // Count of matricies (hor)
    uint8_t _high;                  // Count of matricies (ver)
    uint8_t* _addresses;            // The array of connected i2c addresses of the matricies
    Adafruit_BicolorMatrix* _matrices;  // The array of connected bi-color matricies
};

#endif // MULTI_MATRIX_DISPLAY_H
