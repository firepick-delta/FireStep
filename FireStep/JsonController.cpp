#include "Arduino.h"
#ifdef CMAKE
#include <cstring>
#include <cstdio>
#endif
#include "version.h"
#include "JsonController.h"

using namespace firestep;

JsonController::JsonController(Machine& machine)
    : machine(machine) {
}

Status JsonController::setup() {
    return STATUS_OK;
}

template<class TF, class TJ>
Status processField(JsonObject& jobj, const char* key, TF& field) {
    Status status = STATUS_OK;
    const char *s;
    if ((s = jobj[key]) && *s == 0) { // query
        status = (jobj[key] = (TJ) field).success() ? status : STATUS_FIELD_ERROR;
    } else {
		TJ tjValue = jobj[key];
        double value = tjValue;
		TF tfValue = (TF) value;
        field = tfValue;
		float diff = abs(tfValue - tjValue);
        if (diff > 1e-7) {
			TESTCOUT3("STATUS_VALUE_RANGE tfValue:", tfValue, " tjValue:", tjValue, " diff:", diff);
            return STATUS_VALUE_RANGE;
        }
        jobj[key] = (TJ) field;
    }
    return status;
}
template Status processField<int16_t, int32_t>(JsonObject& jobj, const char *key, int16_t& field);
template Status processField<uint16_t, int32_t>(JsonObject& jobj, const char *key, uint16_t& field);
template Status processField<uint8_t, int32_t>(JsonObject& jobj, const char *key, uint8_t& field);
template Status processField<PH5TYPE, PH5TYPE>(JsonObject& jobj, const char *key, PH5TYPE& field);
template Status processField<bool, bool>(JsonObject& jobj, const char *key, bool& field);

template<>
Status processField<int32_t, int32_t>(JsonObject& jobj, const char *key, int32_t& field) {
    Status status = STATUS_OK;
    const char *s;
    if ((s = jobj[key]) && *s == 0) { // query
        status = (jobj[key] = field).success() ? status : STATUS_FIELD_ERROR;
    } else {
		field = jobj[key];
    }
    return status;
}

Status processProbeField(Machine& machine, MotorIndex iMotor, JsonCommand &jcmd, JsonObject &jobj, const char *key) {
    Status status = processField<StepCoord, int32_t>(jobj, key, machine.op.probe.end.value[iMotor]);
    if (status == STATUS_OK) {
        Axis &a = machine.getMotorAxis(iMotor);
        if (!a.isEnabled()) {
            return jcmd.setError(STATUS_AXIS_DISABLED, key);
        }
        StepCoord delta = abs(machine.op.probe.end.value[iMotor] - a.position);
        machine.op.probe.maxDelta = max(machine.op.probe.maxDelta, delta);
    }
    return status;
}

Status processHomeField(Machine& machine, AxisIndex iAxis, JsonCommand &jcmd, JsonObject &jobj, const char *key) {
    Status status = processField<StepCoord, int32_t>(jobj, key, machine.axis[iAxis].home);
    Axis &a = machine.axis[iAxis];
    if (a.isEnabled() && a.pinMin != NOPIN) {
        jobj[key] = a.home;
        a.homing = true;
    } else {
        jobj[key] = a.position;
        a.homing = false;
    }

    return status;
}

int axisOf(char c) {
    switch (c) {
    default:
        return -1;
    case 'x':
        return 0;
    case 'y':
        return 1;
    case 'z':
        return 2;
    case 'a':
        return 3;
    case 'b':
        return 4;
    case 'c':
        return 5;
    }
}

Status JsonController::processPosition(JsonCommand &jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    const char *s;
    if (strlen(key) == 3) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["1"] = "";
            node["2"] = "";
            node["3"] = "";
            node["4"] = "";
            if (!node.at("4").success()) {
                return jcmd.setError(STATUS_JSON_KEY, "4");
            }
        }
        JsonObject& kidObj = jobj[key];
        if (!kidObj.success()) {
            return STATUS_POSITION_ERROR;
        }
        for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
            status = processPosition(jcmd, kidObj, it->key);
            if (status != STATUS_OK) {
                return status;
            }
        }
    } else {
        const char* axisStr = key;
        AxisIndex iAxis = machine.axisOfName(axisStr);
        if (iAxis == INDEX_NONE) {
            if (strlen(key) > 3) {
                axisStr += 3;
                iAxis = machine.axisOfName(axisStr);
                if (iAxis == INDEX_NONE) {
                    return jcmd.setError(STATUS_NO_MOTOR, key);
                }
            } else {
                return jcmd.setError(STATUS_NO_MOTOR, key);
            }
        }
        status = processField<StepCoord, int32_t>(jobj, key, machine.axis[iAxis].position);
    }
    return status;
}

inline int8_t hexValue(char c) {
    switch (c) {
    case '0':
        return 0;
    case '1':
        return 1;
    case '2':
        return 2;
    case '3':
        return 3;
    case '4':
        return 4;
    case '5':
        return 5;
    case '6':
        return 6;
    case '7':
        return 7;
    case '8':
        return 8;
    case '9':
        return 9;
    case 'A':
    case 'a':
        return 0xa;
    case 'B':
    case 'b':
        return 0xb;
    case 'C':
    case 'c':
        return 0xc;
    case 'D':
    case 'd':
        return 0xd;
    case 'E':
    case 'e':
        return 0xe;
    case 'F':
    case 'f':
        return 0xf;
    default:
        return -1;
    }
}

Status JsonController::initializeStrokeArray(JsonCommand &jcmd,
        JsonObject& stroke, const char *key, MotorIndex iMotor, int16_t &slen) {
    if (stroke.at(key).is<JsonArray&>()) {
        JsonArray &jarr = stroke[key];
        for (JsonArray::iterator it2 = jarr.begin(); it2 != jarr.end(); ++it2) {
            if (*it2 < -127 || 127 < *it2) {
                return STATUS_RANGE_ERROR;
            }
            machine.stroke.seg[slen++].value[iMotor] = (StepDV) (int32_t) * it2;
        }
    } else if (stroke.at(key).is<const char*>()) {
        const char *s = stroke[key];
        while (*s) {
            int8_t high = hexValue(*s++);
            int8_t low = hexValue(*s++);
            if (high < 0 || low < 0) {
                return STATUS_FIELD_HEX_ERROR;
            }
            StepDV dv = ((high<<4) | low);
            //TESTCOUT3("initializeStrokeArray(", key, ") sLen:", (int)slen, " dv:", (int) dv);
            machine.stroke.seg[slen++].value[iMotor] = dv;
        }
    } else {
        return STATUS_FIELD_ARRAY_ERROR;
    }
    stroke[key] = (int32_t) 0;
    return STATUS_OK;
}

Status JsonController::initializeStroke(JsonCommand &jcmd, JsonObject& stroke) {
    Status status = STATUS_OK;
    int16_t slen[4] = {0, 0, 0, 0};
    bool us_ok = false;
    machine.stroke.clear();
    for (JsonObject::iterator it = stroke.begin(); it != stroke.end(); ++it) {
        if (strcmp("us", it->key) == 0) {
            int32_t planMicros;
            status = processField<int32_t, int32_t>(stroke, it->key, planMicros);
            if (status != STATUS_OK) {
                return jcmd.setError(status, it->key);
            }
            float seconds = (float) planMicros / 1000000.0;
            machine.stroke.setTimePlanned(seconds);
            us_ok = true;
        } else if (strcmp("dp", it->key) == 0) {
            JsonArray &jarr = stroke[it->key];
            if (!jarr.success()) {
                return jcmd.setError(STATUS_FIELD_ARRAY_ERROR, it->key);
            }
            if (!jarr[0].success()) {
                return jcmd.setError(STATUS_JSON_ARRAY_LEN, it->key);
            }
            for (MotorIndex i = 0; i < 4 && jarr[i].success(); i++) {
                machine.stroke.dEndPos.value[i] = jarr[i];
            }
        } else if (strcmp("sc", it->key) == 0) {
            status = processField<StepCoord, int32_t>(stroke, it->key, machine.stroke.scale);
            if (status != STATUS_OK) {
                return jcmd.setError(status, it->key);
            }
        } else {
            MotorIndex iMotor = machine.motorOfName(it->key);
            if (iMotor == INDEX_NONE) {
                return jcmd.setError(STATUS_NO_MOTOR, it->key);
            }
            status = initializeStrokeArray(jcmd, stroke, it->key, iMotor, slen[iMotor]);
            if (status != STATUS_OK) {
                return jcmd.setError(status, it->key);
            }
        }
    }
    if (!us_ok) {
        return jcmd.setError(STATUS_FIELD_REQUIRED, "us");
    }
    if (slen[0] && slen[1] && slen[0] != slen[1]) {
        return STATUS_S1S2LEN_ERROR;
    }
    if (slen[0] && slen[2] && slen[0] != slen[2]) {
        return STATUS_S1S3LEN_ERROR;
    }
    if (slen[0] && slen[3] && slen[0] != slen[3]) {
        return STATUS_S1S4LEN_ERROR;
    }
    machine.stroke.length = slen[0] ? slen[0] : (slen[1] ? slen[1] : (slen[2] ? slen[2] : slen[3]));
    if (machine.stroke.length == 0) {
        return STATUS_STROKE_NULL_ERROR;
    }
    status = machine.stroke.start(ticks());
    if (status != STATUS_OK) {
        return status;
    }
    return STATUS_BUSY_MOVING;
}

Status JsonController::traverseStroke(JsonCommand &jcmd, JsonObject &stroke) {
    Status status =  machine.stroke.traverse(ticks(), machine);

    Quad<StepCoord> &pos = machine.stroke.position();
    for (JsonObject::iterator it = stroke.begin(); it != stroke.end(); ++it) {
        MotorIndex iMotor = machine.motorOfName(it->key + (strlen(it->key) - 1));
        if (iMotor != INDEX_NONE) {
            stroke[it->key] = pos.value[iMotor];
        }
    }

    return status;
}

Status JsonController::processStroke(JsonCommand &jcmd, JsonObject& jobj, const char* key) {
    JsonObject &stroke = jobj[key];
    if (!stroke.success()) {
        return STATUS_JSON_STROKE_ERROR;
    }

    Status status = jcmd.getStatus();
    if (status == STATUS_BUSY_PARSED) {
        status = initializeStroke(jcmd, stroke);
    } else if (status == STATUS_BUSY_MOVING) {
        if (machine.stroke.curSeg < machine.stroke.length) {
            status = traverseStroke(jcmd, stroke);
        }
        if (machine.stroke.curSeg >= machine.stroke.length) {
            status = STATUS_OK;
        }
    }
    return status;
}

Status JsonController::processMotor(JsonCommand &jcmd, JsonObject& jobj, const char* key, char group) {
    Status status = STATUS_OK;
    const char *s;
    if (strlen(key) == 1) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["ma"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = processMotor(jcmd, kidObj, it->key, group);
                if (status != STATUS_OK) {
                    return status;
                }
            }
        }
    } else if (strcmp("ma", key) == 0 || strcmp("ma", key + 1) == 0) {
        JsonVariant &jv = jobj[key];
        MotorIndex iMotor = group - '1';
        if (iMotor < 0 || MOTOR_COUNT <= iMotor) {
            return STATUS_MOTOR_INDEX;
        }
        AxisIndex iAxis = machine.getAxisIndex(iMotor);
        status = processField<AxisIndex, int32_t>(jobj, key, iAxis);
        machine.setAxisIndex(iMotor, iAxis);
    }
    return status;
}

Status JsonController::processPin(JsonObject& jobj, const char *key, PinType &pin, int16_t mode, int16_t value) {
    PinType newPin = pin;
    Status status = processField<PinType, int32_t>(jobj, key, newPin);
    machine.setPin(pin, newPin, mode, value);
    return status;
}

Status JsonController::processAxis(JsonCommand &jcmd, JsonObject& jobj, const char* key, char group) {
    Status status = STATUS_OK;
    const char *s;
    AxisIndex iAxis = axisOf(group);
    if (iAxis < 0) {
        return STATUS_AXIS_ERROR;
    }
    Axis &axis = machine.axis[iAxis];
    if (strlen(key) == 1) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["dh"] = "";
            node["en"] = "";
            node["ho"] = "";
            node["is"] = "";
            node["lm"] = "";
            node["ln"] = "";
            node["mi"] = "";
            node["pd"] = "";
            node["pe"] = "";
            node["pm"] = "";
            node["pn"] = "";
            node["po"] = "";
            node["ps"] = "";
            node["sa"] = "";
            node["tm"] = "";
            node["tn"] = "";
            node["ud"] = "";
            if (!node.at("ud").success()) {
                return jcmd.setError(STATUS_JSON_KEY, "ud");
            }
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = processAxis(jcmd, kidObj, it->key, group);
                if (status != STATUS_OK) {
                    return status;
                }
            }
        }
    } else if (strcmp("en", key) == 0 || strcmp("en", key + 1) == 0) {
        bool active = axis.isEnabled();
        status = processField<bool, bool>(jobj, key, active);
        if (status == STATUS_OK) {
            axis.enable(active);
            status = (jobj[key] = axis.isEnabled()).success() ? status : STATUS_FIELD_ERROR;
        }
    } else if (strcmp("dh", key) == 0 || strcmp("dh", key + 1) == 0) {
        status = processField<bool, bool>(jobj, key, axis.dirHIGH);
        if (axis.pinDir != NOPIN && status == STATUS_OK) {	// force setting of direction bit in case meaning changed
            axis.setAdvancing(false);
            axis.setAdvancing(true);
        }
    } else if (strcmp("ho", key) == 0 || strcmp("ho", key + 1) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, axis.home);
    } else if (strcmp("is", key) == 0 || strcmp("is", key + 1) == 0) {
        status = processField<DelayMics, int32_t>(jobj, key, axis.idleSnooze);
    } else if (strcmp("lb", key) == 0 || strcmp("lb", key + 1) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, machine.latchBackoff);
    } else if (strcmp("lm", key) == 0 || strcmp("lm", key + 1) == 0) {
        axis.readAtMax(machine.invertLim);
        status = processField<bool, bool>(jobj, key, axis.atMax);
    } else if (strcmp("ln", key) == 0 || strcmp("ln", key + 1) == 0) {
        axis.readAtMin(machine.invertLim);
        status = processField<bool, bool>(jobj, key, axis.atMin);
    } else if (strcmp("mi", key) == 0 || strcmp("mi", key + 1) == 0) {
        status = processField<uint8_t, int32_t>(jobj, key, axis.microsteps);
        if (axis.microsteps < 1) {
            axis.microsteps = 1;
            return STATUS_JSON_POSITIVE1;
        }
    } else if (strcmp("pd", key) == 0 || strcmp("pd", key + 1) == 0) {
        status = processPin(jobj, key, axis.pinDir, OUTPUT);
    } else if (strcmp("pe", key) == 0 || strcmp("pe", key + 1) == 0) {
        status = processPin(jobj, key, axis.pinEnable, OUTPUT, HIGH);
    } else if (strcmp("pm", key) == 0 || strcmp("pm", key + 1) == 0) {
        status = processPin(jobj, key, axis.pinMax, INPUT);
    } else if (strcmp("pn", key) == 0 || strcmp("pn", key + 1) == 0) {
        status = processPin(jobj, key, axis.pinMin, INPUT);
    } else if (strcmp("po", key) == 0 || strcmp("po", key + 1) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, axis.position);
    } else if (strcmp("ps", key) == 0 || strcmp("ps", key + 1) == 0) {
        status = processPin(jobj, key, axis.pinStep, OUTPUT);
    } else if (strcmp("sa", key) == 0 || strcmp("sa", key + 1) == 0) {
        status = processField<float, double>(jobj, key, axis.stepAngle);
    } else if (strcmp("tm", key) == 0 || strcmp("tm", key + 1) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, axis.travelMax);
    } else if (strcmp("tn", key) == 0 || strcmp("tn", key + 1) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, axis.travelMin);
    } else if (strcmp("ud", key) == 0 || strcmp("ud", key + 1) == 0) {
        status = processField<DelayMics, int32_t>(jobj, key, axis.usDelay);
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

typedef class PHSelfTest {
private:
    int32_t nLoops;
    StepCoord pulses;
    int16_t nSegs;
    Machine &machine;

private:
    Status execute(JsonCommand& jcmd, JsonObject& jobj);

public:
    PHSelfTest(Machine& machine)
        : nLoops(0), pulses(6400), nSegs(0), machine(machine)
    {}

    Status process(JsonCommand& jcmd, JsonObject& jobj, const char* key);
} PHSelfTest;

Status PHSelfTest::execute(JsonCommand &jcmd, JsonObject& jobj) {
    int16_t minSegs = nSegs ? nSegs : 0; //max(10, min(STROKE_SEGMENTS-1,abs(pulses)/100));
    int16_t maxSegs = nSegs ? nSegs : 0; // STROKE_SEGMENTS-1;
    if (maxSegs >= STROKE_SEGMENTS) {
        return jcmd.setError(STATUS_STROKE_MAXLEN, "sg");
    }
    StrokeBuilder sb(machine.vMax, machine.tvMax, minSegs, maxSegs);
    if (pulses >= 0) {
        machine.setMotorPosition(Quad<StepCoord>());
    } else {
        machine.setMotorPosition(Quad<StepCoord>(
                                     machine.getMotorAxis(0).isEnabled() ? -pulses : 0,
                                     machine.getMotorAxis(1).isEnabled() ? -pulses : 0,
                                     machine.getMotorAxis(2).isEnabled() ? -pulses : 0,
                                     machine.getMotorAxis(3).isEnabled() ? -pulses : 0));
    }
    Status status = sb.buildLine(machine.stroke, Quad<StepCoord>(
                                     machine.getMotorAxis(0).isEnabled() ? pulses : 0,
                                     machine.getMotorAxis(1).isEnabled() ? pulses : 0,
                                     machine.getMotorAxis(2).isEnabled() ? pulses : 0,
                                     machine.getMotorAxis(3).isEnabled() ? pulses : 0));
    if (status != STATUS_OK) {
        return status;
    }
    Ticks tStart = ticks();
    status = machine.stroke.start(tStart);
    switch (status) {
    case STATUS_OK:
        break;
    case STATUS_STROKE_TIME:
        return jcmd.setError(status, "tv");
    default:
        return status;
    }
#ifdef TEST
    cout << "PHSelfTest::execute() pulses:" << pulses
         << " pos:" << machine.getMotorPosition().toString() << endl;
#endif
    do {
        nLoops++;
        status =  machine.stroke.traverse(ticks(), machine);
#ifdef TEST
        if (nLoops % 500 == 0) {
            cout << "PHSelfTest:execute()"
                 << " t:"
                 << (threadClock.ticks - machine.stroke.tStart) /
                 (float) machine.stroke.get_dtTotal()
                 << " pos:"
                 << machine.getMotorPosition().toString() << endl;
        }
#endif
    } while (status == STATUS_BUSY_MOVING);
#ifdef TEST
    cout << "PHSelfTest::execute() pos:" << machine.getMotorPosition().toString()
         << " status:" << status << endl;
#endif
    if (status == STATUS_OK) {
        status = STATUS_BUSY_MOVING; // repeat indefinitely
    }
    Ticks tElapsed = ticks() - tStart;

    float ts = tElapsed / (float) TICKS_PER_SECOND;
    float tp = machine.stroke.getTimePlanned();
    jobj["lp"] = nLoops;
    jobj["pp"].set(machine.stroke.vPeak * (machine.stroke.length / ts), 1);
    jobj["sg"] = machine.stroke.length;
    jobj["tp"].set(tp, 3);
    jobj["ts"].set(ts, 3);

    return status;
}

Status PHSelfTest::process(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    const char *s;

    if (strcmp("tstph", key) == 0) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["lp"] = "";
            node["mv"] = "";
            node["pp"] = "";
            node["pu"] = "";
            node["sg"] = "";
            node["ts"] = "";
            node["tp"] = "";
            node["tv"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (!kidObj.success()) {
            return jcmd.setError(STATUS_JSON_OBJECT, key);
        }
        for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
            status = process(jcmd, kidObj, it->key);
            if (status != STATUS_OK) {
#ifdef TEST
                cout << "PHSelfTest::process() status:" << status << endl;
#endif
                return status;
            }
        }
        status = execute(jcmd, kidObj);
        if (status == STATUS_BUSY_MOVING) {
            pulses = -pulses; //reverse direction
            status = execute(jcmd, kidObj);
        }
    } else if (strcmp("lp", key) == 0) {
        // output variable
    } else if (strcmp("mv", key) == 0) {
        status = processField<int32_t, int32_t>(jobj, key, machine.vMax);
    } else if (strcmp("pp", key) == 0) {
        // output variable
    } else if (strcmp("pu", key) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, pulses);
    } else if (strcmp("sg", key) == 0) {
        status = processField<int16_t, int32_t>(jobj, key, nSegs);
    } else if (strcmp("ts", key) == 0) {
        // output variable
    } else if (strcmp("tp", key) == 0) {
        // output variable
    } else if (strcmp("tv", key) == 0) {
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, machine.tvMax);
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

typedef class PHMoveTo {
private:
    int32_t nLoops;
    Quad<PH5TYPE> destination;
    int16_t nSegs;
    Machine &machine;

private:
    Status execute(JsonCommand& jcmd, JsonObject *pjobj);

public:
    PHMoveTo(Machine& machine)
        : nLoops(0), nSegs(0), machine(machine) {
        Quad<StepCoord> curPos = machine.getMotorPosition();
        for (QuadIndex i=0; i<QUAD_ELEMENTS; i++) {
            destination.value[i] = curPos.value[i];
        }
        switch (machine.topology) {
        case MTO_RAW:
        default:
            // no conversion required
            break;
        case MTO_FPD:
            XYZ3D xyz(machine.getXYZ3D());
            destination.value[0] = xyz.x;
            destination.value[1] = xyz.y;
            destination.value[2] = xyz.z;
            break;
        }
    }

    Status process(JsonCommand& jcmd, JsonObject& jobj, const char* key);
} PHMoveTo;

Status PHMoveTo::execute(JsonCommand &jcmd, JsonObject *pjobj) {
    StrokeBuilder sb(machine.vMax, machine.tvMax);
    Quad<StepCoord> curPos = machine.getMotorPosition();
    Quad<StepCoord> dPos;
    switch (machine.topology) {
    case MTO_RAW:
    default:
        for (QuadIndex i=0; i<QUAD_ELEMENTS; i++) {
            dPos.value[i] = destination.value[i] - curPos.value[i];
        }
        break;
    case MTO_FPD:
        XYZ3D xyz(destination.value[0], destination.value[1], destination.value[2]);
        Step3D pulses(machine.delta.calcPulses(xyz));
        dPos.value[0] = pulses.p1 - curPos.value[0];
        dPos.value[1] = pulses.p2 - curPos.value[1];
        dPos.value[2] = pulses.p3 - curPos.value[2];
        dPos.value[3] = destination.value[3] - curPos.value[3];
        break;
    }
    for (QuadIndex i = 0; i < QUAD_ELEMENTS; i++) {
        if (!machine.getMotorAxis(i).isEnabled()) {
            dPos.value[i] = 0;
        }
    }
    Status status = STATUS_OK;
    float tp = 0;
    float ts = 0;
    float pp = 0;
    int16_t sg = 0;
    if (!dPos.isZero()) {
        status = sb.buildLine(machine.stroke, dPos);
        if (status != STATUS_OK) {
            return status;
        }
        Ticks tStrokeStart = ticks();
        status = machine.stroke.start(tStrokeStart);
        switch (status) {
        case STATUS_OK:
            break;
        case STATUS_STROKE_TIME:
            return jcmd.setError(status, "tv");
        default:
            return status;
        }
        do {
            nLoops++;
            status = machine.stroke.traverse(ticks(), machine);
        } while (status == STATUS_BUSY_MOVING);
        tp = machine.stroke.getTimePlanned();
        ts = (ticks() - tStrokeStart) / (float) TICKS_PER_SECOND;
        pp = machine.stroke.vPeak * (machine.stroke.length / ts);
        sg = machine.stroke.length;
    }

    if (pjobj) {
        if (pjobj->at("lp").success()) {
            (*pjobj)["lp"] = nLoops;
        }
        if (pjobj->at("pp").success()) {
            (*pjobj)["pp"].set(pp, 1);
        }
        if (pjobj->at("sg").success()) {
            (*pjobj)["sg"] = sg;
        }
        if (pjobj->at("tp").success()) {
            (*pjobj)["tp"].set(tp, 3);
        }
        if (pjobj->at("ts").success()) {
            (*pjobj)["ts"].set(ts, 3);
        }
    }

    return status;
}

Status PHMoveTo::process(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    const char *s;

    if (strcmp("mov", key) == 0) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["lp"] = "";
            node["mv"] = "";
            node["pp"] = "";
            node["sg"] = "";
            node["tp"] = "";
            node["ts"] = "";
            if (machine.getMotorAxis(0).isEnabled()) {
                node["1"] = "";
            }
            if (machine.getMotorAxis(1).isEnabled()) {
                node["2"] = "";
            }
            if (machine.getMotorAxis(2).isEnabled()) {
                node["3"] = "";
            }
            if (machine.getMotorAxis(3).isEnabled()) {
                node["4"] = "";
            }
        }
        JsonObject& kidObj = jobj[key];
        if (!kidObj.success()) {
            return jcmd.setError(STATUS_JSON_OBJECT, key);
        }
        for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
            status = process(jcmd, kidObj, it->key);
            if (status != STATUS_OK) {
                TESTCOUT1("PHMoveTo::process() status:", status);
                return status;
            }
        }
        status = execute(jcmd, &kidObj);
    } else if (strcmp("movrx",key) == 0 || strcmp("rx",key) == 0) {
        // TODO: clean up mov implementation
        switch (machine.topology) {
        case MTO_FPD: {
            XYZ3D xyz = machine.getXYZ3D();
            PH5TYPE x = 0;
            status = processField<PH5TYPE, PH5TYPE>(jobj, key, x);
            if (status == STATUS_OK) {
                destination.value[0] = xyz.x + x;
                if (strcmp("movrx",key) == 0) {
                    status = execute(jcmd, NULL);
                }
            }
            break;
        }
        default:
            return jcmd.setError(STATUS_MTO_FIELD, key);
        }
    } else if (strcmp("movry",key) == 0 || strcmp("ry",key) == 0) {
        // TODO: clean up mov implementation
        switch (machine.topology) {
        case MTO_FPD: {
            XYZ3D xyz = machine.getXYZ3D();
            PH5TYPE y = 0;
            status = processField<PH5TYPE, PH5TYPE>(jobj, key, y);
            if (status == STATUS_OK) {
                destination.value[1] = xyz.y + y;
                if (strcmp("movry",key) == 0) {
                    status = execute(jcmd, NULL);
                }
            }
            break;
        }
        default:
            return jcmd.setError(STATUS_MTO_FIELD, key);
        }
    } else if (strcmp("movrz",key) == 0 || strcmp("rz",key) == 0) {
        // TODO: clean up mov implementation
        switch (machine.topology) {
        case MTO_FPD: {
            XYZ3D xyz = machine.getXYZ3D();
            PH5TYPE z = 0;
            status = processField<PH5TYPE, PH5TYPE>(jobj, key, z);
            if (status == STATUS_OK) {
                destination.value[2] = xyz.z + z;
                if (strcmp("movrz",key) == 0) {
                    status = execute(jcmd, NULL);
                }
            }
            break;
        }
        default:
            return jcmd.setError(STATUS_MTO_FIELD, key);
        }
    } else if (strncmp("mov", key, 3) == 0) { // short form
        // TODO: clean up mov implementation
        MotorIndex iMotor = machine.motorOfName(key + strlen(key) - 1);
        if (iMotor == INDEX_NONE) {
            TESTCOUT1("STATUS_NO_MOTOR: ", key);
            return jcmd.setError(STATUS_NO_MOTOR, key);
        }
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, destination.value[iMotor]);
        if (status == STATUS_OK) {
            status = execute(jcmd, NULL);
        }
    } else if (strcmp("d", key) == 0) {
        if (!jobj.at("a").success()) {
            return jcmd.setError(STATUS_FIELD_REQUIRED,"a");
        }
    } else if (strcmp("a", key) == 0) {
        // polar CCW from X-axis around X0Y0
        if (!jobj.at("d").success()) {
            return jcmd.setError(STATUS_FIELD_REQUIRED,"d");
        }
        PH5TYPE d = jobj["d"];
        PH5TYPE a = jobj["a"];
        PH5TYPE pi = 3.14159265359;
        PH5TYPE radians = a * pi / 180.0;
        PH5TYPE y = d * sin(radians);
        PH5TYPE x = d * cos(radians);
        TESTCOUT2("x:", x, " y:", y);
        destination.value[0] = x;
        destination.value[1] = y;
    } else if (strcmp("lp", key) == 0) {
        // output variable
    } else if (strcmp("mv", key) == 0) {
        status = processField<int32_t, int32_t>(jobj, key, machine.vMax);
    } else if (strcmp("pp", key) == 0) {
        // output variable
    } else if (strcmp("sg", key) == 0) {
        status = processField<int16_t, int32_t>(jobj, key, nSegs);
    } else if (strcmp("ts", key) == 0) {
        // output variable
    } else if (strcmp("tp", key) == 0) {
        // output variable
    } else if (strcmp("tv", key) == 0) {
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, machine.tvMax);
    } else {
        MotorIndex iMotor = machine.motorOfName(key);
        if (iMotor == INDEX_NONE) {
            TESTCOUT1("STATUS_NO_MOTOR: ", key);
            return jcmd.setError(STATUS_NO_MOTOR, key);
        }
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, destination.value[iMotor]);
    }
    return status;
}

Status JsonController::processTest(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = jcmd.getStatus();

    switch (status) {
    case STATUS_BUSY_PARSED:
    case STATUS_BUSY_MOVING:
        if (strcmp("tst", key) == 0) {
            JsonObject& tst = jobj[key];
            if (!tst.success()) {
                return jcmd.setError(STATUS_JSON_OBJECT, key);
            }
            for (JsonObject::iterator it = tst.begin(); it != tst.end(); ++it) {
                status = processTest(jcmd, tst, it->key);
            }
        } else if (strcmp("rv", key) == 0 || strcmp("tstrv", key) == 0) { // revolution steps
            JsonArray &jarr = jobj[key];
            if (!jarr.success()) {
                return jcmd.setError(STATUS_FIELD_ARRAY_ERROR, key);
            }
            Quad<StepCoord> steps;
            for (MotorIndex i = 0; i < 4; i++) {
                if (jarr[i].success()) {
                    Axis &a = machine.getMotorAxis(i);
                    int16_t revs = jarr[i];
                    int16_t revSteps = 360 / a.stepAngle;
                    int16_t revMicrosteps = revSteps * a.microsteps;
                    int16_t msRev = (a.usDelay * revMicrosteps) / 1000;
                    steps.value[i] = revs * revMicrosteps;
                }
            }
            Quad<StepCoord> steps1(steps);
            status = machine.pulse(steps1);
            if (status == STATUS_OK) {
                delay(250);
                Quad<StepCoord> steps2(steps.absoluteValue());
                status = machine.pulse(steps2);
                delay(250);
            }
            if (status == STATUS_OK) {
                status = STATUS_BUSY_MOVING;
            }
        } else if (strcmp("sp", key) == 0 || strcmp("tstsp", key) == 0) {
            // step pulses
            JsonArray &jarr = jobj[key];
            if (!jarr.success()) {
                return jcmd.setError(STATUS_FIELD_ARRAY_ERROR, key);
            }
            Quad<StepCoord> steps;
            for (MotorIndex i = 0; i < 4; i++) {
                if (jarr[i].success()) {
                    steps.value[i] = jarr[i];
                }
            }
            status = machine.pulse(steps);
        } else if (strcmp("ph", key) == 0 || strcmp("tstph", key) == 0) {
            return PHSelfTest(machine).process(jcmd, jobj, key);
        } else {
            return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
        }
        break;
    }
    return status;
}

Status JsonController::processSys(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    if (strcmp("sys", key) == 0) {
        const char *s;
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["ah"] = "";
            node["as"] = "";
			node["ch"] = "";
			node["eu"] = "";
            node["fr"] = "";
            node["hp"] = "";
            node["jp"] = "";
            node["lb"] = "";
            node["lh"] = "";
            node["lp"] = "";
            node["mv"] = "";
            node["om"] = "";
            node["pc"] = "";
            node["pi"] = "";
            node["sd"] = "";
            node["tc"] = "";
            node["to"] = "";
            node["tv"] = "";
            node["v"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = processSys(jcmd, kidObj, it->key);
                if (status != STATUS_OK) {
                    return status;
                }
            }
        }
    } else if (strcmp("ah", key) == 0 || strcmp("sysah", key) == 0) {
        status = processField<bool, bool>(jobj, key, machine.autoHome);
    } else if (strcmp("as", key) == 0 || strcmp("sysas", key) == 0) {
        status = processField<bool, bool>(jobj, key, machine.autoSync);
    } else if (strcmp("ch", key) == 0 || strcmp("sysch", key) == 0) {
		int32_t curHash = machine.hash();
		int32_t jsonHash = curHash;
		//TESTCOUT3("A curHash:", curHash, " jsonHash:", jsonHash, " jobj[key]:", (int32_t) jobj[key]);
        status = processField<int32_t, int32_t>(jobj, key, jsonHash);
		//TESTCOUT3("B curHash:", curHash, " jsonHash:", jsonHash, " jobj[key]:", (int32_t) jobj[key]);
		if (jsonHash != curHash) {
			machine.syncHash = jsonHash;
		}
    } else if (strcmp("eu", key) == 0 || strcmp("syseu", key) == 0) {
		bool euExisting = machine.isEEUserEnabled();
		bool euNew = euExisting;
        status = processField<bool, bool>(jobj, key, euNew);
		if (euNew != euExisting) {
			machine.enableEEUser(euNew);
		}
    } else if (strcmp("db", key) == 0 || strcmp("sysdb", key) == 0) {
        status = processField<uint8_t, long>(jobj, key, machine.debounce);
    } else if (strcmp("fr", key) == 0 || strcmp("sysfr", key) == 0) {
        leastFreeRam = min(leastFreeRam, freeRam());
        jobj[key] = leastFreeRam;
    } else if (strcmp("hp", key) == 0 || strcmp("syshp", key) == 0) {
        status = processField<int16_t, long>(jobj, key, machine.homingPulses);
    } else if (strcmp("jp", key) == 0 || strcmp("sysjp", key) == 0) {
        status = processField<bool, bool>(jobj, key, machine.jsonPrettyPrint);
    } else if (strcmp("lb", key) == 0 || strcmp("lb", key + 1) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, machine.latchBackoff);
    } else if (strcmp("lh", key) == 0 || strcmp("syslh", key) == 0) {
        status = processField<bool, bool>(jobj, key, machine.invertLim);
    } else if (strcmp("lp", key) == 0 || strcmp("syslp", key) == 0) {
        status = processField<int32_t, int32_t>(jobj, key, nLoops);
    } else if (strcmp("mv", key) == 0 || strcmp("sysmv", key) == 0) {
        status = processField<int32_t, int32_t>(jobj, key, machine.vMax);
    } else if (strcmp("om", key) == 0 || strcmp("sysom", key) == 0) {
        status = processField<OutputMode, int32_t>(jobj, key, machine.outputMode);
    } else if (strcmp("pc", key) == 0 || strcmp("syspc", key) == 0) {
        PinConfig pc = machine.getPinConfig();
        status = processField<PinConfig, int32_t>(jobj, key, pc);
        const char *s;
        if ((s = jobj.at(key)) && *s == 0) { // query
            // do nothing
        } else {
            machine.setPinConfig(pc);
        }
    } else if (strcmp("pi", key) == 0 || strcmp("syspi", key) == 0) {
        PinType pinStatus = machine.pinStatus;
        status = processField<PinType, int32_t>(jobj, key, pinStatus);
        if (pinStatus != machine.pinStatus) {
            machine.pinStatus = pinStatus;
            machine.pDisplay->setup(pinStatus);
        }
    } else if (strcmp("sd", key) == 0 || strcmp("syssd", key) == 0) {
        status = processField<DelayMics, int32_t>(jobj, key, machine.searchDelay);
    } else if (strcmp("to", key) == 0 || strcmp("systo", key) == 0) {
        Topology value = machine.topology;
        status = processField<Topology, int32_t>(jobj, key, value);
        if (value != machine.topology) {
            machine.topology = value;
            switch (machine.topology) {
            case MTO_RAW:
            default:
                break;
            case MTO_FPD:
                machine.delta.setup();
                if (machine.axis[0].home >= 0 &&
                        machine.axis[1].home >= 0 &&
                        machine.axis[2].home >= 0) {
                    // Delta always has negateve home limit switch
                    Step3D home = machine.delta.getHomePulses();
                    machine.axis[0].position += home.p1-machine.axis[0].home;
                    machine.axis[1].position += home.p2-machine.axis[1].home;
                    machine.axis[2].position += home.p3-machine.axis[2].home;
                    machine.axis[0].home = home.p1;
                    machine.axis[1].home = home.p2;
                    machine.axis[2].home = home.p3;
                }
                break;
            }
        }
    } else if (strcmp("tc", key) == 0 || strcmp("systc", key) == 0) {
        jobj[key] = threadClock.ticks;
    } else if (strcmp("tv", key) == 0 || strcmp("systv", key) == 0) {
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, machine.tvMax);
    } else if (strcmp("v", key) == 0 || strcmp("sysv", key) == 0) {
        jobj[key] = VERSION_MAJOR * 100 + VERSION_MINOR + VERSION_PATCH / 100.0;
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

Status JsonController::initializeHome(JsonCommand& jcmd, JsonObject& jobj,
                                      const char* key, bool clear)
{
    Status status = STATUS_OK;
    if (clear) {
        for (QuadIndex i = 0; i < QUAD_ELEMENTS; i++) {
            machine.getMotorAxis(i).homing = false;
        }
    }
    if (strcmp("hom", key) == 0) {
        const char *s;
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["1"] = "";
            node["2"] = "";
            node["3"] = "";
            node["4"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = initializeHome(jcmd, kidObj, it->key, false);
                if (status != STATUS_BUSY_MOVING) {
                    return status;
                }
            }
        }
    } else {
        MotorIndex iMotor = machine.motorOfName(key + (strlen(key) - 1));
        if (iMotor == INDEX_NONE) {
            return jcmd.setError(STATUS_NO_MOTOR, key);
        }
        status = processHomeField(machine, iMotor, jcmd, jobj, key);
    }
    return status == STATUS_OK ? STATUS_BUSY_MOVING : status;
}

Status JsonController::processHome(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = jcmd.getStatus();
    switch (status) {
    case STATUS_BUSY_PARSED:
        status = initializeHome(jcmd, jobj, key, true);
        break;
    case STATUS_BUSY_MOVING:
    case STATUS_BUSY_OK:
    case STATUS_BUSY_CALIBRATING:
        status = machine.home(status);
        break;
    default:
        TESTCOUT1("status:", status);
        ASSERT(false);
        return jcmd.setError(STATUS_STATE, key);
    }
    return status;
}

Status JsonController::processEEPROMValue(JsonCommand& jcmd, JsonObject& jobj, const char* key, const char*addr) {
    Status status = STATUS_OK;
    JsonVariant &jvalue = jobj[key];
    if (addr && *addr == '!') {
        status = processObj(jcmd, jvalue);
        if (status != STATUS_OK) {
            return jcmd.setError(status, key);
        }
        TESTCOUT1("processEEPROMValue!:", addr);
        addr++;
    }
    if (!addr || *addr<'0' || '9'<*addr) {
        return STATUS_JSON_DIGIT;
    }
    long addrLong = strtol(addr, NULL, 10);
    if (addrLong<0 || EEPROM_END <= addrLong) {
        return STATUS_EEPROM_ADDR;
    }
    char buf[EEPROM_BYTES];
    buf[0] = 0;
    if (jvalue.is<JsonArray&>()) {
        JsonArray &jeep = jvalue;
        jeep.printTo(buf, EEPROM_BYTES);
    } else if (jvalue.is<JsonObject&>()) {
        JsonObject &jeep = jvalue;
        jeep.printTo(buf, EEPROM_BYTES);
    } else if (jvalue.is<const char *>()) {
        const char *s = jvalue;
        snprintf(buf, sizeof(buf), "%s", s);
    }
    if (!buf) {
        return STATUS_JSON_STRING;
    }
    if (buf[0] == 0) { // query
        uint8_t c = eeprom_read_byte((uint8_t*) addrLong);
        if (c && c != 255) {
            char *buf = jcmd.allocate(EEPROM_BYTES);
            if (!buf) {
                return jcmd.setError(STATUS_JSON_MEM3, key);
            }
            for (int16_t i=0; i<EEPROM_BYTES; i++) {
                c = eeprom_read_byte((uint8_t*) addrLong+i);
                if (c == 255 || c == 0) {
                    buf[i] = 0;
                    break;
                }
                buf[i] = c;
            }
            jobj[key] = buf;
        }
    } else {
        int16_t len = strlen(buf) + 1;
        if (len >= EEPROM_BYTES) {
            return jcmd.setError(STATUS_JSON_EEPROM, key);
        }
        for (int16_t i=0; i<len; i++) {
            eeprom_write_byte((uint8_t*)addrLong+i, buf[i]);
            TESTCOUT3("EEPROM[", ((int)addrLong+i), "]:",
                      (char) eeprom_read_byte((uint8_t *) addrLong+i),
                      " ",
                      (int) eeprom_read_byte((uint8_t *) addrLong+i)
                     );
        }
    }
    return status;
}

Status JsonController::initializeProbe(JsonCommand& jcmd, JsonObject& jobj,
                                       const char* key, bool clear)
{
    Status status = STATUS_OK;
    if (clear) {
        machine.op.probe.setup(machine.getMotorPosition());
    }
    if (strcmp("prb", key) == 0) {
        const char *s;
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["1"] = "";
            node["2"] = "";
            node["3"] = "";
            node["4"] = "";
            node["ip"] = "";
            node["pn"] = "";
            node["sd"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = initializeProbe(jcmd, kidObj, it->key, false);
                if (status < 0) {
                    return jcmd.setError(status, it->key);
                }
            }
            if (status == STATUS_BUSY_CALIBRATING && machine.op.probe.pinProbe==NOPIN) {
                return jcmd.setError(STATUS_FIELD_REQUIRED, "pn");
            }
        }
    } else if (strcmp("prbip", key) == 0 || strcmp("ip", key) == 0) {
        status = processField<bool, bool>(jobj, key, machine.op.probe.invertProbe);
    } else if (strcmp("prbpn", key) == 0 || strcmp("pn", key) == 0) {
        status = processField<PinType, int32_t>(jobj, key, machine.op.probe.pinProbe);
    } else if (strcmp("prbsd", key) == 0 || strcmp("sd", key) == 0) {
        status = processField<DelayMics, int32_t>(jobj, key, machine.searchDelay);
    } else {
        MotorIndex iMotor = machine.motorOfName(key + (strlen(key) - 1));
        if (iMotor == INDEX_NONE) {
            return jcmd.setError(STATUS_NO_MOTOR, key);
        }
        status = processProbeField(machine, iMotor, jcmd, jobj, key);
    }
    return status == STATUS_OK ? STATUS_BUSY_CALIBRATING : status;
}

Status JsonController::processProbe(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = jcmd.getStatus();
    switch (status) {
    case STATUS_BUSY_PARSED:
        status = initializeProbe(jcmd, jobj, key, true);
        break;
    case STATUS_BUSY_OK:
    case STATUS_BUSY_CALIBRATING:
        status = machine.probe(status);
        if (status == STATUS_OK) {
            JsonObject& kidObj = jobj[key];
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                MotorIndex iMotor = machine.motorOfName(it->key + (strlen(it->key) - 1));
                if (iMotor != INDEX_NONE) {
                    kidObj[it->key] = machine.getMotorAxis(iMotor).position;
                }
            }
        }
        break;
    default:
        ASSERT(false);
        return jcmd.setError(STATUS_STATE, key);
    }
    return status;
}

Status JsonController::processEEPROM(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    if (strcmp("eep", key) == 0) {
        JsonObject& kidObj = jobj[key];
        if (!kidObj.success()) {
            return jcmd.setError(STATUS_JSON_OBJECT, key);
        }
        for (JsonObject::iterator it = kidObj.begin(); status>=0 && it != kidObj.end(); ++it) {
            status = processEEPROMValue(jcmd, kidObj, it->key, it->key);
        }
    } else if (strncmp("eep",key,3) == 0) {
        status = processEEPROMValue(jcmd, jobj, key, key+3);
    } else {
        status = STATUS_UNRECOGNIZED_NAME;
    }
    if (status < 0) {
        return jcmd.setError(status, key);
    }
    return status;
}

Status JsonController::processIOPin(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    const char *pinStr = *key == 'd' || *key == 'a' ? key+1 : key+3;
    long pinLong = strtol(pinStr, NULL, 10);
    if (machine.isCorePin(pinLong)) {
        return jcmd.setError(STATUS_CORE_PIN, key);
    }
    if (pinLong < 0 || MAX_PIN < pinLong) {
        return jcmd.setError(STATUS_NO_SUCH_PIN, key);
    }
    int16_t pin = (int16_t) pinLong;
    const char *s = jobj[key];
    bool isAnalog = *key == 'a' || strncmp("ioa",key,3)==0;
    if (s && *s == 0) { // read
        if (isAnalog) {
            pinMode(pin+A0, INPUT);
            jobj[key] = analogRead(pin+A0);
        } else {
            pinMode(pin, INPUT);
            jobj[key] = (bool) digitalRead(pin);
        }
    } else if (isAnalog) {
        if (jobj[key].is<long>()) { // write
            long value = jobj[key];
            if (value < 0 || 255 < value) {
                return jcmd.setError(STATUS_JSON_255, key);
            }
            pinMode(pin+A0, OUTPUT);
            analogWrite(pin+A0, (int16_t) value);
        } else {
            return jcmd.setError(STATUS_JSON_255, key);
        }
    } else {
        if (jobj[key].is<bool>()) { // write
            bool value = jobj[key];
            pinMode(pin, OUTPUT);
            digitalWrite(pin, value);
        } else if (jobj[key].is<long>()) { // write
            bool value = (bool) (long)jobj[key];
            pinMode(pin, OUTPUT);
            digitalWrite(pin, value);
        } else {
            return jcmd.setError(STATUS_JSON_BOOL, key);
        }
    }
    return STATUS_OK;
}

Status JsonController::processIO(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_NOT_IMPLEMENTED;
    if (strcmp("io", key) == 0) {
        JsonObject& kidObj = jobj[key];
        if (!kidObj.success()) {
            return jcmd.setError(STATUS_JSON_OBJECT, key);
        }
        for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
            status = processIO(jcmd, kidObj, it->key);
        }
    } else if (strncmp("d",key,1)==0 || strncmp("iod",key,3)==0) {
        status = processIOPin(jcmd, jobj, key);
    } else if (strncmp("a",key,1)==0 || strncmp("ioa",key,3)==0) {
        status = processIOPin(jcmd, jobj, key);
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

Status JsonController::processDisplay(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    if (strcmp("dpy", key) == 0) {
        const char *s;
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["cb"] = "";
            node["cg"] = "";
            node["cr"] = "";
            node["dl"] = "";
            node["ds"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = processDisplay(jcmd, kidObj, it->key);
                if (status != STATUS_OK) {
                    return status;
                }
            }
        }
    } else if (strcmp("cb", key) == 0 || strcmp("dpycb", key) == 0) {
        status = processField<uint8_t, int32_t>(jobj, key, machine.pDisplay->cameraB);
    } else if (strcmp("cg", key) == 0 || strcmp("dpycg", key) == 0) {
        status = processField<uint8_t, int32_t>(jobj, key, machine.pDisplay->cameraG);
    } else if (strcmp("cr", key) == 0 || strcmp("dpycr", key) == 0) {
        status = processField<uint8_t, int32_t>(jobj, key, machine.pDisplay->cameraR);
    } else if (strcmp("dl", key) == 0 || strcmp("dpydl", key) == 0) {
        status = processField<uint8_t, int32_t>(jobj, key, machine.pDisplay->level);
    } else if (strcmp("ds", key) == 0 || strcmp("dpyds", key) == 0) {
        const char *s;
        bool isAssignment = (!(s = jobj[key]) || *s != 0);
        status = processField<uint8_t, int32_t>(jobj, key, machine.pDisplay->status);
        if (isAssignment) {
            switch (machine.pDisplay->status) {
            case DISPLAY_WAIT_IDLE:
                status = STATUS_WAIT_IDLE;
                break;
            case DISPLAY_WAIT_ERROR:
                status = STATUS_WAIT_ERROR;
                break;
            case DISPLAY_WAIT_OPERATOR:
                status = STATUS_WAIT_OPERATOR;
                break;
            case DISPLAY_BUSY_MOVING:
                status = STATUS_WAIT_MOVING;
                break;
            case DISPLAY_BUSY:
                status = STATUS_WAIT_BUSY;
                break;
            case DISPLAY_WAIT_CAMERA:
                status = STATUS_WAIT_CAMERA;
                break;
            }
        }
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

Status JsonController::cancel(JsonCommand& jcmd, Status cause) {
    sendResponse(jcmd, cause);
    return STATUS_WAIT_CANCELLED;
}

void JsonController::sendResponse(JsonCommand &jcmd, Status status) {
    jcmd.setStatus(status);
    if (status >= 0) {
        if (jcmd.responseAvailable() < 1) {
            TESTCOUT2("response available:", jcmd.responseAvailable(), " capacity:", jcmd.responseCapacity());
            jcmd.setStatus(STATUS_JSON_MEM1);
        } else if (jcmd.requestAvailable() < 1) {
            TESTCOUT2("request available:", jcmd.requestAvailable(), " capacity:", jcmd.requestCapacity());
            jcmd.setStatus(STATUS_JSON_MEM2);
        }
    }
    if (machine.jsonPrettyPrint) {
        jcmd.response().prettyPrintTo(Serial);
    } else {
        jcmd.response().printTo(Serial);
    }
    jcmd.responseClear();
    Serial.println();
}

Status JsonController::processObj(JsonCommand& jcmd, JsonObject&jobj) {
    JsonVariant node;
    node = jobj;
    Status status = STATUS_OK;

    for (JsonObject::iterator it = jobj.begin(); status >= 0 && it != jobj.end(); ++it) {
        if (strcmp("dvs", it->key) == 0) {
            status = processStroke(jcmd, jobj, it->key);
        } else if (strncmp("mov", it->key, 3) == 0) {
            status = PHMoveTo(machine).process(jcmd, jobj, it->key);
        } else if (strncmp("hom", it->key, 3) == 0) {
            status = processHome(jcmd, jobj, it->key);
        } else if (strncmp("tst", it->key, 3) == 0) {
            status = processTest(jcmd, jobj, it->key);
        } else if (strncmp("sys", it->key, 3) == 0) {
            status = processSys(jcmd, jobj, it->key);
        } else if (strncmp("dpy", it->key, 3) == 0) {
            status = processDisplay(jcmd, jobj, it->key);
        } else if (strncmp("mpo", it->key, 3) == 0) {
            switch (machine.topology) {
            case MTO_RAW:
            default:
                status = processPosition(jcmd, jobj, it->key);
                break;
            case MTO_FPD:
                status = processPosition_MTO_FPD(jcmd, jobj, it->key);
                break;
            }
        } else if (strncmp("io", it->key, 2) == 0) {
            status = processIO(jcmd, jobj, it->key);
        } else if (strncmp("eep", it->key, 3) == 0) {
            status = processEEPROM(jcmd, jobj, it->key);
        } else if (strncmp("dim", it->key, 3) == 0) {
            switch (machine.topology) {
            case MTO_RAW:
            default:
                status = jcmd.setError(STATUS_TOPOLOGY_NAME, it->key);
                break;
            case MTO_FPD:
                status = processDimension_MTO_FPD(jcmd, jobj, it->key);
                break;
            }
        } else if (strncmp("prb", it->key, 3) == 0) {
            switch (machine.topology) {
            case MTO_RAW:
            default:
                status = processProbe(jcmd, jobj, it->key);
                break;
            case MTO_FPD:
                status = processProbe_MTO_FPD(jcmd, jobj, it->key);
                break;
            }
		} else if (strcmp("idl", it->key) == 0) {
			int16_t ms = it->value;
			delay(ms);
		} else if (strcmp("cmt", it->key) == 0) {
			if (OUTPUT_CMT==(machine.outputMode&OUTPUT_CMT)) {
				const char *s = it->value;
				Serial.println(s);
			}
			status = STATUS_OK;
		} else if (strcmp("msg", it->key) == 0) {
			const char *s = it->value;
			Serial.println(s);
			status = STATUS_OK;
        } else {
            switch (it->key[0]) {
            case '1':
            case '2':
            case '3':
            case '4':
                status = processMotor(jcmd, jobj, it->key, it->key[0]);
                break;
            case 'x':
            case 'y':
            case 'z':
            case 'a':
            case 'b':
            case 'c':
                status = processAxis(jcmd, jobj, it->key, it->key[0]);
                break;
            default:
                status = jcmd.setError(STATUS_UNRECOGNIZED_NAME, it->key);
                break;
            }
        }
    }

    return status;
}

Status JsonController::process(JsonCommand& jcmd) {
    Status status = STATUS_OK;
    JsonVariant &jroot = jcmd.requestRoot();

    if (jroot.is<JsonObject&>()) {
        JsonObject& jobj = jroot;
        status = processObj(jcmd, jobj);
    } else if (jroot.is<JsonArray&>()) {
        JsonArray& jarr = jroot;
        if (jcmd.cmdIndex < jarr.size()) {
            JsonObject& jobj = jarr[jcmd.cmdIndex];
            jcmd.jResponseRoot["r"] = jobj;
            status = processObj(jcmd, jobj);
            //TESTCOUT3("JsonController::process(", (int) jcmd.cmdIndex+1,
            //" of ", jarr.size(), ") status:", status);
            if (status == STATUS_OK) {
                bool isLast = jcmd.cmdIndex >= jarr.size()-1;
                if (!isLast && OUTPUT_ARRAYN==(machine.outputMode&OUTPUT_ARRAYN)) {
					jcmd.setTicks();
                    sendResponse(jcmd, status);
                }
                status = STATUS_BUSY_PARSED;
                jcmd.cmdIndex++;
            }
        } else {
            status = STATUS_OK;
        }
    } else {
        status = STATUS_JSON_CMD;
    }

    jcmd.setTicks();
    jcmd.setStatus(status);

    if (!isProcessing(status)) {
        sendResponse(jcmd,status);
    }

    return status;
}

//////////////// MTO_FPD /////////
Status JsonController::processPosition_MTO_FPD(JsonCommand &jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    const char *axisStr = key + strlen(key) - 1;
    const char *s;
    if (strlen(key) == 3) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["1"] = "";
            node["2"] = "";
            node["3"] = "";
            node["4"] = "";
            node["x"] = "";
            node["y"] = "";
            node["z"] = "";
            if (!node.at("4").success()) {
                return jcmd.setError(STATUS_JSON_KEY, "4");
            }
        }
        JsonObject& kidObj = jobj[key];
        if (!kidObj.success()) {
            return jcmd.setError(STATUS_POSITION_ERROR, key);
        }
        for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
            status = processPosition_MTO_FPD(jcmd, kidObj, it->key);
            if (status != STATUS_OK) {
                return status;
            }
        }
    } else if (strcmp("1", axisStr) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, machine.axis[0].position);
    } else if (strcmp("2", axisStr) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, machine.axis[1].position);
    } else if (strcmp("3", axisStr) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, machine.axis[2].position);
    } else if (strcmp("4", axisStr) == 0) {
        status = processField<StepCoord, int32_t>(jobj, key, machine.axis[3].position);
    } else if (strcmp("x", axisStr) == 0) {
        XYZ3D xyz(machine.getXYZ3D());
        if (!xyz.isValid()) {
            return jcmd.setError(STATUS_KINEMATIC_XYZ, key);
        }
        PH5TYPE value = xyz.x;
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        if (value != xyz.x) {
            status = jcmd.setError(STATUS_OUTPUT_FIELD, key);
        }
    } else if (strcmp("y", axisStr) == 0) {
        XYZ3D xyz(machine.getXYZ3D());
        if (!xyz.isValid()) {
            return jcmd.setError(STATUS_KINEMATIC_XYZ, key);
        }
        PH5TYPE value = xyz.y;
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        if (value != xyz.y) {
            status = jcmd.setError(STATUS_OUTPUT_FIELD, key);
        }
    } else if (strcmp("z", axisStr) == 0) {
        XYZ3D xyz(machine.getXYZ3D());
        if (!xyz.isValid()) {
            return jcmd.setError(STATUS_KINEMATIC_XYZ, key);
        }
        PH5TYPE value = xyz.z;
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        if (value != xyz.z) {
            status = jcmd.setError(STATUS_OUTPUT_FIELD, key);
        }
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

Status JsonController::initializeProbe_MTO_FPD(JsonCommand& jcmd, JsonObject& jobj,
        const char* key, bool clear)
{
    Status status = STATUS_OK;
    OpProbe &probe = machine.op.probe;

    if (clear) {
        Quad<StepCoord> curPos = machine.getMotorPosition();
        probe.setup(curPos);
    }
    Step3D probeEnd(probe.end.value[0], probe.end.value[1], probe.end.value[2]);
    XYZ3D xyzEnd = machine.delta.calcXYZ(probeEnd);
    const char *s;
    if (strcmp("prb", key) == 0) {
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            xyzEnd.z = machine.delta.getMinZ(xyzEnd.z, xyzEnd.y);
            node["1"] = "";
            node["2"] = "";
            node["3"] = "";
            node["4"] = "";
            node["ip"] = "";
            node["pn"] = machine.op.probe.pinProbe;
            node["sd"] = "";
            node["x"] = xyzEnd.x;
            node["y"] = xyzEnd.y;
            node["z"] = xyzEnd.z;
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = initializeProbe_MTO_FPD(jcmd, kidObj, it->key, false);
                if (status < 0) {
                    return jcmd.setError(status, it->key);
                }
            }
            if (status == STATUS_BUSY_CALIBRATING && machine.op.probe.pinProbe==NOPIN) {
                return jcmd.setError(STATUS_FIELD_REQUIRED, "pn");
            }
        }
    } else if (strcmp("prbip", key) == 0 || strcmp("ip", key) == 0) {
        status = processField<bool, bool>(jobj, key, machine.op.probe.invertProbe);
    } else if (strcmp("prbpn", key) == 0 || strcmp("pn", key) == 0) {
        status = processField<PinType, int32_t>(jobj, key, machine.op.probe.pinProbe);
    } else if (strcmp("prbsd", key) == 0 || strcmp("sd", key) == 0) {
        status = processField<DelayMics, int32_t>(jobj, key, machine.searchDelay);
    } else if (strcmp("prbx", key) == 0 || strcmp("x", key) == 0) {
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, xyzEnd.x);
    } else if (strcmp("prby", key) == 0 || strcmp("y", key) == 0) {
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, xyzEnd.y);
    } else if (strcmp("prbz", key) == 0 || strcmp("z", key) == 0) {
        machine.op.probe.dataSource = PDS_Z;
        xyzEnd.z = machine.delta.getMinZ(xyzEnd.x, xyzEnd.y);
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, xyzEnd.z);
    } else {
        MotorIndex iMotor = machine.motorOfName(key + (strlen(key) - 1));
        if (iMotor != INDEX_NONE) {
            if ((s = jobj[key]) && *s == 0) {
                // query is fine
            } else {
                return jcmd.setError(STATUS_OUTPUT_FIELD, key);
            }
        } else {
            return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
        }
    }

    // This code only works for probes along a single cartesian axis
    Step3D pEnd = machine.delta.calcPulses(xyzEnd);
    machine.op.probe.end.value[0] = pEnd.p1;
    machine.op.probe.end.value[1] = pEnd.p2;
    machine.op.probe.end.value[2] = pEnd.p3;
    TESTCOUT3("pEnd:", pEnd.p1, ", ", pEnd.p2, ", ", pEnd.p3);
    machine.op.probe.maxDelta = 0;
    for (MotorIndex iMotor=0; iMotor<3; iMotor++) {
        StepCoord delta = machine.op.probe.end.value[iMotor] - machine.op.probe.start.value[iMotor];
        machine.op.probe.maxDelta = max((StepCoord)abs(machine.op.probe.maxDelta), delta);
    }

    return status == STATUS_OK ? STATUS_BUSY_CALIBRATING : status;
}

Status JsonController::finalizeProbe_MTO_FPD(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    XYZ3D xyz = machine.getXYZ3D();
    if (!xyz.isValid()) {
        return jcmd.setError(STATUS_KINEMATIC_XYZ, key);
    }
    if (strcmp("prbx",key) == 0 || strcmp("x",key) == 0) {
        jobj[key] = xyz.x;
    } else if (strcmp("prby",key) == 0 || strcmp("y",key) == 0) {
        jobj[key] = xyz.y;
    } else if (strcmp("prbz",key) == 0 || strcmp("z",key) == 0) {
        jobj[key] = xyz.z;
    } else if (strcmp("1",key) == 0) {
        jobj[key] = machine.getMotorAxis(0).position;
    } else if (strcmp("2",key) == 0) {
        jobj[key] = machine.getMotorAxis(1).position;
    } else if (strcmp("3",key) == 0) {
        jobj[key] = machine.getMotorAxis(2).position;
    } else if (strcmp("4",key) == 0) {
        jobj[key] = machine.getMotorAxis(3).position;
    }
    return STATUS_OK;
}

Status JsonController::processProbe_MTO_FPD(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = jcmd.getStatus();
    switch (status) {
    case STATUS_BUSY_PARSED:
        status = initializeProbe_MTO_FPD(jcmd, jobj, key, true);
        break;
    case STATUS_BUSY_OK:
    case STATUS_BUSY_CALIBRATING:
        status = machine.probe(status);
        if (status == STATUS_OK) {
            if (jobj[key].is<JsonObject&>()) {
                JsonObject &kidObj = jobj[key];
                for (JsonObject::iterator it = kidObj.begin(); status == STATUS_OK && it != kidObj.end(); ++it) {
                    status = finalizeProbe_MTO_FPD(jcmd, kidObj, it->key);
                }
            } else {
                status = finalizeProbe_MTO_FPD(jcmd, jobj, key);
            }
        }
        break;
    default:
        ASSERT(false);
        return jcmd.setError(STATUS_STATE, key);
    }
    return status;
}

Status JsonController::processDimension_MTO_FPD(JsonCommand& jcmd, JsonObject& jobj, const char* key) {
    Status status = STATUS_OK;
    if (strcmp("dim", key) == 0) {
        const char *s;
        if ((s = jobj[key]) && *s == 0) {
            JsonObject& node = jobj.createNestedObject(key);
            node["e"] = "";
            node["f"] = "";
            node["gr"] = "";
            node["ha1"] = "";
            node["ha2"] = "";
            node["ha3"] = "";
            node["mi"] = "";
            node["pd"] = "";
            node["re"] = "";
            node["rf"] = "";
            node["st"] = "";
            node["zo"] = "";
        }
        JsonObject& kidObj = jobj[key];
        if (kidObj.success()) {
            for (JsonObject::iterator it = kidObj.begin(); it != kidObj.end(); ++it) {
                status = processDimension_MTO_FPD(jcmd, kidObj, it->key);
                if (status != STATUS_OK) {
                    return status;
                }
            }
        }
    } else if (strcmp("zo", key) == 0 || strcmp("zoffset", key) == 0) {
        PH5TYPE value = machine.delta.getZOffset();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        machine.delta.setZOffset(value);
    } else if (strcmp("e", key) == 0 || strcmp("dime", key) == 0) {
        PH5TYPE value = machine.delta.getEffectorTriangleSide();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        machine.delta.setEffectorTriangleSide(value);
    } else if (strcmp("f", key) == 0 || strcmp("dimf", key) == 0) {
        PH5TYPE value = machine.delta.getBaseTriangleSide();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        machine.delta.setBaseTriangleSide(value);
    } else if (strcmp("gr", key) == 0 || strcmp("dimgr", key) == 0) {
        PH5TYPE value = machine.delta.getGearRatio();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        machine.delta.setGearRatio(value);
    } else if (strcmp("ha1", key) == 0 || strcmp("dimha1", key) == 0) {
        Angle3D homeAngles = machine.delta.getHomeAngles();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, homeAngles.theta1);
        machine.delta.setHomeAngles(homeAngles);
    } else if (strcmp("ha2", key) == 0 || strcmp("dimha2", key) == 0) {
        Angle3D homeAngles = machine.delta.getHomeAngles();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, homeAngles.theta2);
        machine.delta.setHomeAngles(homeAngles);
    } else if (strcmp("ha3", key) == 0 || strcmp("dimha3", key) == 0) {
        Angle3D homeAngles = machine.delta.getHomeAngles();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, homeAngles.theta3);
        machine.delta.setHomeAngles(homeAngles);
    } else if (strcmp("mi", key) == 0 || strcmp("dimmi", key) == 0) {
        int16_t value = machine.delta.getMicrosteps();
        status = processField<int16_t, int16_t>(jobj, key, value);
        machine.delta.setMicrosteps(value);
    } else if (strcmp("pd", key) == 0 || strcmp("dimpd", key) == 0) {
        const char *s;
        if ((s = jobj[key]) && *s == 0) {
            JsonArray &jarr = jobj.createNestedArray(key);
            for (int16_t i=0; i<PROBE_DATA; i++) {
                jarr.add(machine.op.probe.probeData[i]);
            }
        } else {
            status = jcmd.setError(STATUS_OUTPUT_FIELD, key);
        }
    } else if (strcmp("re", key) == 0 || strcmp("dimre", key) == 0) {
        PH5TYPE value = machine.delta.getEffectorLength();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        machine.delta.setEffectorLength(value);
    } else if (strcmp("rf", key) == 0 || strcmp("dimrf", key) == 0) {
        PH5TYPE value = machine.delta.getBaseArmLength();
        status = processField<PH5TYPE, PH5TYPE>(jobj, key, value);
        machine.delta.setBaseArmLength(value);
    } else if (strcmp("st", key) == 0 || strcmp("dimst", key) == 0) {
        int16_t value = machine.delta.getSteps360();
        status = processField<int16_t, int16_t>(jobj, key, value);
        machine.delta.setSteps360(value);
    } else {
        return jcmd.setError(STATUS_UNRECOGNIZED_NAME, key);
    }
    return status;
}

