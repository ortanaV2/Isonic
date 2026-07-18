#include "sn74hc08.h"
#include "../ic_registry.h"

/* Real TI SN74HC08N pinout (Quad 2-Input AND Gate), matching the physical
   14-pin DIP: pins 1-7 down the left side, 8-14 back up the right (see
   component_init_ic) - also drives eval() indexing below (array order must
   stay ascending by pin_number: tmp[i] == pin_number i+1). */
static const IC_PinDef k_sn74hc08_pins[14] = {
    { 1,  "1A",  PIN_INPUT },
    { 2,  "1B",  PIN_INPUT },
    { 3,  "1Y",  PIN_OUTPUT },
    { 4,  "2A",  PIN_INPUT },
    { 5,  "2B",  PIN_INPUT },
    { 6,  "2Y",  PIN_OUTPUT },
    { 7,  "GND", PIN_POWER },
    { 8,  "3Y",  PIN_OUTPUT },
    { 9,  "3A",  PIN_INPUT },
    { 10, "3B",  PIN_INPUT },
    { 11, "4Y",  PIN_OUTPUT },
    { 12, "4A",  PIN_INPUT },
    { 13, "4B",  PIN_INPUT },
    { 14, "VCC", PIN_POWER },
};

static SignalValue and2(SignalValue a, SignalValue b) {
    if (a == SIG_CONFLICT || b == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_LOW || b == SIG_LOW) return SIG_LOW;
    if (a == SIG_UNKNOWN || b == SIG_UNKNOWN) return SIG_UNKNOWN;
    return SIG_HIGH;
}

static void sn74hc08_eval(SignalValue *v, int pin_count) {
    (void)pin_count; /* always 14 for this IC */
    v[2]  = and2(v[0], v[1]);   /* 1Y = 1A & 1B  */
    v[5]  = and2(v[3], v[4]);   /* 2Y = 2A & 2B  */
    v[7]  = and2(v[8], v[9]);   /* 3Y = 3A & 3B  */
    v[10] = and2(v[11], v[12]); /* 4Y = 4A & 4B  */
}

static const IC_Def k_sn74hc08_def = {
    .name = "SN74HC08N",
    .pin_count = 14,
    .pins = k_sn74hc08_pins,
    .eval = sn74hc08_eval,
};

void ic_sn74hc08_register(void) {
    ic_registry_register(&k_sn74hc08_def);
}
