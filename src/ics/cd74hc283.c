#include "cd74hc283.h"
#include "../ic_registry.h"

/* Real CD74HC283E pinout (4-Bit Binary Full Adder with Fast Carry, 16-pin
   DIP) - verified against Texas Instruments' CD74HC283 datasheet, which is
   pin-for-pin and function-table-identical to the older DM74LS283 (TI
   explicitly sells the HC283 as a drop-in replacement for it). Pin names
   here use "S1".."S4" for the Sigma (Σ) sum outputs the datasheet itself
   uses that Greek letter for - same ASCII-safe substitution this project's
   font rendering already relies on elsewhere, nothing about the pinout or
   behavior changes. Array order must stay ascending by pin_number: tmp[i]
   == pin_number i+1.

   Internally the real chip computes this via full carry-look-ahead logic
   across all four bits rather than a simple ripple carry (that's the whole
   point of the "Fast Carry" name) - but that's an internal timing/gate-
   count optimization with no externally observable effect on any input/
   output combination, so eval() below just computes the same 5-bit result
   (C4:S4:S3:S2:S1 = A + B + C0) directly; it matches the datasheet's own
   Function Table exactly, row for row. */
static const IC_PinDef k_cd74hc283_pins[16] = {
    { .pin_number = 1, .name = "S2", .direction = PIN_OUTPUT },
    { .pin_number = 2, .name = "B2", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "A2", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "S1", .direction = PIN_OUTPUT },
    { .pin_number = 5, .name = "A1", .direction = PIN_INPUT },
    { .pin_number = 6, .name = "B1", .direction = PIN_INPUT },
    { .pin_number = 7, .name = "C0", .direction = PIN_INPUT },   /* carry in */
    { .pin_number = 8, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 9, .name = "C4", .direction = PIN_OUTPUT },  /* carry out */
    { .pin_number = 10, .name = "S4", .direction = PIN_OUTPUT },
    { .pin_number = 11, .name = "B4", .direction = PIN_INPUT },
    { .pin_number = 12, .name = "A4", .direction = PIN_INPUT },
    { .pin_number = 13, .name = "S3", .direction = PIN_OUTPUT },
    { .pin_number = 14, .name = "A3", .direction = PIN_INPUT },
    { .pin_number = 15, .name = "B3", .direction = PIN_INPUT },
    { .pin_number = 16, .name = "VCC", .direction = PIN_POWER },
};

static void cd74hc283_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 16 for this IC */
    (void)state;

    SignalValue a1 = v[4], b1 = v[5], a2 = v[2], b2 = v[1];
    SignalValue a3 = v[13], b3 = v[14], a4 = v[11], b4 = v[10];
    SignalValue c0 = v[6];

    /* same CONFLICT/UNKNOWN propagation priority used throughout the other
       combinational ICs - see CD74HCT238E's eval for the identical pattern */
    SignalValue inputs[9] = { a1, b1, a2, b2, a3, b3, a4, b4, c0 };
    int conflict = 0, unknown = 0;
    for (int i = 0; i < 9; i++) {
        if (inputs[i] == SIG_CONFLICT) conflict = 1;
        else if (inputs[i] == SIG_UNKNOWN) unknown = 1;
    }
    if (conflict || unknown) {
        SignalValue indefinite = conflict ? SIG_CONFLICT : SIG_UNKNOWN;
        v[0] = v[3] = v[8] = v[9] = v[12] = indefinite; /* S2,S1,C4,S4,S3 */
        return;
    }

    int a = (a4 == SIG_HIGH ? 8 : 0) | (a3 == SIG_HIGH ? 4 : 0) |
            (a2 == SIG_HIGH ? 2 : 0) | (a1 == SIG_HIGH ? 1 : 0);
    int b = (b4 == SIG_HIGH ? 8 : 0) | (b3 == SIG_HIGH ? 4 : 0) |
            (b2 == SIG_HIGH ? 2 : 0) | (b1 == SIG_HIGH ? 1 : 0);
    int sum = a + b + (c0 == SIG_HIGH ? 1 : 0); /* 0..31, bit 4 is the carry out */

    v[3] = (sum & 1) ? SIG_HIGH : SIG_LOW;   /* S1 */
    v[0] = (sum & 2) ? SIG_HIGH : SIG_LOW;   /* S2 */
    v[12] = (sum & 4) ? SIG_HIGH : SIG_LOW;  /* S3 */
    v[9] = (sum & 8) ? SIG_HIGH : SIG_LOW;   /* S4 */
    v[8] = (sum & 16) ? SIG_HIGH : SIG_LOW;  /* C4 */
}

static const IC_Def k_cd74hc283_def = {
    .name = "CD74HC283E",
    .pin_count = 16,
    .pins = k_cd74hc283_pins,
    .eval = cd74hc283_eval,
};

void ic_cd74hc283_register(void) {
    ic_registry_register(&k_cd74hc283_def);
}
