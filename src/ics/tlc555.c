#include "tlc555.h"
#include "../ic_registry.h"

/* Real TI TLC555 pinout (CMOS Timer), matching the physical 8-pin DIP: pins
   1-4 down the left side, 5-8 back up the right (see component_init_ic) -
   pin-compatible with the classic bipolar NE555. Array order must stay
   ascending by pin_number: tmp[i] == pin_number i+1.

   The real 555 is fundamentally an ANALOG part: TRIG/THRES are compared
   against 1/3 and 2/3 of VCC by two internal comparators driving an SR
   latch, and in the classic astable/monostable circuits those two pins are
   tied to an external RC network whose charge/discharge time sets the
   period - Isonic has no resistor/capacitor components and no continuous-
   voltage concept on a wire, so that timing can't be computed from real
   component values here.

   What IS modeled faithfully is the chip's internal architecture: a set/
   reset latch driven by TRIG (set) and THRES (reset), an active-low RESET
   pin that overrides both, and an open-collector DISCH output that's off
   (SIG_HIZ) while the latch is set and pulled low while it's reset - exactly
   the real comparator/latch/discharge-transistor topology. TRIG and THRES
   are ordinary digital inputs here: driving TRIG low (a real external
   trigger) starts a period; driving THRES high (a real external threshold
   trip) ends one early; RESET low aborts unconditionally - all real,
   functional behavior. The one simplification: if NEITHER pin is being
   externally driven (SIG_UNKNOWN - the normal astable wiring, where they'd
   really be tied to the RC node this simulator can't represent), the chip
   free-runs its OWN internal timer using a fixed frame count per phase
   instead of a real RC-derived one, standing in for that missing network -
   see TLC555_HIGH_PHASE_FRAMES/LOW_PHASE_FRAMES below. CONT (the analog
   control-voltage pin) is modeled only for pinout fidelity, like OE on
   TC74HC373APF - it has no digital equivalent to wire up. */
static const IC_PinDef k_tlc555_pins[8] = {
    { 1, "GND",   PIN_POWER },
    { 2, "TRIG",  PIN_INPUT },  /* driving low sets the latch (starts a period) */
    { 3, "OUT",   PIN_OUTPUT },
    { 4, "RESET", PIN_INPUT },  /* active low, overrides everything */
    { 5, "CONT",  PIN_INPUT },  /* analog control voltage - not wired, see note above */
    { 6, "THRES", PIN_INPUT },  /* driving high resets the latch (ends a period) */
    { 7, "DISCH", PIN_OUTPUT }, /* open-collector: SIG_HIZ while set, LOW while reset */
    { 8, "VCC",   PIN_POWER },
};

/* No combinational behavior of its own - eval() is a required, always-called
   hook (see ic_registry.h), but all the real work happens once per real
   frame in tlc555_clock_edge below (the free-running internal timer needs
   the same "once per frame, not once per settle iteration" treatment a
   clocked counter does - see sim.c's tick_clocked_ics). */
static void tlc555_eval(SignalValue *v, int pin_count) {
    (void)v;
    (void)pin_count;
}

/* Frames per half-cycle of the internal free-running timer (60 FPS target -
   see main.c's TARGET_FRAME_MS - so 30 frames/phase is a ~1 Hz square wave
   when nothing is wired to TRIG/THRES). Arbitrary but slow enough to
   actually watch the output blink; there's no real R/C to derive this from. */
#define TLC555_HIGH_PHASE_FRAMES 30
#define TLC555_LOW_PHASE_FRAMES  30

static void tlc555_clock_edge(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count;
    SignalValue trig = v[1];  /* pin 2 */
    SignalValue reset = v[3]; /* pin 4 */
    SignalValue thres = v[5]; /* pin 6 */

    int q = state[0];
    unsigned int phase = state[1] | ((unsigned int)state[2] << 8);

    if (reset == SIG_LOW) {
        q = 0;
        phase = 0;
    } else if (trig == SIG_LOW) {
        q = 1; /* external trigger - (re)start the timing period */
        phase = 0;
    } else if (thres == SIG_HIGH) {
        q = 0; /* external threshold trip - end the period early */
        phase = 0;
    } else {
        /* neither pin is being driven from outside - free-run the internal
           timer as a stand-in for the RC network this circuit would
           normally have (see file comment above) */
        phase++;
        if (q && phase >= TLC555_HIGH_PHASE_FRAMES) {
            q = 0;
            phase = 0;
        } else if (!q && phase >= TLC555_LOW_PHASE_FRAMES) {
            q = 1;
            phase = 0;
        }
    }

    state[0] = (unsigned char)q;
    state[1] = (unsigned char)(phase & 0xFF);
    state[2] = (unsigned char)((phase >> 8) & 0xFF);

    v[2] = q ? SIG_HIGH : SIG_LOW; /* OUT */
    v[6] = q ? SIG_HIZ : SIG_LOW;  /* DISCH */
}

static const IC_Def k_tlc555_def = {
    .name = "TLC555",
    .pin_count = 8,
    .pins = k_tlc555_pins,
    .eval = tlc555_eval,
    .clock_edge = tlc555_clock_edge,
};

void ic_tlc555_register(void) {
    ic_registry_register(&k_tlc555_def);
}
