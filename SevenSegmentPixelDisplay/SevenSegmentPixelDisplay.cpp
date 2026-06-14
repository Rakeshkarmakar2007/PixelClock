// Seven Segment display project by Rakesh Karmakar.

#include "SevenSegmentPixelDisplay.h"

const bool SevenSegmentPixelDisplay::digits[10][7]{
  { 1, 1, 1, 1, 1, 1, 0 },  //0
  { 1, 0, 0, 0, 0, 1, 0 },  //1
  { 0, 1, 1, 0, 1, 1, 1 },  //2
  { 1, 1, 0, 0, 1, 1, 1 },  //3
  { 1, 0, 0, 1, 0, 1, 1 },  //4
  { 1, 1, 0, 1, 1, 0, 1 },  //5
  { 1, 1, 1, 1, 1, 0, 1 },  //6
  { 1, 0, 0, 0, 1, 1, 0 },  //7
  { 1, 1, 1, 1, 1, 1, 1 },  //8
  { 1, 1, 0, 1, 1, 1, 1 }   //9
}; 

const bool SevenSegmentPixelDisplay::letter[2][7]{
  { 0, 1, 1, 1, 1, 0, 0 },  // C
  { 0, 0, 1, 1, 1, 0, 1 },  // F
};

// Initialize the internal NeoPixel driver instance dynamically
SevenSegmentPixelDisplay::SevenSegmentPixelDisplay(int pin, int numLeds) 
  : pixel(numLeds, pin, NEO_GRB + NEO_KHZ800), totalNumPixels(numLeds) {}

void SevenSegmentPixelDisplay::begin() {
  pixel.begin();
}

void SevenSegmentPixelDisplay::show() {
  pixel.show();
}

void SevenSegmentPixelDisplay::clear() {
  pixel.clear();
}

uint32_t SevenSegmentPixelDisplay::color(int r, int g, int b) const {
  return pixel.Color(r, g, b);
}

uint32_t SevenSegmentPixelDisplay::colorHSV(uint16_t hue) const {
  return pixel.ColorHSV(hue);
}

uint32_t SevenSegmentPixelDisplay::colorHSV(uint16_t hue, int sat, int val) const {
  return pixel.ColorHSV(hue, sat, val);
}

void SevenSegmentPixelDisplay::clearRange(int st, int en) {
  if (st > en || st < 0 || en > totalNumPixels) return;
  for (int i = st; i < en; i++) {
    pixel.setPixelColor(i, 0, 0, 0);
  }
}

void SevenSegmentPixelDisplay::setCursorVal(int cursor, int val, uint32_t color) {
  cursor = constrain(cursor, 0, 3);
  val = constrain(val, 0, 9);
  
  int shift = cursor * 7;
  if (cursor > 1) shift += 2; // Compensate index tracking for the central colon blinkers

  for (int i = 0; i < 7; i++) {
    int targetPixel = shift + i;
    
    // Bounds check protection: prevent writing to indices beyond the physically initialized limit
    if (targetPixel < totalNumPixels) {
      if (digits[val][i]) {
        pixel.setPixelColor(targetPixel, color);
      } else {
        pixel.setPixelColor(targetPixel, 0); 
      }
    }
  }
}

void SevenSegmentPixelDisplay::printHr(int hr, uint32_t color) {
  setCursorVal(3, hr / 10, color);
  setCursorVal(2, hr % 10, color);
  // REMOVED pixel.show() - Handed off to the main loop's mutual exclusion control block
}

void SevenSegmentPixelDisplay::printMin(int min, uint32_t color) {
  setCursorVal(1, min / 10, color);
  setCursorVal(0, min % 10, color);
  // REMOVED pixel.show()
}

void SevenSegmentPixelDisplay::blinker(bool state, uint32_t color) {
  if (state) {
    pixel.setPixelColor(14, color);
    pixel.setPixelColor(15, color);
  } else {
    pixel.setPixelColor(14, 0);
    pixel.setPixelColor(15, 0);
  }
  // REMOVED pixel.show()
}

void SevenSegmentPixelDisplay::printTemp(int temp, UNIT unit, uint32_t color) {
  pixel.clear();
  
  setCursorVal(3, temp / 10, color);
  setCursorVal(2, temp % 10, color);
  
  pixel.setPixelColor(15, color); 
  
  for (int i = 0; i < 7; i++) {
    int targetPixel = 7 + i;
    if (targetPixel < totalNumPixels) {
      if (letter[unit][i]) {
        pixel.setPixelColor(targetPixel, color); 
      } else {
        pixel.setPixelColor(targetPixel, 0);
      }
    }
  }
  pixel.show(); // Keeps individual show function here, assuming Temperature is an exclusive display state
}