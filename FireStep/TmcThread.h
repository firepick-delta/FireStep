// TmcThread.h - Adds TMC2130 functionality to EMC03

#ifndef TMCTHREAD_H
#define TMCTHREAD_H

#include "MachineThread.h"
#include <tmc2130.h>

extern Tmc2130 axisX;
extern Tmc2130 axisY;
extern Tmc2130 axisZ;

namespace firestep {

typedef class TmcThread : Thread {

protected:

public:
	uint8_t tmcLedPin;
	uint8_t tmcLedValue;
  
  //uint8_t current_axis: //[0=x, 1=y, 2=z ]
  uint16_t loadmeas;
  uint16_t loadmeas_max;
  uint16_t loadmeas_min;
  
  uint32_t tstep;
  
public:
    TmcThread();
    void setup();
    void loop();
} TmcThread;

} // namespace firestep

#endif //TMCTHREAD_H
