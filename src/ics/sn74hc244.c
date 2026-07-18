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
    { 1,  "1G",  PIN_INPUT },  /* output enable, active low, group 1 */
    { 2,  "1A1", PIN_INPUT },
    { 3,  "2Y4", PIN_OUTPUT },
    { 4,  "1A2", PIN_INPUT },
    { 5,  "2Y3", PIN_OUTPUT },
    { 6,  "1A3", PIN_INPUT },
    { 7,  "2Y2", PIN_OUTPUT },
    { 8,  "1A4", PIN_INPUT },
    { 9,  "2Y1", PIN_OUTPUT },
    { 10, "GND", PIN_POWER },
    { 11, "2A1", PIN_INPUT },
    { 12, "1Y4", PIN_OUTPUT },
    { 13, "2A2", PIN_INPUT },
    { 14, "1Y3", PIN_OUTPUT },
    { 15, "2A3", PIN_INPUT },
    { 16, "1Y2", PIN_OUTPUT },
    { 17, "2A4", PIN_INPUT },
    { 18, "1Y1", PIN_OUTPUT },
    { 19, "2G",  PIN_INPUT },  /* output enable, active low, group 2 */
    { 20, "VCC", PIN_POWER },
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
