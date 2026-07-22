#ifndef ISONIC_DIAGNOSTICS_H
#define ISONIC_DIAGNOSTICS_H

#include "circuit.h"

typedef enum { DIAG_WARNING, DIAG_ERROR } DiagSeverity;
typedef enum { DIAG_BUS_CONFLICT, DIAG_OVERWRITE, DIAG_FLOATING_PIN, DIAG_FAN_OUT } DiagCategory;

/* DIAG_MAX_WIRES/DIAG_MAX_PINS match diagnostics.c's own NET_MAX_WIRES/
   NET_MAX_LOADS (the largest of NET_MAX_DRIVERS/NET_MAX_LOADS a diagnostic
   ever copies pins from) - both were bumped together with circuit.h's
   MAX_COMPONENTS/MAX_WIRES scale-up (were 64/64/64), since a net's wire/pin
   membership list is exactly what a Diagnostic here re-copies for
   highlighting (see diag_add_wires/diag_add_pin) - a smaller cap here would
   just re-truncate what collect_nets already gathered in full. */
#define DIAG_MAX 256
#define DIAG_MAX_WIRES 256
#define DIAG_MAX_PINS 1024

typedef struct {
    DiagSeverity severity;
    DiagCategory category;
    char summary[32]; /* short label shown directly on the popup chip, e.g. "Bus Conflict" */
    char detail[256]; /* longer description shown on hover, over both the chip and any canvas target below */

    /* what to highlight on the canvas, and what a hover over the canvas
       should match back to this diagnostic - see diagnostic_has_wire/pin */
    int wire_ids[DIAG_MAX_WIRES];
    int wire_count;
    struct { int component_id, pin_index; } pins[DIAG_MAX_PINS];
    int pin_count;
} Diagnostic;

typedef struct {
    Diagnostic items[DIAG_MAX];
    int count;
} DiagnosticSet;

/* Recomputes every diagnostic from the circuit's current state - call once
   per frame, after sim_step, so PIN_OUTPUT values (used to tell a real
   driver from a tri-stated SIG_HIZ one) are settled. Purely a read-only
   overlay: never changes circuit/wire/pin values, net resolution stays
   exactly what sim.c already computed - a Bus-Conflict on a net that
   happens to agree on HIGH still resolves to HIGH, this just also flags it. */
void diagnostics_compute(Circuit *circuit, DiagnosticSet *out);

int diagnostic_has_wire(const Diagnostic *diag, int wire_id);
int diagnostic_has_pin(const Diagnostic *diag, int component_id, int pin_index);

#endif
