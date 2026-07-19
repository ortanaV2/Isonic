#include "sn74hc00.h"
#include "../ic_registry.h"

/* Real TI SN74HC00N pinout (Quad 2-Input NAND Gate) - same physical 14-pin
   DIP layout as the SN74HC08N (AND) family: pins 1-7 down the left side,
   8-14 back up the right (see component_init_ic) - also drives eval()
   indexing below (array order must stay ascending by pin_number: tmp[i] ==
   pin_number i+1). */
static const IC_PinDef k_sn74hc00_pins[14] = {
    { .pin_number = 1, .name = "1A", .direction = PIN_INPUT },
    { .pin_number = 2, .name = "1B", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "1Y", .direction = PIN_OUTPUT },
    { .pin_number = 4, .name = "2A", .direction = PIN_INPUT },
    { .pin_number = 5, .name = "2B", .direction = PIN_INPUT },
    { .pin_number = 6, .name = "2Y", .direction = PIN_OUTPUT },
    { .pin_number = 7, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 8, .name = "3Y", .direction = PIN_OUTPUT },
    { .pin_number = 9, .name = "3A", .direction = PIN_INPUT },
    { .pin_number = 10, .name = "3B", .direction = PIN_INPUT },
    { .pin_number = 11, .name = "4Y", .direction = PIN_OUTPUT },
    { .pin_number = 12, .name = "4A", .direction = PIN_INPUT },
    { .pin_number = 13, .name = "4B", .direction = PIN_INPUT },
    { .pin_number = 14, .name = "VCC", .direction = PIN_POWER },
};

static SignalValue nand2(SignalValue a, SignalValue b) {
    if (a == SIG_CONFLICT || b == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_LOW || b == SIG_LOW) return SIG_HIGH;
    if (a == SIG_UNKNOWN || b == SIG_UNKNOWN) return SIG_UNKNOWN;
    return SIG_LOW;
}

static void sn74hc00_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 14 for this IC */
    (void)state;
    v[2]  = nand2(v[0], v[1]);   /* 1Y = NOT(1A & 1B)  */
    v[5]  = nand2(v[3], v[4]);   /* 2Y = NOT(2A & 2B)  */
    v[7]  = nand2(v[8], v[9]);   /* 3Y = NOT(3A & 3B)  */
    v[10] = nand2(v[11], v[12]); /* 4Y = NOT(4A & 4B)  */
}

static const IC_Def k_sn74hc00_def = {
    .name = "SN74HC00N",
    .pin_count = 14,
    .pins = k_sn74hc00_pins,
    .eval = sn74hc00_eval,
};

void ic_sn74hc00_register(void) {
    ic_registry_register(&k_sn74hc00_def);
}
