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
    SIG_CONFLICT = 3,
    SIG_HIZ = 4 /* tri-stated output (see SN74HC244N) - doesn't drive its net, same as not being connected at all */
} SignalValue;

#endif
