#include "cd4555.h"
#include "../ic_registry.h"

/* Real CD4555BE pinout (Dual 1-of-4 Decoder/Demultiplexer, active-HIGH
   outputs), matching the physical 16-pin DIP: pins 1-8 down the left side,
   9-16 back up the right (see component_init_ic) - also drives eval()
   indexing below (array order must stay ascending by pin_number: tmp[i] ==
   pin_number i+1). "BE" is TI's PDIP-package suffix for this part - picked
   specifically because it's the TI-branded variant actually in stock as a
   through-hole DIP (a Harris-branded CD4555BE also exists but is obsolete). */
static const IC_PinDef k_cd4555_pins[16] = {
    { .pin_number = 1, .name = "1E", .direction = PIN_INPUT },  /* enable, active low, section 1 */
    { .pin_number = 2, .name = "1A", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "1B", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "1Q0", .direction = PIN_OUTPUT },
    { .pin_number = 5, .name = "1Q1", .direction = PIN_OUTPUT },
    { .pin_number = 6, .name = "1Q2", .direction = PIN_OUTPUT },
    { .pin_number = 7, .name = "1Q3", .direction = PIN_OUTPUT },
    { .pin_number = 8, .name = "VSS", .direction = PIN_POWER },
    { .pin_number = 9, .name = "2Q3", .direction = PIN_OUTPUT },
    { .pin_number = 10, .name = "2Q2", .direction = PIN_OUTPUT },
    { .pin_number = 11, .name = "2Q1", .direction = PIN_OUTPUT },
    { .pin_number = 12, .name = "2Q0", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "2B", .direction = PIN_INPUT },
    { .pin_number = 14, .name = "2A", .direction = PIN_INPUT },
    { .pin_number = 15, .name = "2E", .direction = PIN_INPUT },  /* enable, active low, section 2 */
    { .pin_number = 16, .name = "VDD", .direction = PIN_POWER },
};

/* One 1-of-4 section: active-low enable, active-HIGH outputs - the selected
   line goes high, every other line (and everything when disabled) stays
   low. This is the exact output-polarity mirror of SN74HC139N's
   demux4_section: same truth table, SIG_HIGH/SIG_LOW swapped. y_idx lists
   Q0..Q3's absolute indices into v[]. Same CONFLICT/UNKNOWN propagation
   priority used throughout the other gate/mux/demux ICs. */
static void demux4_active_high_section(SignalValue *v, const int y_idx[4], SignalValue enable, SignalValue a, SignalValue b) {
    SignalValue inputs[3] = { enable, a, b };
    int conflict = 0, unknown = 0;
    for (int i = 0; i < 3; i++) {
        if (inputs[i] == SIG_CONFLICT) conflict = 1;
        else if (inputs[i] == SIG_UNKNOWN) unknown = 1;
    }
    if (conflict || unknown) {
        SignalValue indefinite = conflict ? SIG_CONFLICT : SIG_UNKNOWN;
        for (int i = 0; i < 4; i++) v[y_idx[i]] = indefinite;
        return;
    }

    int enabled = (enable == SIG_LOW);
    int sel = (b == SIG_HIGH ? 2 : 0) | (a == SIG_HIGH ? 1 : 0);
    for (int i = 0; i < 4; i++) {
        v[y_idx[i]] = (enabled && i == sel) ? SIG_HIGH : SIG_LOW;
    }
}

static void cd4555_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 16 for this IC */
    (void)state;
    static const int y1[4] = { 3, 4, 5, 6 };   /* 1Q0..1Q3 */
    static const int y2[4] = { 11, 10, 9, 8 }; /* 2Q0..2Q3 */
    demux4_active_high_section(v, y1, v[0], v[1], v[2]);    /* 1E, 1A, 1B */
    demux4_active_high_section(v, y2, v[14], v[13], v[12]); /* 2E, 2A, 2B */
}

static const IC_Def k_cd4555_def = {
    .name = "CD4555BE",
    .pin_count = 16,
    .pins = k_cd4555_pins,
    .eval = cd4555_eval,
};

void ic_cd4555_register(void) {
    ic_registry_register(&k_cd4555_def);
}
