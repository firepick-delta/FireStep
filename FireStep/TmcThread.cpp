// TmcThread.cpp - Adds TMC2130 functionality to EMC03

#include "TmcThread.h"
#include "Arduino.h"

using namespace firestep;

TmcThread::TmcThread(){
  
}

void TmcThread::setup() {
  // id = 'M'; // FireStep will assign an ID
	tmcLedPin = 45;
  pinMode(tmcLedPin, OUTPUT);
  analogWrite(tmcLedPin,128);
	Thread::setup();
}

void TmcThread::loop() {

  tstep = axisX.get_tstep();
  //Serial.print("TSTEP: ");
  //Serial.println(tstep);
  
//  x_loadmeas = axisX.get_loadmeas(); //Call the function to get the load measurement
//  Serial.print("LoadMeas_raw: ");
//  Serial.println(x_loadmeas);
  //y_loadmeas = axisY.get_loadmeas(); //Call the function to get the load measurement
  //z_loadmeas = axisZ.get_loadmeas(); //Call the function to get the load measurement
  
  //float loadmeas_scaled = (float)x_loadmeas / 256.0;
//  Serial.print("LoadMeas: ");
//  Serial.println(loadmeas_scaled);
	//analogWrite(tmcLedPin, (uint8_t)loadmeas_scaled);
  nextLoop.ticks = threadClock.ticks + 10000;
//  nextLoop.ticks = threadClock.ticks + 500;
}

