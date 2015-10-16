#define TMC2130 1

#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <ph5.h>
#include <tmc2130.h>
#include "Arduino.h"
#include "TmcThread.h"
#include "MachineThread.h"
#include "NeoPixel.h"

///////////////////// CHOOSE DEFAULT PIN CONFIGURATION ///////////
//#define PIN_CONFIG PC2_RAMPS_1_4
#define PIN_CONFIG PC1_EMC02

firestep::MachineThread machineThread; // FireStep command interpreter
firestep::TmcThread tmcThread;

/////////// NeoPixel display driver /////////////
#define NEOPIXEL_LEDS 16
firestep::NeoPixel neoPixel(NEOPIXEL_LEDS);

#if PIN_CONFIG == PC1_EMC02
#define LED_PIN PC1_LED_PIN
#else
#define LED_PIN PC2_LED_PIN
#endif

#ifdef TMC2130
  Tmc2130 axisX = Tmc2130(23);
  Tmc2130 axisY = Tmc2130(25);
  Tmc2130 axisZ = Tmc2130(27);
#endif

void setup() { // run once, when the sketch starts
    // Serial I/O has lowest priority, so you may need to
    // decrease baud rate to fix Serial I/O problems.
    //Serial.begin(38400); // short USB cables
    Serial.begin(19200); // long USB cables

#ifdef TMC2130
    SPI.begin();
    //CPOL=0, CPHA=0.  MSB first, per datasheet.  Assuming fsck = 8 MHz
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

    // Initialize SPI interfaces with each axis
    axisX.begin(false); //Initialize X axis
    axisY.begin(false); //Initialize Y axis
    axisZ.begin(false); //Initialize Z axis

    tmcThread.setup(); //Kick off a thread that can read registers and store statistics about them, in the background
#endif


    // Bind in NeoPixel display driver
    machineThread.machine.pDisplay = &neoPixel;

    // Initialize
    machineThread.setup(PIN_CONFIG);

    firestep::threadRunner.setup(LED_PIN);
}

void loop() {	// run over and over again
    firestep::threadRunner.run();
}

