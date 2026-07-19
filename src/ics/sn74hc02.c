#include "sn74hc02.h"
#include "../ic_registry.h"

/* Real TI SN74HC02N pinout (Quad 2-Input NOR Gate) - unlike its 08/00/32/86
   14-pin family siblings, the 02's die layout puts gates 1 and 2's outputs
   BEFORE their inputs, but gates 3 and 4 revert to input-then-output (the
   two halves of the package are NOT symmetric with each other - verified
   against the real datasheet pin table, this asymmetry is a genuine die-
   layout quirk of this specific part, not a copy-paste error). Matches the
   physical 14-pin DIP: pins 1-7 down the left side, 8-14 back up the right -
   see component_init_ic. Array order must stay ascending by pin_number:
   tmp[i] == pin_number i+1. */
static const IC_PinDef k_sn74hc02_pins[14] = {
    { .pin_number = 1, .name = "1Y", .direction = PIN_OUTPUT },
    { .pin_number = 2, .name = "1A", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "1B", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "2Y", .direction = PIN_OUTPUT },
    { .pin_number = 5, .name = "2A", .direction = PIN_INPUT },
    { .pin_number = 6, .name = "2B", .direction = PIN_INPUT },
    { .pin_number = 7, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 8, .name = "3A", .direction = PIN_INPUT },
    { .pin_number = 9, .name = "3B", .direction = PIN_INPUT },
    { .pin_number = 10, .name = "3Y", .direction = PIN_OUTPUT },
    { .pin_number = 11, .name = "4A", .direction = PIN_INPUT },
    { .pin_number = 12, .name = "4B", .direction = PIN_INPUT },
    { .pin_number = 13, .name = "4Y", .direction = PIN_OUTPUT },
    { .pin_number = 14, .name = "VCC", .direction = PIN_POWER },
};

static SignalValue nor2(SignalValue a, SignalValue b) {
    if (a == SIG_CONFLICT || b == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_HIGH || b == SIG_HIGH) return SIG_LOW;
    if (a == SIG_UNKNOWN || b == SIG_UNKNOWN) return SIG_UNKNOWN;
    return SIG_HIGH;
}

static void sn74hc02_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 14 for this IC */
    (void)state;
    v[0]  = nor2(v[1], v[2]);   /* 1Y = NOT(1A | 1B) */
    v[3]  = nor2(v[4], v[5]);   /* 2Y = NOT(2A | 2B) */
    v[9]  = nor2(v[7], v[8]);   /* 3Y = NOT(3A | 3B) */
    v[12] = nor2(v[10], v[11]); /* 4Y = NOT(4A | 4B) */
}

static const IC_Def k_sn74hc02_def = {
    .name = "SN74HC02N",
    .pin_count = 14,
    .pins = k_sn74hc02_pins,
    .eval = sn74hc02_eval,
};

void ic_sn74hc02_register(void) {
    ic_registry_register(&k_sn74hc02_def);
}
