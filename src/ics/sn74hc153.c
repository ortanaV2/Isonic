#include "sn74hc153.h"
#include "../ic_registry.h"

/* Real TI SN74HC153N pinout (Dual 4-Input Multiplexer), matching the
   physical 16-pin DIP: pins 1-8 down the left side, 9-16 back up the right
   (see component_init_ic) - also drives eval() indexing below (array order
   must stay ascending by pin_number: tmp[i] == pin_number i+1). The two
   4:1 sections share both select lines (A, B) but have independent
   active-low enables (1G, 2G) and independent outputs. */
static const IC_PinDef k_sn74hc153_pins[16] = {
    { 1,  "1G",  PIN_INPUT },  /* enable, active low, section 1 */
    { 2,  "B",   PIN_INPUT },  /* select, MSB, shared */
    { 3,  "1C3", PIN_INPUT },
    { 4,  "1C2", PIN_INPUT },
    { 5,  "1C1", PIN_INPUT },
    { 6,  "1C0", PIN_INPUT },
    { 7,  "1Y",  PIN_OUTPUT },
    { 8,  "GND", PIN_POWER },
    { 9,  "2Y",  PIN_OUTPUT },
    { 10, "2C0", PIN_INPUT },
    { 11, "2C1", PIN_INPUT },
    { 12, "2C2", PIN_INPUT },
    { 13, "2C3", PIN_INPUT },
    { 14, "A",   PIN_INPUT },  /* select, LSB, shared */
    { 15, "2G",  PIN_INPUT },  /* enable, active low, section 2 */
    { 16, "VCC", PIN_POWER },
};

/* Selects one of 4 lines by 2-bit address (b is MSB, a is LSB) - CONFLICT/
   UNKNOWN on either select line propagates to the whole selection, same
   priority order used by the simple gate ICs. */
static SignalValue mux4(const SignalValue *c, SignalValue a, SignalValue b) {
    if (a == SIG_CONFLICT || b == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_UNKNOWN || b == SIG_UNKNOWN) return SIG_UNKNOWN;
    int sel = (b == SIG_HIGH ? 2 : 0) | (a == SIG_HIGH ? 1 : 0);
    return c[sel];
}

static SignalValue mux_section(SignalValue enable, const SignalValue *c, SignalValue a, SignalValue b) {
    if (enable == SIG_CONFLICT) return SIG_CONFLICT;
    if (enable == SIG_UNKNOWN) return SIG_UNKNOWN;
    if (enable == SIG_HIGH) return SIG_LOW; /* disabled - forced low, the real chip doesn't tri-state */
    return mux4(c, a, b);
}

static void sn74hc153_eval(SignalValue *v, int pin_count) {
    (void)pin_count; /* always 16 for this IC */
    SignalValue a = v[13], b = v[1];
    SignalValue c1[4] = { v[5], v[4], v[3], v[2] };    /* 1C0, 1C1, 1C2, 1C3 */
    SignalValue c2[4] = { v[9], v[10], v[11], v[12] }; /* 2C0, 2C1, 2C2, 2C3 */
    v[6] = mux_section(v[0], c1, a, b);  /* 1Y */
    v[8] = mux_section(v[14], c2, a, b); /* 2Y */
}

static const IC_Def k_sn74hc153_def = {
    .name = "SN74HC153N",
    .pin_count = 16,
    .pins = k_sn74hc153_pins,
    .eval = sn74hc153_eval,
};

void ic_sn74hc153_register(void) {
    ic_registry_register(&k_sn74hc153_def);
}
