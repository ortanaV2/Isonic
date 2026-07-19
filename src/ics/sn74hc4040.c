#include "sn74hc4040.h"
#include "../ic_registry.h"

/* Real TI SN74HC4040N pinout (12-Bit Asynchronous Binary Counter), matching
   the physical 16-pin DIP: pins 1-8 down the left side, 9-16 back up the
   right (see component_init_ic). TI labels the 12 outputs QA (LSB) through
   QL (MSB) rather than numbering them, to avoid confusing "output 1" with
   "pin 1" - QA is pin 9, QL is pin 1; the physical order is NOT simply
   ascending/descending by significance (a real die-layout quirk, verified
   against the primary TI datasheet SCLS160D and cross-checked independently -
   pins 1-7 go Q12,Q6,Q5,Q7,Q4,Q3,Q2 in that exact non-monotonic order).
   Array order must stay ascending by pin_number: tmp[i] == pin_number i+1. */
static const IC_PinDef k_sn74hc4040_pins[16] = {
    { .pin_number = 1, .name = "Q12", .direction = PIN_OUTPUT },
    { .pin_number = 2, .name = "Q6", .direction = PIN_OUTPUT },
    { .pin_number = 3, .name = "Q5", .direction = PIN_OUTPUT },
    { .pin_number = 4, .name = "Q7", .direction = PIN_OUTPUT },
    { .pin_number = 5, .name = "Q4", .direction = PIN_OUTPUT },
    { .pin_number = 6, .name = "Q3", .direction = PIN_OUTPUT },
    { .pin_number = 7, .name = "Q2", .direction = PIN_OUTPUT },
    { .pin_number = 8, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 9, .name = "Q1", .direction = PIN_OUTPUT },
    { .pin_number = 10, .name = "CLK", .direction = PIN_INPUT },  /* counts on the HIGH-to-LOW (falling) transition */
    { .pin_number = 11, .name = "CLR", .direction = PIN_INPUT },  /* asynchronous clear, active high */
    { .pin_number = 12, .name = "Q9", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "Q8", .direction = PIN_OUTPUT },
    { .pin_number = 14, .name = "Q10", .direction = PIN_OUTPUT },
    { .pin_number = 15, .name = "Q11", .direction = PIN_OUTPUT },
    { .pin_number = 16, .name = "VCC", .direction = PIN_POWER },
};

/* No combinational behavior of its own - eval() is a required, always-called
   hook (see ic_registry.h), but this IC does all its actual work in
   sn74hc4040_clock_edge below, once per real frame rather than once per
   settle iteration. */
static void sn74hc4040_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)v;
    (void)pin_count;
    (void)state;
}

/* Q1 (LSB, bit 0) through Q12 (MSB, bit 11) -> absolute pin index, read off
   the pin table above. */
static const int k_q_idx[12] = { 8, 6, 5, 4, 2, 1, 3, 12, 11, 13, 14, 0 };

/* Runs once per real simulation frame (see tick_clocked_ics in sim.c), using
   this frame's already-settled CLK/CLR levels and this instance's own
   persistent seq_state (component.h) - which is what makes a genuine
   edge-triggered counter possible at all: eval() alone has no way to tell
   "the clock changed since last frame" from "the clock is just sitting
   there", since it re-runs several times within the same frame while
   combinational signals settle.

   CAVEAT: this only samples CLK once per rendered frame (~60 Hz) - a clock
   signal toggling faster than that will have transitions missed/undercounted,
   same limitation any frame-stepped (rather than true event-driven) digital
   sim has. Fine for the speeds a human actually toggles an Input wire at,
   not a substitute for real timing analysis. */
static void sn74hc4040_clock_edge(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count;
    SignalValue clk = v[9];  /* pin 10 */
    SignalValue clr = v[10]; /* pin 11 */
    SignalValue prev_clk = (SignalValue)state[2];

    unsigned int count = state[0] | ((unsigned int)state[1] << 8);

    if (clr == SIG_HIGH) {
        count = 0; /* asynchronous clear wins outright, same as the real chip */
    } else if (prev_clk == SIG_HIGH && clk == SIG_LOW) {
        count = (count + 1) & 0x0FFF; /* 12-bit wrap */
    }
    /* any other CLK transition (including through CONFLICT/UNKNOWN) isn't a
       clean HIGH-then-LOW edge, so it doesn't count - holds instead */

    state[0] = (unsigned char)(count & 0xFF);
    state[1] = (unsigned char)((count >> 8) & 0xFF);
    state[2] = (unsigned char)clk;

    for (int bit = 0; bit < 12; bit++) {
        v[k_q_idx[bit]] = (count & (1u << bit)) ? SIG_HIGH : SIG_LOW;
    }
}

static const IC_Def k_sn74hc4040_def = {
    .name = "SN74HC4040N",
    .pin_count = 16,
    .pins = k_sn74hc4040_pins,
    .eval = sn74hc4040_eval,
    .clock_edge = sn74hc4040_clock_edge,
};

void ic_sn74hc4040_register(void) {
    ic_registry_register(&k_sn74hc4040_def);
}
