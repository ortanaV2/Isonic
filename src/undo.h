#ifndef ISONIC_UNDO_H
#define ISONIC_UNDO_H

#include "circuit.h"

/* Undo/redo for circuit STRUCTURE only (placing/moving/deleting components
   and wires) - not simulation actions like toggling an Input wire's H/L or
   a sequential IC's running state (a counter's count, a timer's phase, an
   EEPROM's memory-pool slot), and not AT28C64B Manage Data byte edits (that
   memory lives in its own pool outside Circuit entirely, see
   ics/at28c64b.c). Matches how most schematic/circuit tools draw the line
   between "editing" and "using" a circuit: undoing past the point where you
   flipped a switch or watched a counter tick shouldn't un-flip/un-tick it,
   only structural edits are steps in this history. undo_undo/undo_redo
   restore everything else from the snapshot but always keep whatever
   Wire.input_value/Component.seq_state the live circuit currently has for
   anything that still exists afterward - see restore_structure_keep_live_state
   in undo.c.

   Implemented as a plain snapshot stack: Circuit has no pointers to owned
   memory (its one pointer member, Component.ic_def, points at static
   registry data that never moves), so a whole-struct copy is a perfectly
   valid snapshot of everything else. Circuit is well over 1MB now (see
   circuit.h's MAX_COMPONENTS/MAX_WIRES/MAX_VIAS), so a 100-deep stack is a
   few hundred MB - fine as static storage (see g_stack in undo.c), but far
   too large to ever put on a plain stack frame, which is why App itself is
   `static App app` in main.c rather than an ordinary stack local. */

void undo_init(void);

/* Records the given circuit as the new "current" undo step, discarding any
   redo history past it (same as any undo stack - making a new edit after
   undoing invalidates the undone future). Call once per completed
   structural edit (a finished drag that actually moved something, a wire
   just added, a component/wire just deleted, ...) - not continuously while
   a drag is still in progress, or every frame of that drag would become
   its own undo step. */
void undo_push(const Circuit *circuit);

/* Restores the previous snapshot into *circuit. Returns 0 (circuit
   untouched) if there is nothing before the current step. */
int undo_undo(Circuit *circuit);

/* Restores the next (previously undone) snapshot into *circuit. Returns 0
   (circuit untouched) if there is no undone step to redo. */
int undo_redo(Circuit *circuit);

#endif
