#include "sn7408.h"
#include "../ic_registry.h"

/* Real TI SN7408 pinout (Quad 2-Input AND Gate) drives eval() indexing below
   (array order must stay ascending by pin_number: tmp[i] == pin_number i+1).
   `side` is chosen schematically (inputs left, outputs+power right) rather
   than by physical DIP layout - purely a rendering choice, free per IC. */
static const IC_PinDef k_sn7408_pins[14] = {
    { 1,  "1A",  PIN_INPUT,  0 },
    { 2,  "1B",  PIN_INPUT,  0 },
    { 3,  "1Y",  PIN_OUTPUT, 1 },
    { 4,  "2A",  PIN_INPUT,  0 },
    { 5,  "2B",  PIN_INPUT,  0 },
    { 6,  "2Y",  PIN_OUTPUT, 1 },
    { 7,  "GND", PIN_POWER,  1 },
    { 8,  "3Y",  PIN_OUTPUT, 1 },
    { 9,  "3A",  PIN_INPUT,  0 },
    { 10, "3B",  PIN_INPUT,  0 },
    { 11, "4Y",  PIN_OUTPUT, 1 },
    { 12, "4A",  PIN_INPUT,  0 },
    { 13, "4B",  PIN_INPUT,  0 },
    { 14, "VCC", PIN_POWER,  1 },
};

static SignalValue and2(SignalValue a, SignalValue b) {
    if (a == SIG_CONFLICT || b == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_LOW || b == SIG_LOW) return SIG_LOW;
    if (a == SIG_UNKNOWN || b == SIG_UNKNOWN) return SIG_UNKNOWN;
    return SIG_HIGH;
}

static void sn7408_eval(SignalValue *v, int pin_count) {
    (void)pin_count; /* always 14 for this IC */
    v[2]  = and2(v[0], v[1]);   /* 1Y = 1A & 1B  */
    v[5]  = and2(v[3], v[4]);   /* 2Y = 2A & 2B  */
    v[7]  = and2(v[8], v[9]);   /* 3Y = 3A & 3B  */
    v[10] = and2(v[11], v[12]); /* 4Y = 4A & 4B  */
}

static const IC_Def k_sn7408_def = {
    .name = "SN7408",
    .pin_count = 14,
    .pins = k_sn7408_pins,
    .width = 5,
    .height = 10,
    .eval = sn7408_eval,
};

void ic_sn7408_register(void) {
    ic_registry_register(&k_sn7408_def);
}
