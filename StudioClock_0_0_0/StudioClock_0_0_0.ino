 /*
  * Studio Clock
  *    
  * 2019-11-09: JBS - Created
  * 2021-01-10: JBS - Updated with I2C Display, RTC and Pi Time Server
  * 2021-01-18: JBS - Several changes:
  *                     Changed NeoPixel type to NEO_GRBW to match ring
  *                     Removed Oled display (may put it back later)
  *                     Integrated 7 segment for HH MM
  *                     Reworked the ring display to only Red
  * 2021-01-21: JBS - Final for release 1:
  *                     Implemented non blocking serial read and 
  *                     processing of $GPRMC messages.
  *                     Set RTC adjust to every 10 min on the 15's
  *                     Display status on oled
  *                     bring baud rate to 4800
  */

#include <SoftwareSerial.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#define MAX_INPUT 128

// OLED Setup --------------------------------------------------
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C

SSD1306AsciiAvrI2c oled;

// End OLED Setup ----------------------------------------------

// Real Time Clock Setup ---------------------------------------
// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
#include "RTClib.h"

RTC_DS1307 rtc;

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
// char timeFormat[24] = "hh:mm:ss";
DateTime now;
DateTime uxTime;   // utc time object
DateTime wrkTime;  // time object for setting RTC
int iHr = 0;       // working values for set time hours
int iMn = 0;       // working values for set time minutes
int iSs = 0;       // working values for set time seconds
char wrkChr[16];    // conversion work string
// end Real Time Clock Setup -----------------------------------

// Seven Segment Setup -----------------------------------------
#include "Adafruit_LEDBackpack.h"
#define DISPLAY_ADDRESS   0x70
Adafruit_7segment clockDisplay = Adafruit_7segment();
bool blinkColon = false;
// end Seven Segment Setup -------------------------------------

// NeoPixel Ring Setup -----------------------------------------
#include <Adafruit_NeoPixel.h>

// define pins
#define NEOPIN 3

#define STARTPIXEL 0 // where do we start on the loop? use this to shift the arcs if the wiring does not start at the "12" point
uint16_t iPix = 0;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(60, NEOPIN, NEO_GRBW + NEO_KHZ800); // strip object

byte pixelColorRed, pixelColorGreen, pixelColorBlue; // holds color values

// nighttime dimming constants
// brightness based on time of day- could try warmer colors at night?
// 0-15
#define DAYBRIGHTNESS 64
#define NIGHTBRIGHTNESS 20

// end NeoPixel Ring Setup -------------------------------------
#define GPSBAUD 4800
const char del[] = ",";
char inputBuff[256];
char cBuff[256];
byte secondval = 0;
int itimeHHMM = 0;
static char input_line[MAX_INPUT];
static unsigned int input_pos = 0;

void setup() {
  // OLED Setup
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
  oled.setFont(System5x7);
  oled.setScrollMode(SCROLL_MODE_AUTO);
  oled.clear();
  oled.print("Hello world!");
  // End OLED Setup
  Serial.begin(GPSBAUD);
  // Neopixel setup
  pixelColorRed = 0;
  pixelColorGreen = 0;
  pixelColorBlue = 0;
  pinMode(NEOPIN, OUTPUT);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  strip.setBrightness(DAYBRIGHTNESS); // set brightness

  // startup sequence
  delay(500);
  colorWipe(strip.Color(127, 0, 0), 20); // Red
  colorWipe(strip.Color(0, 127, 0), 20); // Green
  colorWipe(strip.Color(0, 0, 127), 20); // Blue
  delay(500);
  // end Neopixel setup
  
  // RTC Setup ----------------------------------------------------------

#ifndef ESP8266
  while (!Serial); // wait for serial port to connect. Needed for native USB
#endif

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }  


  // end RTC Setup ------------------------------------------------------
  // Setup the display.
  clockDisplay.begin(DISPLAY_ADDRESS);
  clearInput();  // clear serial buffer input
}

void loop() {

  if (Serial.available() > 0) { // process serial data if available
    processByte(Serial.read());
  }
  
  uxTime = rtc.now();
  itimeHHMM = uxTime.hour() * 100 + uxTime.minute();
  //itimeHHMM = uxTime.hour() * 100 + uxTime.second(); // while debugging seconds display
  iSs =uxTime.second();
  clockDisplay.print(itimeHHMM, DEC);
  if ((iSs % 2) == 0 ) {
    blinkColon = !blinkColon;
  }
  clockDisplay.drawColon(blinkColon);
  clockDisplay.writeDisplay();
  secondval = iSs;  // get seconds

  // Now ring...
  for (uint8_t i = 0; i < strip.numPixels(); i++) {
    if (i <= secondval) {
      pixelColorRed = 127; //dot on
    }
    else {
      pixelColorRed = 0;   //dot off
    }
    strip.setPixelColor((i + STARTPIXEL) % 60, strip.Color(pixelColorRed, pixelColorGreen, pixelColorBlue));
  }
  strip.show();
  // done ring
}

// Process incoming serial byte
void processByte(char inByte) {
  static char jMessage[128];
  switch (inByte) {
    case '\r':
    case '\n':
    case '*':   // end of text
      input_line[input_pos] = 0;  // terminating null byte
      // terminator reached! process input_line here ...
      sprintf(jMessage, "ipos: %d ln: %s", input_pos, input_line);
      oled.println(jMessage);
      //delay(5000);
      setRTC (input_line);
      // clear input_line
      clearInput();
      // reset buffer for next time
      input_pos = 0;  
      break;
    case '$': // Start of message
      clearInput();  // clear message
      //oled.print("strt:");
      input_line[input_pos++] = '$';    
      break;
    default:
      // keep adding if not full ... allow for terminating null byte
      if (input_pos < (MAX_INPUT - 1)) {
        input_line[input_pos++] = inByte;
      }
      break;

  }  // end of switch

}

// Clears input buffer and buffer position
void clearInput() {  
      for(input_pos = 0 ; input_pos++ ; input_pos < MAX_INPUT) {
        input_line[input_pos] = '\0';
      }
      input_pos = 0;
}
// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  uint16_t iPx = 0;
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    iPx = i + STARTPIXEL;
    strip.setPixelColor((i + STARTPIXEL) % 60, c);
    strip.show();
    delay(wait);
  }
}
// Set Real Time Clock from GPS or unix time via serial
void setRTC(const char * inputData) {
  char cMessage[128];
  //          1         2
  //012345678901234567890123456789
  //$GPRMC,174523.000
  oled.print("Set1:");
  oled.println(inputData);
  if(strncmp(inputData, "$GPRMC,", 7) == 0) {  // got a $GPMRC message
    wrkChr[0] = inputData[7];
    wrkChr[1] = inputData[8];
    wrkChr[2] = '\0';
    iHr = atoi(wrkChr);
    wrkChr[0] = inputData[9];
    wrkChr[1] = inputData[10];
    wrkChr[2] = '\0';
    iMn = atoi(wrkChr);
    wrkChr[0] = inputData[11];
    wrkChr[1] = inputData[12];
    wrkChr[2] = '\0';
    iSs = atoi(wrkChr);

    sprintf(cMessage, "TM: %2d:%2d:%2d", iHr, iMn, iSs);
    oled.println(cMessage);

    if(iSs % 15 == 0) {  // update on the 15 30 45 60
      if(iHr < 24 && iMn < 60 && iSs < 60) {  // make sure time is in range
        wrkTime = DateTime(2021, 1, 21, iHr, iMn, iSs);
        rtc.adjust(wrkTime);                    
        oled.println("*** ADJUSTED ***");
      }
      //delay(5000);
    }
  }
}
