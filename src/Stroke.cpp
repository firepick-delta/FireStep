#include <cstring>

#include "Stroke.h"

using namespace firestep;

template class Quad<int16_t>;
template class Quad<int32_t>;

Stroke::Stroke() 
	: length(0), maxV(16), scale(1), curSeg(0), planMicros(1000000), tStart(0), tTotal(0)
{}

SegIndex Stroke::goalSegment(Ticks t) {
	if (t < tStart || length == 0 || tTotal==0) {
		return 0;
	}
	Ticks dt = t-tStart;
	return dt >= tTotal ? length-1 : ((dt*length) / tTotal);
}

Ticks Stroke::goalStartTicks(Ticks t) {
	if (t < tStart || length == 0 || tTotal==0) {
		return 0;
	}
	Ticks dt = t - tStart;
	Ticks dtl = (dt >= tTotal ? tTotal-1:dt) * length;
	return ((dtl/tTotal)*tTotal)/length;
}

Ticks Stroke::goalEndTicks(Ticks t) {
	if (t < tStart || length == 0 || tTotal==0) {
		return 0;
	}
	Ticks dt = t - tStart;
	Ticks dtl = (dt >= tTotal ? tTotal-1:dt) * length;
	return (((dtl+tTotal-1)/tTotal)*tTotal)/length;
}

Quad<StepCoord> Stroke::goalPos(Ticks t) {
	Quad<StepCoord> v;
	SegIndex sGoal = goalSegment(t);
	Quad<StepCoord> pos;
	if (t <= tStart || tTotal <= 0 || length <= 0) {
		// do nothing
	} else if (t >= tStart+tTotal) {
		return dEndPos;
	} else {
		Quad<StepCoord> posSegStart;
		for (SegIndex s=0; s<sGoal; s++) {
			v += seg[s]*scale;
			posSegStart += v;
		}
		Ticks tSegStart = goalStartTicks(t);
		Ticks tSegEnd = goalEndTicks(t);
		Ticks dt = t - tStart;
		Ticks tNum = (dt>tSegEnd ? tSegEnd:dt) - tSegStart;
		Ticks tDenom = tSegEnd - tSegStart;
		v += seg[sGoal]*scale;
		v *= tNum;
		v /= tDenom;
		pos = posSegStart+v;
#ifdef TEST_TRACE
		cout << std::dec << "goalPos"
			<< " dt:" << dt
			<< " tNum:" << tNum 
			<< " tDenom:" << tDenom 
			<< " v:" << v.toString() 
			<< " pos:" << pos.toString() 
			<< endl;
#endif
	}
	return pos;
}

template<class T> T abs(T a) { return a < 0 ? -a : a; };

Status Stroke::start(Ticks tStart) {
	this->tStart = tStart;

	dPos = 0;
	Quad<StepCoord> end = goalPos(tStart+tTotal-1);
	for (int i=0; i<4; i++) {
		if (maxV < abs(dEndPos.value[i] - end.value[i])) {
			return STATUS_STROKE_END_ERROR;
		}
	}
	return STATUS_OK;
}

bool Stroke::isDone() {
	return dPos == dEndPos;
}

Status Stroke::traverse(Ticks tCurrent, QuadStepper &stepper) {
	Quad<StepCoord> dGoal = goalPos(tCurrent);
	if (tCurrent>tStart+tTotal || tCurrent>tStart && dPos==dGoal) {
		return STATUS_OK;
	}
	while (dPos != dGoal) {
		StepCoord d[4];
		StepCoord dMax = 0;
		for (uint8_t i=0; i<4; i++) {
			d[i] = dGoal.value[i] - dPos.value[i];
			dMax = max(dMax, abs(d[i]));
		}
		if (dMax == 0) {
			break;
		}
		Quad<StepCoord> pulse;
		for (uint8_t i=0; i<4; i++) {
			if (abs(d[i]) != dMax) {
				continue;
			}
			pulse.value[i] = d[i] < 0 ? -1 : 1;
		}
		dPos += pulse;
		Status status = stepper.step(pulse);
		switch (status) {
			case STATUS_OK:			// operation complete
			case STATUS_PROCESSING:	// work in progress
				break;
			default:
				return status;	// abnormal return
		}
	}
	return STATUS_PROCESSING;
}

