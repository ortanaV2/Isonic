#include <string.h>
#include "sim.h"

#define SIM_ITERATIONS 4

/* Sized to TOTAL_POINTS (not just MAX_GLOBAL_PINS) because a union-find root
   can land on either a pin index or a wire-endpoint index - the two ranges
   share this same lookup space (see circuit.h). */
static int driver_present[TOTAL_POINTS];
static int driver_conflict[TOTAL_POINTS];
static SignalValue driver_value[TOTAL_POINTS];

/* Net roots are resolved once per sim_step (topology can't change mid-step,
   only circuit_rebuild_nets mutates it) and reused across all 5 gather/
   propagate passes below, instead of re-walking the union-find chain for
   every pin/wire on every pass. */
static int pin_root_cache[MAX_GLOBAL_PINS];
static int wire_root_cache[MAX_WIRES];

static void cache_net_roots(Circuit *circuit) {
    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            pin_root_cache[GLOBAL_PIN_ID(ci, pi)] = circuit_pin_net_root(circuit, ci, pi);
        }
    }
    for (int wi = 0; wi < circuit->wire_high_water; wi++) {
        if (!circuit->wires[wi].in_use) continue;
        wire_root_cache[wi] = circuit_wire_net_root(circuit, wi);
    }
}

static void register_driver(int root, SignalValue val) {
    if (driver_present[root]) {
        if (driver_value[root] != val) driver_conflict[root] = 1;
    } else {
        driver_present[root] = 1;
        driver_value[root] = val;
    }
}

/* True if some via sitting exactly at (x,y) bridges to a GND or +5V-role
   layer, writing the value that plane forces (LOW/HIGH) to *out_val. A via
   only gates whether two wire endpoints AT THE SAME POINT get unioned into
   one net (see circuit_rebuild_nets) - it has no identity of its own in the
   union-find, so it can't be the thing gather_drivers registers a driver
   on. This is what lets a wire on an ordinary routed layer, connected only
   through a via to the plane (no wire of its own ever drawn ON the GND/+5V
   layer at that exact point), still read the forced value - see
   gather_drivers' own wire loop below. If a via somehow bridges both a
   GND-role and a POWER-role layer at once (tying ground to power - already
   nonsensical wiring the sim's own conflict detection would flag elsewhere
   once something drives the opposing level), whichever role is found first
   wins; not worth resolving more carefully. */
static int via_forces_value(const Circuit *circuit, int x, int y, SignalValue *out_val) {
    for (int i = 0; i < circuit->via_high_water; i++) {
        const Via *v = &circuit->vias[i];
        if (!v->in_use || v->x != x || v->y != y) continue;
        LayerRole role_a = circuit->layers[v->layer_slot_a].role;
        LayerRole role_b = circuit->layers[v->layer_slot_b].role;
        if (role_a == LAYER_ROLE_GND || role_b == LAYER_ROLE_GND) {
            *out_val = SIG_LOW;
            return 1;
        }
        if (role_a == LAYER_ROLE_POWER || role_b == LAYER_ROLE_POWER) {
            *out_val = SIG_HIGH;
            return 1;
        }
    }
    return 0;
}

static void gather_drivers(Circuit *circuit) {
    memset(driver_present, 0, sizeof(driver_present));
    memset(driver_conflict, 0, sizeof(driver_conflict));

    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            Pin *p = &c->pins[pi];
            if (p->direction != PIN_OUTPUT) continue;
            if (p->value == SIG_HIZ) continue; /* tri-stated - not driving its net right now */
            register_driver(pin_root_cache[GLOBAL_PIN_ID(ci, pi)], p->value);
        }
    }

    for (int wi = 0; wi < circuit->wire_high_water; wi++) {
        Wire *w = &circuit->wires[wi];
        if (!w->in_use) continue;
        if (w->kind == WIRE_KIND_INPUT) {
            register_driver(wire_root_cache[wi], w->input_value ? SIG_HIGH : SIG_LOW);
        }
        /* GND/+5V layers are a forced plane regardless of WireKind - any
           wire routed there drives LOW/HIGH on its own, no Input wire
           needed (see LAYER_ROLE_GND/POWER's doc comment in circuit.h).
           Every such wire is already unioned into one shared net by
           circuit_rebuild_nets, so registering from each is redundant but
           harmless - same as several agreeing Input wires today. */
        LayerRole role = circuit->layers[w->layer_slot].role;
        if (role == LAYER_ROLE_GND) {
            register_driver(wire_root_cache[wi], SIG_LOW);
        } else if (role == LAYER_ROLE_POWER) {
            register_driver(wire_root_cache[wi], SIG_HIGH);
        }

        /* a via at EITHER end bridging this wire's (ordinary-layer) endpoint
           to the GND/+5V plane forces it too, even though this wire's own
           layer isn't the plane layer itself - see via_forces_value's own
           comment. Checked regardless of the role check just above (a wire
           already on the plane just gets the same value registered twice,
           harmless - same "redundant but harmless" precedent several
           agreeing drivers already get elsewhere in this function). */
        SignalValue via_val;
        if (via_forces_value(circuit, w->from_x, w->from_y, &via_val)) register_driver(wire_root_cache[wi], via_val);
        if (via_forces_value(circuit, w->to_x, w->to_y, &via_val)) register_driver(wire_root_cache[wi], via_val);
    }
}

static void propagate_to_pins(Circuit *circuit) {
    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            Pin *p = &c->pins[pi];
            int root = pin_root_cache[GLOBAL_PIN_ID(ci, pi)];
            if (driver_conflict[root]) {
                p->value = SIG_CONFLICT;
            } else if (driver_present[root]) {
                p->value = driver_value[root];
            } else {
                p->value = SIG_UNKNOWN;
            }
        }
    }
}

static void propagate_to_wires(Circuit *circuit) {
    for (int wi = 0; wi < circuit->wire_high_water; wi++) {
        if (!circuit->wires[wi].in_use) continue;
        int root = wire_root_cache[wi];
        if (driver_conflict[root]) {
            circuit->wire_values[wi] = SIG_CONFLICT;
        } else if (driver_present[root]) {
            circuit->wire_values[wi] = driver_value[root];
        } else {
            circuit->wire_values[wi] = SIG_UNKNOWN;
        }
    }
}

static void evaluate_ics(Circuit *circuit) {
    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use || c->type != COMP_IC || c->ic_def == NULL) continue;

        SignalValue tmp[MAX_PINS_PER_COMPONENT];
        for (int pi = 0; pi < c->pin_count; pi++) {
            tmp[pi] = c->pins[pi].value;
        }
        c->ic_def->eval(tmp, c->pin_count, c->seq_state);
        for (int pi = 0; pi < c->pin_count; pi++) {
            if (c->pins[pi].direction == PIN_OUTPUT) {
                c->pins[pi].value = tmp[pi];
            }
        }
    }
}

/* Ticks every IC with a clock_edge callback (see ic_registry.h) exactly once
   per real simulation frame, using this frame's settled pin values - unlike
   evaluate_ics above, this must NOT run inside the SIM_ITERATIONS loop, or a
   single real clock transition would get counted up to SIM_ITERATIONS times. */
static void tick_clocked_ics(Circuit *circuit) {
    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use || c->type != COMP_IC || c->ic_def == NULL || c->ic_def->clock_edge == NULL) continue;

        SignalValue tmp[MAX_PINS_PER_COMPONENT];
        for (int pi = 0; pi < c->pin_count; pi++) {
            tmp[pi] = c->pins[pi].value;
        }
        c->ic_def->clock_edge(tmp, c->pin_count, c->seq_state);
        for (int pi = 0; pi < c->pin_count; pi++) {
            if (c->pins[pi].direction == PIN_OUTPUT) {
                c->pins[pi].value = tmp[pi];
            }
        }
    }
}

void sim_step(Circuit *circuit) {
    cache_net_roots(circuit);
    for (int iter = 0; iter < SIM_ITERATIONS; iter++) {
        gather_drivers(circuit);
        propagate_to_pins(circuit);
        evaluate_ics(circuit);
    }
    /* clock-edge-triggered ICs (e.g. a counter) tick here, once, using this
       frame's now-settled CLK/CLR levels - see tick_clocked_ics above */
    tick_clocked_ics(circuit);
    /* final propagate so wire/net rendering (and next frame's first
       combinational pass) reflects the latest state, including whatever
       tick_clocked_ics just changed */
    gather_drivers(circuit);
    propagate_to_pins(circuit);
    propagate_to_wires(circuit);
}
