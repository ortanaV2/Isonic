#include "cd74hct238.h"
#include "../ic_registry.h"

/* Real CD74HCT238E pinout (3-to-8 Decoder/Demultiplexer, active-HIGH
   outputs) - pin-for-pin identical to its SN74HC138N sibling (same select
   lines, same three enables at the same positions, same physical 16-pin
   DIP layout: pins 1-8 down the left side, 9-16 back up the right, see
   component_init_ic). The 138/238 pair is a well-known drop-in-compatible
   set sold specifically so a board can swap output polarity without any
   layout change - only the internal output stage differs. "HCT" (vs. "HC")
   just means TTL-compatible input thresholds instead of CMOS ones - same
   pinout and truth table either way; this specific part/package was picked
   because it's the one actually stocked in a through-hole DIP right now
   (unlike the CD74HC238N line, which is SOIC-only at the usual
   distributors). Array order must stay ascending by pin_number: tmp[i] ==
   pin_number i+1. */
static const IC_PinDef k_cd74hct238_pins[16] = {
    { .pin_number = 1, .name = "A", .direction = PIN_INPUT },  /* select, LSB */
    { .pin_number = 2, .name = "B", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "C", .direction = PIN_INPUT },  /* select, MSB */
    { .pin_number = 4, .name = "G2A", .direction = PIN_INPUT },  /* enable, active low */
    { .pin_number = 5, .name = "G2B", .direction = PIN_INPUT },  /* enable, active low */
    { .pin_number = 6, .name = "G1", .direction = PIN_INPUT },  /* enable, active high */
    { .pin_number = 7, .name = "Y7", .direction = PIN_OUTPUT },
    { .pin_number = 8, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 9, .name = "Y6", .direction = PIN_OUTPUT },
    { .pin_number = 10, .name = "Y5", .direction = PIN_OUTPUT },
    { .pin_number = 11, .name = "Y4", .direction = PIN_OUTPUT },
    { .pin_number = 12, .name = "Y3", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "Y2", .direction = PIN_OUTPUT },
    { .pin_number = 14, .name = "Y1", .direction = PIN_OUTPUT },
    { .pin_number = 15, .name = "Y0", .direction = PIN_OUTPUT },
    { .pin_number = 16, .name = "VCC", .direction = PIN_POWER },
};

static void cd74hct238_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 16 for this IC */
    (void)state;
    SignalValue a = v[0], b = v[1], c = v[2];
    SignalValue g2a = v[3], g2b = v[4], g1 = v[5];
    const int y_idx[8] = { 14, 13, 12, 11, 10, 9, 8, 6 }; /* Y0..Y7 */

    /* same CONFLICT/UNKNOWN propagation priority used throughout the other
       gate/mux/demux ICs - see SN74HC138N's eval for the identical pattern */
    SignalValue inputs[6] = { a, b, c, g2a, g2b, g1 };
    int conflict = 0, unknown = 0;
    for (int i = 0; i < 6; i++) {
        if (inputs[i] == SIG_CONFLICT) conflict = 1;
        else if (inputs[i] == SIG_UNKNOWN) unknown = 1;
    }
    if (conflict || unknown) {
        SignalValue indefinite = conflict ? SIG_CONFLICT : SIG_UNKNOWN;
        for (int i = 0; i < 8; i++) v[y_idx[i]] = indefinite;
        return;
    }

    /* active-HIGH outputs: the selected line goes high, every other line
       (and everything when disabled) stays low - the exact polarity mirror
       of SN74HC138N's truth table. */
    int enabled = (g1 == SIG_HIGH && g2a == SIG_LOW && g2b == SIG_LOW);
    int sel = (c == SIG_HIGH ? 4 : 0) | (b == SIG_HIGH ? 2 : 0) | (a == SIG_HIGH ? 1 : 0);
    for (int i = 0; i < 8; i++) {
        v[y_idx[i]] = (enabled && i == sel) ? SIG_HIGH : SIG_LOW;
    }
}

static const IC_Def k_cd74hct238_def = {
    .name = "CD74HCT238E",
    .pin_count = 16,
    .pins = k_cd74hct238_pins,
    .eval = cd74hct238_eval,
};

void ic_cd74hct238_register(void) {
    ic_registry_register(&k_cd74hct238_def);
}
