#ifndef ISONIC_TYPES_H
#define ISONIC_TYPES_H

typedef enum {
    PIN_INPUT,
    PIN_OUTPUT,
    PIN_POWER
} PinDirection;

typedef enum {
    SIG_LOW = 0,
    SIG_HIGH = 1,
    SIG_UNKNOWN = 2,
    SIG_CONFLICT = 3
} SignalValue;

#endif
