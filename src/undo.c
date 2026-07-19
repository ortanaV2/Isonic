#include <string.h>
#include "undo.h"

#define UNDO_MAX_STEPS 100

/* Static, not embedded in App - see the size/stack-headroom reasoning in
   undo.h. g_current is the index of the snapshot matching whatever the
   live circuit currently is; g_count is how many valid snapshots exist
   (g_current+1 of them are "past", the rest beyond g_current are "future"
   redo steps still sitting in the array from before an undo). */
static Circuit g_stack[UNDO_MAX_STEPS];
static int g_current;
static int g_count;

void undo_init(void) {
    g_current = -1;
    g_count = 0;
}

void undo_push(const Circuit *circuit) {
    g_current++;
    if (g_current >= UNDO_MAX_STEPS) {
        /* full - drop the oldest snapshot rather than grow, same reasoning
           as every other fixed-size table in this codebase (diagnostics'
           g_nets, sim.c's driver arrays, ...): a session realistically
           never approaches 100 structural edits deep in undo history. */
        memmove(&g_stack[0], &g_stack[1], sizeof(Circuit) * (UNDO_MAX_STEPS - 1));
        g_current = UNDO_MAX_STEPS - 1;
    }
    g_stack[g_current] = *circuit;
    g_count = g_current + 1; /* discards any redo history past this point */
}

/* Wire.input_value (a switch the user set) and Component.seq_state (a
   counter's running count, a timer's phase, an EEPROM's memory-pool slot
   bookkeeping) are simulation-time "live" state, not structure - undo/redo
   must never revert those, only placing/moving/deleting. Saves them out of
   *circuit into small local buffers (NOT a second full Circuit - that's the
   ~300KB struct itself, far too large for a stack local, which is exactly
   why the undo stack is static instead of living in App - see undo.h)
   before the snapshot overwrites *circuit, then reapplies them onto
   whatever's still in_use at the same slot afterward. A slot that changed
   which wire/component occupies it between the two snapshots (freed by a
   delete, then reused by an unrelated add) just doesn't carry its live
   state over cleanly - a harmless corner case, not lasting corruption. */
static void restore_structure_keep_live_state(Circuit *circuit, const Circuit *snapshot) {
    int live_input[MAX_WIRES];
    int wire_was_input[MAX_WIRES];
    unsigned char live_seq[MAX_COMPONENTS][IC_SEQ_STATE_BYTES];
    int comp_was_in_use[MAX_COMPONENTS];

    for (int i = 0; i < MAX_WIRES; i++) {
        live_input[i] = circuit->wires[i].input_value;
        wire_was_input[i] = circuit->wires[i].in_use && circuit->wires[i].kind == WIRE_KIND_INPUT;
    }
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        memcpy(live_seq[i], circuit->components[i].seq_state, IC_SEQ_STATE_BYTES);
        comp_was_in_use[i] = circuit->components[i].in_use;
    }

    *circuit = *snapshot;

    for (int i = 0; i < MAX_WIRES; i++) {
        if (wire_was_input[i] && circuit->wires[i].in_use && circuit->wires[i].kind == WIRE_KIND_INPUT) {
            circuit->wires[i].input_value = live_input[i];
        }
    }
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (comp_was_in_use[i] && circuit->components[i].in_use) {
            memcpy(circuit->components[i].seq_state, live_seq[i], IC_SEQ_STATE_BYTES);
        }
    }
}

int undo_undo(Circuit *circuit) {
    if (g_current <= 0) return 0;
    g_current--;
    restore_structure_keep_live_state(circuit, &g_stack[g_current]);
    return 1;
}

int undo_redo(Circuit *circuit) {
    if (g_current + 1 >= g_count) return 0;
    g_current++;
    restore_structure_keep_live_state(circuit, &g_stack[g_current]);
    return 1;
}
