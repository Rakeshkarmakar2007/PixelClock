#ifndef SEVENSEGMENTPIXELDISPLAY_H
#define SEVENSEGMENTPIXELDISPLAY_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

enum UNIT { C, F };

class SevenSegmentPixelDisplay {
  private:
    Adafruit_NeoPixel pixel;
    int totalNumPixels; // Dynamic tracking instead of a hardcoded macro limit

    static const bool digits[10][7];
    static const bool letter[2][7];

    void clearRange(int st, int en); // Renamed from clear() to prevent function overloading ambiguity
    void setCursorVal(int cursor, int val, uint32_t color);

  public:
    // Constructor accepts both the data Pin and the dynamic number of total LEDs
    SevenSegmentPixelDisplay(int pin, int numLeds);
    
    void begin();
    void show();
    void clear();
    
    // Marked helper utility methods as 'const' for architectural efficiency
    uint32_t color(int r, int g, int b) const;
    uint32_t colorHSV(uint16_t hue) const;
    uint32_t colorHSV(uint16_t hue, int sat, int val) const;
    
    void printHr(int hr, uint32_t color);
    void printMin(int min, uint32_t color);
    void printTemp(int temp, UNIT unit, uint32_t color);
    void blinker(bool state, uint32_t color);
};

#endif