#include <string.h>
#include "sim.h"

#define SIM_ITERATIONS 4

/* Sized to TOTAL_POINTS (not just MAX_GLOBAL_PINS) because a union-find root
   can land on either a pin index or a wire-endpoint index - the two ranges
   share this same lookup space (see circuit.h). */
static int driver_present[TOTAL_POINTS];
static int driver_conflict[TOTAL_POINTS];
static SignalValue driver_value[TOTAL_POINTS];

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
            register_driver(circuit_pin_net_root(circuit, ci, pi), p->value);
        }
    }

    for (int wi = 0; wi < circuit->wire_high_water; wi++) {
        Wire *w = &circuit->wires[wi];
        if (!w->in_use || w->kind != WIRE_KIND_INPUT) continue;
        register_driver(circuit_wire_net_root(circuit, wi), w->input_value ? SIG_HIGH : SIG_LOW);
    }
}

static void propagate_to_pins(Circuit *circuit) {
    for (int ci = 0; ci < circuit->component_high_water; ci++) {
        Component *c = &circuit->components[ci];
        if (!c->in_use) continue;
        for (int pi = 0; pi < c->pin_count; pi++) {
            Pin *p = &c->pins[pi];
            int root = circuit_pin_net_root(circuit, ci, pi);
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
        int root = circuit_wire_net_root(circuit, wi);
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
        c->ic_def->eval(tmp, c->pin_count);
        for (int pi = 0; pi < c->pin_count; pi++) {
            if (c->pins[pi].direction == PIN_OUTPUT) {
                c->pins[pi].value = tmp[pi];
            }
        }
    }
}

void sim_step(Circuit *circuit) {
    for (int iter = 0; iter < SIM_ITERATIONS; iter++) {
        gather_drivers(circuit);
        propagate_to_pins(circuit);
        evaluate_ics(circuit);
    }
    /* final propagate so wire/net rendering reflects the last eval pass */
    gather_drivers(circuit);
    propagate_to_pins(circuit);
    propagate_to_wires(circuit);
}
