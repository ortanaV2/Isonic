#include "sn74hc151.h"
#include "../ic_registry.h"

/* Real TI SN74HC151N pinout (8-Input Multiplexer), matching the physical
   16-pin DIP: pins 1-8 down the left side, 9-16 back up the right (see
   component_init_ic) - also drives eval() indexing below (array order must
   stay ascending by pin_number: tmp[i] == pin_number i+1). */
static const IC_PinDef k_sn74hc151_pins[16] = {
    { .pin_number = 1, .name = "D3", .direction = PIN_INPUT },
    { .pin_number = 2, .name = "D2", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "D1", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "D0", .direction = PIN_INPUT },
    { .pin_number = 5, .name = "Y", .direction = PIN_OUTPUT },
    { .pin_number = 6, .name = "W", .direction = PIN_OUTPUT },
    { .pin_number = 7, .name = "G", .direction = PIN_INPUT },  /* strobe/enable, active low */
    { .pin_number = 8, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 9, .name = "C", .direction = PIN_INPUT },  /* select, MSB */
    { .pin_number = 10, .name = "B", .direction = PIN_INPUT },
    { .pin_number = 11, .name = "A", .direction = PIN_INPUT },  /* select, LSB */
    { .pin_number = 12, .name = "D7", .direction = PIN_INPUT },
    { .pin_number = 13, .name = "D6", .direction = PIN_INPUT },
    { .pin_number = 14, .name = "D5", .direction = PIN_INPUT },
    { .pin_number = 15, .name = "D4", .direction = PIN_INPUT },
    { .pin_number = 16, .name = "VCC", .direction = PIN_POWER },
};

static SignalValue not1(SignalValue a) {
    if (a == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_UNKNOWN) return SIG_UNKNOWN;
    return (a == SIG_HIGH) ? SIG_LOW : SIG_HIGH;
}

/* Selects one of 8 data lines by 3-bit address (c is MSB, a is LSB) - CONFLICT/
   UNKNOWN on any select line propagates to the whole selection (which specific
   data line is even being read is itself indeterminate), same priority order
   used by the simple gate ICs (CONFLICT beats UNKNOWN beats a determinate pick). */
static SignalValue mux8(const SignalValue *d, SignalValue a, SignalValue b, SignalValue c) {
    if (a == SIG_CONFLICT || b == SIG_CONFLICT || c == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_UNKNOWN || b == SIG_UNKNOWN || c == SIG_UNKNOWN) return SIG_UNKNOWN;
    int sel = (c == SIG_HIGH ? 4 : 0) | (b == SIG_HIGH ? 2 : 0) | (a == SIG_HIGH ? 1 : 0);
    return d[sel];
}

static void sn74hc151_eval(SignalValue *v, int pin_count) {
    (void)pin_count; /* always 16 for this IC */
    SignalValue strobe = v[6]; /* G, active low */

    if (strobe == SIG_CONFLICT) {
        v[4] = SIG_CONFLICT;
        v[5] = SIG_CONFLICT;
        return;
    }
    if (strobe == SIG_UNKNOWN) {
        v[4] = SIG_UNKNOWN;
        v[5] = SIG_UNKNOWN;
        return;
    }
    if (strobe == SIG_HIGH) {
        /* disabled - the real chip forces Y low / W high, it doesn't tri-state */
        v[4] = SIG_LOW;
        v[5] = SIG_HIGH;
        return;
    }

    SignalValue d[8] = { v[3], v[2], v[1], v[0], v[14], v[13], v[12], v[11] };
    SignalValue y = mux8(d, v[10], v[9], v[8]); /* A=v[10], B=v[9], C=v[8] */
    v[4] = y;       /* Y */
    v[5] = not1(y); /* W = NOT Y */
}

static const IC_Def k_sn74hc151_def = {
    .name = "SN74HC151N",
    .pin_count = 16,
    .pins = k_sn74hc151_pins,
    .eval = sn74hc151_eval,
};

void ic_sn74hc151_register(void) {
    ic_registry_register(&k_sn74hc151_def);
}
