#include "tc74hc373.h"
#include "../ic_registry.h"

/* Real TC74HC373APF pinout (Toshiba Octal Transparent D-Type Latch) - the
   74HC373 function is standardized across manufacturers (TI's SN74HC373N/
   CD74HC373E use the identical pinout), so Toshiba's second-sourced part is
   pin-for-pin the same; picked because it's the one actually in stock as a
   through-hole DIP right now (the TI-branded 373 variants are all
   backordered at the usual distributors). Matches the physical 20-pin DIP:
   pins 1-10 down the left side, 11-20 back up the right (see
   component_init_ic). D/Q pins interleave across the package rather than
   running in tidy blocks - a real datasheet quirk, not a mistake. Array
   order must stay ascending by pin_number: tmp[i] == pin_number i+1.

   NOTE: the real chip's OE pin (active low) additionally tri-states all 8 Q
   outputs regardless of LE. That's deliberately NOT implemented here: this
   simulator's eval() is stateless between sim steps except for what a
   component chooses to read back from its own previous output value (see
   the "hold" comment on latch() below) - and once an output pin is
   overwritten with SIG_HIZ, that very trick loses the latched value there is
   nothing else to remember it by. OE is still modeled as a real input pin
   for correct pinout/labeling, it's simply always treated as if permanently
   asserted (which matches how it's tied in the overwhelming majority of
   schematics anyway). */
static const IC_PinDef k_tc74hc373_pins[20] = {
    { .pin_number = 1, .name = "OE", .direction = PIN_INPUT },  /* output enable, active low - see note above, not wired to tri-state. Deliberately NOT marked decorative: the simulator ignores it either way, but real hardware genuinely needs it tied to a defined level (usually GND) - leaving it floating in the schematic is exactly the mistake the floating-pin warning exists to catch. */
    { .pin_number = 2, .name = "1Q", .direction = PIN_OUTPUT },
    { .pin_number = 3, .name = "1D", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "2D", .direction = PIN_INPUT },
    { .pin_number = 5, .name = "2Q", .direction = PIN_OUTPUT },
    { .pin_number = 6, .name = "3Q", .direction = PIN_OUTPUT },
    { .pin_number = 7, .name = "3D", .direction = PIN_INPUT },
    { .pin_number = 8, .name = "4D", .direction = PIN_INPUT },
    { .pin_number = 9, .name = "4Q", .direction = PIN_OUTPUT },
    { .pin_number = 10, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 11, .name = "LE", .direction = PIN_INPUT },  /* latch enable, active high, shared by all 8 */
    { .pin_number = 12, .name = "5Q", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "5D", .direction = PIN_INPUT },
    { .pin_number = 14, .name = "6D", .direction = PIN_INPUT },
    { .pin_number = 15, .name = "6Q", .direction = PIN_OUTPUT },
    { .pin_number = 16, .name = "7Q", .direction = PIN_OUTPUT },
    { .pin_number = 17, .name = "7D", .direction = PIN_INPUT },
    { .pin_number = 18, .name = "8D", .direction = PIN_INPUT },
    { .pin_number = 19, .name = "8Q", .direction = PIN_OUTPUT },
    { .pin_number = 20, .name = "VCC", .direction = PIN_POWER },
};

/* Transparent latch: while LE is high, Q follows D; while LE is low (or
   indeterminate - CONFLICT/UNKNOWN is treated as "not confirmed high", the
   safer assumption), Q holds its last value. "Holds" needs no explicit code:
   evaluate_ics (sim.c) seeds v[] from each pin's current value before
   calling eval(), so *q already IS the held value on entry - not writing it
   is the hold. */
static void latch(SignalValue *q, SignalValue d, SignalValue le) {
    if (le == SIG_HIGH) *q = d;
}

static void tc74hc373_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 20 for this IC */
    (void)state;
    SignalValue le = v[10];
    latch(&v[1], v[2], le);   /* 1Q = 1D */
    latch(&v[4], v[3], le);   /* 2Q = 2D */
    latch(&v[5], v[6], le);   /* 3Q = 3D */
    latch(&v[8], v[7], le);   /* 4Q = 4D */
    latch(&v[11], v[12], le); /* 5Q = 5D */
    latch(&v[14], v[13], le); /* 6Q = 6D */
    latch(&v[15], v[16], le); /* 7Q = 7D */
    latch(&v[18], v[17], le); /* 8Q = 8D */
}

static const IC_Def k_tc74hc373_def = {
    .name = "TC74HC373APF",
    .pin_count = 20,
    .pins = k_tc74hc373_pins,
    .eval = tc74hc373_eval,
};

void ic_tc74hc373_register(void) {
    ic_registry_register(&k_tc74hc373_def);
}
