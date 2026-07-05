# Dot Matrix Display

A dot matrix display is an optional addon board that contains 2 bi-color 8x8 dot matrices, connected via I2C bus to the RP2040 chip and accessible via ports 0x70, 0x71.

Implemented features:

- Draw an audio volume meters for the audio data from the fpga side
- Draw a custom pixels from ZX Spectrum side

Expected features:

- Send text characters and run the "scroll" mode with a desired speed


## ZX Spectrum Support

On the ZX Spectrum side (implemnted for TS-Conf and ZX Spectrum Next cores yet) the matrix is assessible via ZX UNO control / data ports #FC3B (64571) / #FD3B (64827) and their free registers F0, F1, F2.

The defautlt mode for the dot matrix display is the Audio Volume Meter.
On first data write to the control or data registers, the matrix will be switched to the custom draw mode.

### Control register 0xF0

Write to this register will execute some action as described below.

Bit description for the control register:
- bit 0: value=1 will clear the matrix
- bit 1: vaule=1 will update the matrix from the virtual framebuffer
- bit 2: value=1 will set the display brightness level from bits 7-4
- bit 3: value=1 will switch the matrix mode to the audio volume meter, value=0 will switch the matrix to the custom draw mode
- bit 7-4: will set the matrix brightness when bit2 = 1

### Pixel XY register 0xF1

Write to this register will set the next x-y position on the matrix to draw the pixel.
The lower bits 3-0 are to set the X position (from 0 to 15)
The higher bits 7-4 are to set the Y position (from 0 to 7) 

### Pixel Data register 0xF2

Write to this register will set the color to the pixel at position x-y (from the 0xF1 register) to the virtual framebuffer. 
Do display the pixel or set of pixels - please call the "update" action to the control register 0xF0.

## Example program for the Boriel ZX Basic Compiler

This code will fill the matrix with random pixels and will update it in the loop.

```
' ********************************************************************
' Dot Matrix demo on Karabas mini Go by Andy Karpov (2026-07-05)
' ********************************************************************

CLS: BORDER 0: PAPER 0: INK 7: CLS

CONST COLOR_NONE as uByte = 0
CONST COLOR_RED as uByte = 1
CONST COLOR_GREEN as uByte = 2
CONST COLOR_YELLOW as uByte = 3

CONST PORT_CTL as uLong = 64571
CONST PORT_DATA as uLong = 64827

CONST REG_CTL as uByte = 240
CONST REG_XY as uByte = 241
CONST REG_PIX as uByte = 242

CONST CTL_CLEAR as uByte = 1
CONST CTL_UPDATE as uByte = 2
CONST CTL_BRIGHTNESS as uByte = 4

SUB matrix_clear()
    OUT PORT_CTL, REG_CTL
    OUT PORT_DATA, CTL_CLEAR
END SUB

SUB matrix_update()
    OUT PORT_CTL, REG_CTL
    OUT PORT_DATA, CTL_UPDATE
END SUB

SUB matrix_draw_pixel(y as uByte, x as uByte, c as uByte)
    OUT PORT_CTL, REG_XY
    OUT PORT_DATA, x + y*16
    OUT PORT_CTL, REG_PIX
    OUT PORT_DATA, c
END SUB

SUB main()
    matrix_clear()
    DO
        DIM x, y, c AS uByte
        FOR y = 0 TO 7
        FOR x = 0 to 15
            LET c = INT(4 * RND)
            matrix_draw_pixel(y, x, c)
        NEXT x
        NEXT y 
        matrix_update()
    LOOP
END SUB

' -----------------------------------------------------------------------------------

main()

' Debug stop
PAUSE 0
```

