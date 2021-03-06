// DUMMY ARDUINO HEADER
#ifndef ARDUINO_H
#define ARDUINO_H
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdint.h>
#include "../ArduinoJson/include/ArduinoJson/Arduino/Print.hpp"

using namespace std;

typedef uint8_t byte;
typedef uint8_t boolean;

#define NOVALUE 32767 /* 0x77FF */
#define NOVALUESTR "32767"


#define DEC 1
#define BYTE 0
#define HEX 2
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1

#define ADEN 7
#define ADSC 6
#define ADATE 5
//#define ADFR 5
#define ADIF 4
#define ADIE 3
#define ADPS2 2
#define ADPS1 1
#define ADPS0 0
//MUX bit definitions
#define REFS1 7
#define REFS0 6
#define ADLAR 5
#define MUX3 3
#define MUX2 2
#define MUX1 1
#define MUX0 0
#define PRADC 0
#define TOIE1 0
#define CS10 0
#define CS11 1
#define CS12 2

#define ADCH arduino.MEM(0)
#define ADCSRA arduino.MEM(1)
#define ADCSRB arduino.MEM(2)
#define ADMUX arduino.MEM(3)
#define CLKPR arduino.MEM(4)
#define DIDR0 arduino.MEM(5)
#define PRR arduino.MEM(6)
#define SREGI arduino.MEM(7)
#define TCCR1A arduino.MEM(8)
#define TCCR1B arduino.MEM(9)
#define TCNT1 arduino.MEM(10)
#define TIMSK1 arduino.MEM(11)

#define cli() (SREGI=0)
#define sei() (SREGI=1)

extern "C" {
    extern unsigned long millis();
}

typedef class SerialType : public Print {
    private:
        string serialout;
		string serialline;

    public:
		void clear();
		void push(uint8_t value);
        void push(int16_t value);
        void push(int32_t value);
        void push(float value);
		void push(string value);
		void push(const char * value);
        string output();

    public:
        int available();
        void begin(long speed) ;
        byte read() ;
		virtual size_t write(uint8_t value);
        void print(const char value);
        void print(const char *value);
        void print(int value, int format = DEC);
        void println(const char value, int format = DEC) ;
        void println(const char *value = "") ;
} SerialType;


void analogWrite(int16_t dirPin, int16_t value);
int16_t analogRead(int16_t dirPin);
void digitalWrite(int16_t dirPin, int16_t value);
void delayMicroseconds(uint16_t usDelay);
int16_t digitalRead(int16_t dirPin);
void pinMode(int16_t pin, int16_t inout);
void delay(int ms);

extern SerialType Serial;

#define ARDUINO_PINS 127
#define ARDUINO_MEM 1024

#define A0 54
#define A1 (A0+1)
#define A2 (A1+1)
#define A3 (A2+1)
#define A4 (A3+1)
#define A5 (A4+1)
#define A6 (A5+1)
#define A7 (A6+1)
#define A8 (A7+1)
#define A9 (A8+1)
#define A10 (A9+1)
#define A11 (A10+1)
#define A12 (A11+1)
#define A13 (A12+1)
#define A14 (A13+1)
#define A15 (A14+1)

typedef class MockDuino {
	friend void delayMicroseconds(uint16_t us);
	friend void digitalWrite(int16_t pin, int16_t value);
	friend void analogWrite(int16_t pin, int16_t value);
	friend int16_t digitalRead(int16_t pin);
	friend int16_t analogRead(int16_t pin);
	friend void pinMode(int16_t pin, int16_t inout);
	private: 
		int16_t pin[ARDUINO_PINS];
        int16_t _pinMode[ARDUINO_PINS];
		int32_t pinPulses[ARDUINO_PINS];
        int16_t mem[ARDUINO_MEM];
		int32_t usDelay;
    public:

    public:
        MockDuino();
		void dump();
		int16_t& MEM(int addr);
		void clear();
		void timer1(int increment=1);
		void delay500ns();
		int16_t getPinMode(int16_t pin);
		int16_t getPin(int16_t pin);
		void setPin(int16_t pin, int16_t value);
		void setPinMode(int16_t pin, int16_t value);
		uint32_t pulses(int16_t pin);
		uint32_t get_usDelay() {return usDelay;}
} MockDuino;

#define DELAY500NS arduino.delay500ns();

extern MockDuino arduino;

#endif
