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

   OE (active low) tri-states all 8 Q outputs regardless of LE, matching the
   real chip: on real silicon OE only gates the output BUFFER stage, never
   the internal latch storage, so cycling OE never disturbs a held value.
   This simulator's eval() is otherwise stateless between sim steps except
   for whatever a component reads back from its own previous output value -
   which is exactly why this can't just reuse that trick the way most
   combinational ICs do (see latch_get/latch_set below): once Q is actually
   driven to SIG_HIZ, gather_drivers (sim.c) stops treating it as a driver,
   and propagate_to_pins then resolves the net to SIG_UNKNOWN if nothing else
   drives it - overwriting the very value eval() would otherwise have read
   back as "memory". So the 8 latched bits are kept in seq_state instead
   (4 bits per channel, 2 channels per byte - same "state survives
   independently of what the pins currently read" approach SN74HC4040N's own
   running count already uses), decoupling "what's stored" from "what Q is
   currently allowed to show". */
static const IC_PinDef k_tc74hc373_pins[20] = {
    { .pin_number = 1, .name = "OE", .direction = PIN_INPUT },  /* output enable, active low - tri-states all 8 Q outputs when deasserted (see note above and buf_tristate-equivalent handling below). Deliberately NOT marked decorative: real hardware genuinely needs it tied to a defined level (usually GND) if you want it permanently enabled - leaving it floating in the schematic is exactly the mistake the floating-pin warning exists to catch. */
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

/* Each of the 8 latched bits gets a 4-bit nibble in seq_state (2 channels
   per byte, 8 channels * 4 bits = 32 bits = all of IC_SEQ_STATE_BYTES) -
   wide enough to hold any SignalValue (0-4) losslessly, not just a plain
   bit, so a channel latched from an indeterminate D (UNKNOWN/CONFLICT)
   stays exactly as indeterminate as it was, the same fidelity the old
   "*q = d" direct assignment had. */
static SignalValue latch_get(const unsigned char *state, int channel) {
    unsigned char byte = state[channel / 2];
    return (SignalValue)((channel % 2 == 0) ? (byte & 0x0F) : (byte >> 4));
}

static void latch_set(unsigned char *state, int channel, SignalValue v) {
    unsigned char *byte = &state[channel / 2];
    if (channel % 2 == 0) *byte = (unsigned char)((*byte & 0xF0) | ((unsigned char)v & 0x0F));
    else *byte = (unsigned char)((*byte & 0x0F) | (((unsigned char)v & 0x0F) << 4));
}

/* Transparent latch with real OE tri-stating: while LE is high, the stored
   bit tracks D; while LE is low (or indeterminate - CONFLICT/UNKNOWN is
   treated as "not confirmed high", the safer assumption), the store holds
   whatever it last tracked. OE then independently gates whether Q actually
   shows that stored bit or goes SIG_HIZ - same active-low convention (and
   same "UNKNOWN treated as deasserted" caution) as SN74HC244N's own
   buf_tristate, so a floating/indeterminate OE never leaves a design
   thinking a bus is safely driven when it might not be. */
static void latch_channel(unsigned char *state, int channel, SignalValue *q, SignalValue d, SignalValue le, SignalValue oe) {
    if (le == SIG_HIGH) latch_set(state, channel, d);

    if (oe == SIG_CONFLICT) *q = SIG_CONFLICT;
    else if (oe == SIG_HIGH || oe == SIG_UNKNOWN) *q = SIG_HIZ;
    else *q = latch_get(state, channel); /* oe == SIG_LOW: enabled */
}

static void tc74hc373_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 20 for this IC */
    SignalValue oe = v[0];
    SignalValue le = v[10];
    latch_channel(state, 0, &v[1], v[2], le, oe);   /* 1Q = 1D */
    latch_channel(state, 1, &v[4], v[3], le, oe);   /* 2Q = 2D */
    latch_channel(state, 2, &v[5], v[6], le, oe);   /* 3Q = 3D */
    latch_channel(state, 3, &v[8], v[7], le, oe);   /* 4Q = 4D */
    latch_channel(state, 4, &v[11], v[12], le, oe); /* 5Q = 5D */
    latch_channel(state, 5, &v[14], v[13], le, oe); /* 6Q = 6D */
    latch_channel(state, 6, &v[15], v[16], le, oe); /* 7Q = 7D */
    latch_channel(state, 7, &v[18], v[17], le, oe); /* 8Q = 8D */
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
