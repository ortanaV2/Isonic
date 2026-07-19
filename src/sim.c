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
        if (!w->in_use || w->kind != WIRE_KIND_INPUT) continue;
        register_driver(wire_root_cache[wi], w->input_value ? SIG_HIGH : SIG_LOW);
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
