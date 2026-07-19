#include "sn74hc244.h"
#include "../ic_registry.h"

/* Real TI SN74HC244N pinout (Octal Buffer/Line Driver, 3-State), matching the
   physical 20-pin DIP: pins 1-10 down the left side, 11-20 back up the right
   (see component_init_ic). Unlike the simpler gate ICs, the two 4-buffer
   groups interleave on the package (1Ax/2Yx share one side, 2Ax/1Yx the
   other) for shorter PCB routing on the real chip - that's a genuine
   datasheet quirk, not a mistake. Array order must stay ascending by
   pin_number: tmp[i] == pin_number i+1. */
static const IC_PinDef k_sn74hc244_pins[20] = {
    { .pin_number = 1, .name = "1G", .direction = PIN_INPUT },  /* output enable, active low, group 1 */
    { .pin_number = 2, .name = "1A1", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "2Y4", .direction = PIN_OUTPUT },
    { .pin_number = 4, .name = "1A2", .direction = PIN_INPUT },
    { .pin_number = 5, .name = "2Y3", .direction = PIN_OUTPUT },
    { .pin_number = 6, .name = "1A3", .direction = PIN_INPUT },
    { .pin_number = 7, .name = "2Y2", .direction = PIN_OUTPUT },
    { .pin_number = 8, .name = "1A4", .direction = PIN_INPUT },
    { .pin_number = 9, .name = "2Y1", .direction = PIN_OUTPUT },
    { .pin_number = 10, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 11, .name = "2A1", .direction = PIN_INPUT },
    { .pin_number = 12, .name = "1Y4", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "2A2", .direction = PIN_INPUT },
    { .pin_number = 14, .name = "1Y3", .direction = PIN_OUTPUT },
    { .pin_number = 15, .name = "2A3", .direction = PIN_INPUT },
    { .pin_number = 16, .name = "1Y2", .direction = PIN_OUTPUT },
    { .pin_number = 17, .name = "2A4", .direction = PIN_INPUT },
    { .pin_number = 18, .name = "1Y1", .direction = PIN_OUTPUT },
    { .pin_number = 19, .name = "2G", .direction = PIN_INPUT },  /* output enable, active low, group 2 */
    { .pin_number = 20, .name = "VCC", .direction = PIN_POWER },
};

/* A tri-state buffer isn't a logic function - it's the whole reason SIG_HIZ
   exists (see types.h): disabled outputs must stop driving their net rather
   than settle on some value, so two of these wired to the same bus can share
   it without a permanent SIG_CONFLICT. UNKNOWN enable is treated the same as
   disabled (HI-Z) rather than guessing which way an indeterminate active-low
   enable would resolve. */
static SignalValue buf_tristate(SignalValue oe, SignalValue a) {
    if (oe == SIG_CONFLICT) return SIG_CONFLICT;
    if (oe == SIG_HIGH || oe == SIG_UNKNOWN) return SIG_HIZ;
    return a; /* oe == SIG_LOW: enabled, straight non-inverting passthrough */
}

static void sn74hc244_eval(SignalValue *v, int pin_count) {
    (void)pin_count; /* always 20 for this IC */
    SignalValue g1 = v[0], g2 = v[18];
    v[17] = buf_tristate(g1, v[1]); /* 1Y1 = 1A1 */
    v[15] = buf_tristate(g1, v[3]); /* 1Y2 = 1A2 */
    v[13] = buf_tristate(g1, v[5]); /* 1Y3 = 1A3 */
    v[11] = buf_tristate(g1, v[7]); /* 1Y4 = 1A4 */
    v[8]  = buf_tristate(g2, v[10]); /* 2Y1 = 2A1 */
    v[6]  = buf_tristate(g2, v[12]); /* 2Y2 = 2A2 */
    v[4]  = buf_tristate(g2, v[14]); /* 2Y3 = 2A3 */
    v[2]  = buf_tristate(g2, v[16]); /* 2Y4 = 2A4 */
}

static const IC_Def k_sn74hc244_def = {
    .name = "SN74HC244N",
    .pin_count = 20,
    .pins = k_sn74hc244_pins,
    .eval = sn74hc244_eval,
};

void ic_sn74hc244_register(void) {
    ic_registry_register(&k_sn74hc244_def);
}
