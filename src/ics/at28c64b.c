#include <string.h>
#include "at28c64b.h"
#include "../ic_registry.h"

/* AT28C64B-15PU pinout (8K x 8 Parallel EEPROM), matching the physical
   28-pin DIP: pins 1-14 down the left side, 15-28 back up the right (see
   component_init_ic). This is the standard JEDEC byte-wide EEPROM pinout
   shared across the whole 2764/2864/28C64/28C256 family (VPP -> NC and
   PGM -> WE# versus the EPROM-only 2764) - reproduced here from that common,
   widely-documented layout rather than a freshly pulled datasheet PDF, so
   double check it against the actual Microchip/Atmel AT28C64B datasheet
   before wiring real hardware to it. Array order must stay ascending by
   pin_number: tmp[i] == pin_number i+1. */
static const IC_PinDef k_at28c64b_pins[28] = {
    { .pin_number = 1, .name = "NC", .direction = PIN_INPUT, .decorative = 1 },  /* not connected on the real die */
    { .pin_number = 2, .name = "A12", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "A7", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "A6", .direction = PIN_INPUT },
    { .pin_number = 5, .name = "A5", .direction = PIN_INPUT },
    { .pin_number = 6, .name = "A4", .direction = PIN_INPUT },
    { .pin_number = 7, .name = "A3", .direction = PIN_INPUT },
    { .pin_number = 8, .name = "A2", .direction = PIN_INPUT },
    { .pin_number = 9, .name = "A1", .direction = PIN_INPUT },
    { .pin_number = 10, .name = "A0", .direction = PIN_INPUT },
    { .pin_number = 11, .name = "IO0", .direction = PIN_OUTPUT },
    { .pin_number = 12, .name = "IO1", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "IO2", .direction = PIN_OUTPUT },
    { .pin_number = 14, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 15, .name = "IO3", .direction = PIN_OUTPUT },
    { .pin_number = 16, .name = "IO4", .direction = PIN_OUTPUT },
    { .pin_number = 17, .name = "IO5", .direction = PIN_OUTPUT },
    { .pin_number = 18, .name = "IO6", .direction = PIN_OUTPUT },
    { .pin_number = 19, .name = "IO7", .direction = PIN_OUTPUT },
    { .pin_number = 20, .name = "CE", .direction = PIN_INPUT },  /* chip enable, active low */
    { .pin_number = 21, .name = "A10", .direction = PIN_INPUT },
    { .pin_number = 22, .name = "OE", .direction = PIN_INPUT },  /* output enable, active low */
    { .pin_number = 23, .name = "A11", .direction = PIN_INPUT },
    { .pin_number = 24, .name = "A9", .direction = PIN_INPUT },
    { .pin_number = 25, .name = "A8", .direction = PIN_INPUT },
    { .pin_number = 26, .name = "NC", .direction = PIN_INPUT, .decorative = 1 },  /* not connected on the real die */
    { .pin_number = 27, .name = "WE", .direction = PIN_INPUT },  /* write enable, active low */
    { .pin_number = 28, .name = "VCC", .direction = PIN_POWER },
};

/* Absolute pin index (0-based, pin_number - 1) for each address/data bit,
   read off the pin table above - kept as named lookups instead of scattering
   raw indices through eval()/clock_edge() below. */
static const int k_addr_idx[13] = { 9, 8, 7, 6, 5, 4, 3, 2, 24, 23, 20, 22, 1 }; /* A0..A12 */
static const int k_io_idx[8] = { 10, 11, 12, 14, 15, 16, 17, 18 };              /* IO0..IO7 */

/* Per-instance memory, kept OUTSIDE Component/Circuit entirely: a real
   8KB-per-chip array embedded in Component would balloon Circuit (256
   component slots) by ~2MB, and App - which holds a Circuit - is a plain
   stack-local in main(), nowhere near that much stack headroom. Instead each
   placed AT28C64B lazily claims one slot of this small fixed pool the first
   time its eval() ever runs, remembering which slot as a single byte in its
   own 4-byte seq_state (see IC_SEQ_STATE_BYTES in component.h) - byte 0 is
   an "already claimed a slot" flag (0 on a freshly placed/zeroed component,
   which component_init_ic's memset already gives us for free), byte 1 is
   the slot index itself, byte 2 is the last-seen WE# level (for edge
   detection in clock_edge, same pattern as the 4040 counter's own prev-CLK
   byte), byte 3 is the last I/O bus value seen while a write was actually in
   progress (see at28c64b_eval's write-in-progress branch) - clock_edge reads
   this instead of the live bus at the WE# edge itself. 16 chips is far more
   than a hobby CPU build would ever place at once; instances beyond that
   share slot 0 rather than reading/writing out of bounds - wrong, but
   harmless to anything else in the circuit. */
#define AT28C64B_MAX_INSTANCES 16
static unsigned char g_at28c64b_mem[AT28C64B_MAX_INSTANCES][AT28C64B_SIZE];
static int g_at28c64b_next_slot = 0;

static int at28c64b_slot_for(unsigned char *state) {
    if (state[0] != 0) return state[1];
    int slot = (g_at28c64b_next_slot < AT28C64B_MAX_INSTANCES) ? g_at28c64b_next_slot : 0;
    if (g_at28c64b_next_slot < AT28C64B_MAX_INSTANCES) g_at28c64b_next_slot++;
    memset(g_at28c64b_mem[slot], 0xFF, AT28C64B_SIZE); /* factory-erased, same as real NOR EEPROM */
    state[0] = 1;
    state[1] = (unsigned char)slot;
    return slot;
}

/* Address bits below SIG_HIGH are all treated as 0 (LOW/UNKNOWN/CONFLICT
   alike) - full per-bit CONFLICT/UNKNOWN propagation across a 13-line
   address bus (as the small gate ICs do for their 1-3 select/input lines)
   would be a lot of bookkeeping for a case that, on a real address bus
   actually being decoded correctly, shouldn't arise - a deliberate
   simplification, not an oversight. */
static int at28c64b_address(const SignalValue *v) {
    int addr = 0;
    for (int i = 0; i < 13; i++) {
        if (v[k_addr_idx[i]] == SIG_HIGH) addr |= (1 << i);
    }
    return addr;
}

/* Combinational read path: CE#/OE#/WE# gate whether the I/O bus is actively
   driven at all, same tri-state convention SN74HC244N's buf_tristate uses
   (CONFLICT propagates, anything not a confirmed read-enable releases the
   bus rather than guessing). WE# must additionally read confirmed HIGH -
   while a write is in progress (WE# LOW), the real chip's own output stage
   is off regardless of OE#, so it doesn't fight whatever external device is
   driving I/O with the byte being written. Shared by eval() and, after a
   commit, clock_edge() too - see at28c64b_clock_edge's comment on why the
   write path needs to re-drive the bus itself instead of waiting for next
   frame's eval() to catch up. */
static void at28c64b_drive_io(SignalValue *v, int slot) {
    SignalValue ce = v[19];
    SignalValue oe = v[21];
    SignalValue we = v[26];

    if (ce == SIG_CONFLICT || oe == SIG_CONFLICT || we == SIG_CONFLICT) {
        for (int i = 0; i < 8; i++) v[k_io_idx[i]] = SIG_CONFLICT;
        return;
    }
    if (ce != SIG_LOW || oe != SIG_LOW || we != SIG_HIGH) {
        for (int i = 0; i < 8; i++) v[k_io_idx[i]] = SIG_HIZ;
        return;
    }

    unsigned char byte = g_at28c64b_mem[slot][at28c64b_address(v)];
    for (int i = 0; i < 8; i++) {
        v[k_io_idx[i]] = (byte & (1 << i)) ? SIG_HIGH : SIG_LOW;
    }
}

static void at28c64b_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count;
    int slot = at28c64b_slot_for(state);

    SignalValue ce = v[19];
    SignalValue we = v[26];

    /* while a write is actually in progress (CE# low, WE# low), keep
       snapshotting the externally-driven I/O bus into state[3] - this is the
       byte clock_edge will commit on the next WE# rising edge. It can't just
       re-read v[] fresh at that edge instead: by the time WE# reads HIGH,
       eval() (this same function, called several times before clock_edge
       runs - see sim.c) has already switched into actively driving the I/O
       pins itself with the OLD stored byte via at28c64b_drive_io below -
       clock_edge would end up capturing the chip's own read-back rather
       than the real write data. Sampling continuously during the write
       phase instead matches how real write timing works anyway (data must
       be stable before WE# rises, not exactly at the instant it does). */
    if (ce == SIG_LOW && we == SIG_LOW) {
        unsigned char byte = 0;
        for (int i = 0; i < 8; i++) {
            if (v[k_io_idx[i]] == SIG_HIGH) byte |= (unsigned char)(1 << i);
        }
        state[3] = byte;
    }

    at28c64b_drive_io(v, slot);
}

/* Write path: real datasheet behavior is "address is latched by whichever of
   CE#/WE# falls last, data by whichever rises first" - simplified here to
   the overwhelmingly common wiring (CE# already low, WE# pulsed) and keyed
   off WE#'s LOW-to-HIGH transition, same edge-detection pattern the 4040
   counter uses for CLK. Address is read fresh from this frame's settled bus
   (nothing else drives it, so it's stable); the data byte instead comes from
   state[3], the last value eval() saw while the write was actually in
   progress - see its comment above for why the live bus at this exact
   instant isn't usable. The real chip's internal write cycle then takes up
   to ~10ms during which it's busy (visible via DATA polling on I/O7) - not
   modeled, same as every other analog/timing detail this simulator skips;
   the write just takes effect on this edge.

   Re-driving the I/O bus below (at28c64b_drive_io) right after committing
   is what makes that instant: sim.c runs eval() several times BEFORE this
   clock_edge ever runs (see sim_step), all with WE# already reading HIGH -
   so for this entire frame, eval() has been actively driving the OLD
   (pre-write) byte the whole time, and nothing re-runs eval() afterward
   (sim_step's own final propagate pass just re-broadcasts whatever value is
   already sitting on this pin). Without this, the old byte would render for
   exactly one frame - both as a visible flash of stale data, and as a
   transient Overwrite/Bus-Conflict diagnostic against anything still wired
   to I/O with a different intended value - before eval() caught up to the
   new byte on the frame after. */
static void at28c64b_clock_edge(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count;
    int slot = at28c64b_slot_for(state);

    SignalValue ce = v[19];
    SignalValue we = v[26];
    SignalValue prev_we = (SignalValue)state[2];

    if (ce == SIG_LOW && prev_we == SIG_LOW && we == SIG_HIGH) {
        g_at28c64b_mem[slot][at28c64b_address(v)] = state[3];
        at28c64b_drive_io(v, slot);
    }

    state[2] = (unsigned char)we;
}

static const IC_Def k_at28c64b_def = {
    .name = "AT28C64B-15PU",
    .pin_count = 28,
    .pins = k_at28c64b_pins,
    .eval = at28c64b_eval,
    .clock_edge = at28c64b_clock_edge,
};

void ic_at28c64b_register(void) {
    ic_registry_register(&k_at28c64b_def);
}

int ic_at28c64b_is(const Component *c) {
    return c->ic_def == &k_at28c64b_def;
}

unsigned char *ic_at28c64b_memory(Component *c) {
    if (!ic_at28c64b_is(c)) return NULL;
    return g_at28c64b_mem[at28c64b_slot_for(c->seq_state)];
}

int ic_at28c64b_current_address(const Component *c) {
    if (!ic_at28c64b_is(c)) return -1;
    int addr = 0;
    for (int i = 0; i < 13; i++) {
        if (c->pins[k_addr_idx[i]].value == SIG_HIGH) addr |= (1 << i);
    }
    return addr;
}
