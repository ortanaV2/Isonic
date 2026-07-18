#include "cd74hc04.h"
#include "../ic_registry.h"

/* Real CD74HC04E pinout (TI Hex Inverter) - same 74HC04 function and pinout
   as the more commonly cited SN74HC04N, just TI's alternate "CD"-prefixed
   catalog branding (picked because it's the one actually in stock as a
   through-hole DIP right now). Matches the physical 14-pin DIP: pins 1-7
   down the left side, 8-14 back up the right (see component_init_ic) - also
   drives eval() indexing below (array order must stay ascending by
   pin_number: tmp[i] == pin_number i+1). */
static const IC_PinDef k_cd74hc04_pins[14] = {
    { 1,  "1A",  PIN_INPUT },
    { 2,  "1Y",  PIN_OUTPUT },
    { 3,  "2A",  PIN_INPUT },
    { 4,  "2Y",  PIN_OUTPUT },
    { 5,  "3A",  PIN_INPUT },
    { 6,  "3Y",  PIN_OUTPUT },
    { 7,  "GND", PIN_POWER },
    { 8,  "4Y",  PIN_OUTPUT },
    { 9,  "4A",  PIN_INPUT },
    { 10, "5Y",  PIN_OUTPUT },
    { 11, "5A",  PIN_INPUT },
    { 12, "6Y",  PIN_OUTPUT },
    { 13, "6A",  PIN_INPUT },
    { 14, "VCC", PIN_POWER },
};

static SignalValue not1(SignalValue a) {
    if (a == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_UNKNOWN) return SIG_UNKNOWN;
    return (a == SIG_HIGH) ? SIG_LOW : SIG_HIGH;
}

static void cd74hc04_eval(SignalValue *v, int pin_count) {
    (void)pin_count; /* always 14 for this IC */
    v[1]  = not1(v[0]);  /* 1Y = NOT 1A */
    v[3]  = not1(v[2]);  /* 2Y = NOT 2A */
    v[5]  = not1(v[4]);  /* 3Y = NOT 3A */
    v[7]  = not1(v[8]);  /* 4Y = NOT 4A */
    v[9]  = not1(v[10]); /* 5Y = NOT 5A */
    v[11] = not1(v[12]); /* 6Y = NOT 6A */
}

static const IC_Def k_cd74hc04_def = {
    .name = "CD74HC04E",
    .pin_count = 14,
    .pins = k_cd74hc04_pins,
    .eval = cd74hc04_eval,
};

void ic_cd74hc04_register(void) {
    ic_registry_register(&k_cd74hc04_def);
}
