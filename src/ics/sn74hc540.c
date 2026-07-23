#include "sn74hc540.h"
#include "../ic_registry.h"

/* Real TI SN74HC540N pinout (Octal Buffer/Line Driver, Inverting, 3-State) -
   verified against TI's SCLS007F datasheet (pin configuration diagram,
   package J/DW/N/NS/PW). Matches the physical 20-pin DIP: pins 1-10 down the
   left side, 11-20 back up the right (see component_init_ic). Unlike
   SN74HC244N's interleaved layout, the '540 uses a "flow-through" pinout -
   all 8 inputs together on one side, all 8 outputs together on the other -
   which the datasheet itself notes exists specifically to simplify PCB
   routing. Array order must stay ascending by pin_number: tmp[i] ==
   pin_number i+1. */
static const IC_PinDef k_sn74hc540_pins[20] = {
    { .pin_number = 1, .name = "OE1", .direction = PIN_INPUT },  /* output enable, active low - see buf_inv_tristate below */
    { .pin_number = 2, .name = "A1", .direction = PIN_INPUT },
    { .pin_number = 3, .name = "A2", .direction = PIN_INPUT },
    { .pin_number = 4, .name = "A3", .direction = PIN_INPUT },
    { .pin_number = 5, .name = "A4", .direction = PIN_INPUT },
    { .pin_number = 6, .name = "A5", .direction = PIN_INPUT },
    { .pin_number = 7, .name = "A6", .direction = PIN_INPUT },
    { .pin_number = 8, .name = "A7", .direction = PIN_INPUT },
    { .pin_number = 9, .name = "A8", .direction = PIN_INPUT },
    { .pin_number = 10, .name = "GND", .direction = PIN_POWER },
    { .pin_number = 11, .name = "Y8", .direction = PIN_OUTPUT },
    { .pin_number = 12, .name = "Y7", .direction = PIN_OUTPUT },
    { .pin_number = 13, .name = "Y6", .direction = PIN_OUTPUT },
    { .pin_number = 14, .name = "Y5", .direction = PIN_OUTPUT },
    { .pin_number = 15, .name = "Y4", .direction = PIN_OUTPUT },
    { .pin_number = 16, .name = "Y3", .direction = PIN_OUTPUT },
    { .pin_number = 17, .name = "Y2", .direction = PIN_OUTPUT },
    { .pin_number = 18, .name = "Y1", .direction = PIN_OUTPUT },
    { .pin_number = 19, .name = "OE2", .direction = PIN_INPUT }, /* output enable, active low - same channel-wide gate as OE1 */
    { .pin_number = 20, .name = "VCC", .direction = PIN_POWER },
};

/* The datasheet's own 3-state control gate is a 2-input NOR across OE1/OE2:
   all 8 outputs go Hi-Z the instant EITHER is high, both must be low to
   enable. A definite HIGH on either pin alone already settles that (a real
   NOR gate's output is determined the moment one input is high, regardless
   of what the other one is doing) - checked first, ahead of CONFLICT, so an
   indeterminate OE1 can't override an OE2 that's unambiguously HIGH. Only
   once neither pin is confirmed HIGH does an actual CONFLICT on either
   propagate; UNKNOWN is then treated as "not confirmed low" (same cautious
   default SN74HC244N's own buf_tristate uses), never as safely enabled. */
static SignalValue buf_inv_tristate(SignalValue oe1, SignalValue oe2, SignalValue a) {
    if (oe1 == SIG_HIGH || oe2 == SIG_HIGH) return SIG_HIZ;
    if (oe1 == SIG_CONFLICT || oe2 == SIG_CONFLICT) return SIG_CONFLICT;
    if (oe1 == SIG_UNKNOWN || oe2 == SIG_UNKNOWN) return SIG_HIZ;
    /* both confirmed low: enabled, inverted passthrough */
    if (a == SIG_CONFLICT) return SIG_CONFLICT;
    if (a == SIG_UNKNOWN) return SIG_UNKNOWN;
    return (a == SIG_HIGH) ? SIG_LOW : SIG_HIGH;
}

static void sn74hc540_eval(SignalValue *v, int pin_count, unsigned char *state) {
    (void)pin_count; /* always 20 for this IC */
    (void)state;
    SignalValue oe1 = v[0], oe2 = v[18];
    v[17] = buf_inv_tristate(oe1, oe2, v[1]);  /* Y1 = NOT A1 */
    v[16] = buf_inv_tristate(oe1, oe2, v[2]);  /* Y2 = NOT A2 */
    v[15] = buf_inv_tristate(oe1, oe2, v[3]);  /* Y3 = NOT A3 */
    v[14] = buf_inv_tristate(oe1, oe2, v[4]);  /* Y4 = NOT A4 */
    v[13] = buf_inv_tristate(oe1, oe2, v[5]);  /* Y5 = NOT A5 */
    v[12] = buf_inv_tristate(oe1, oe2, v[6]);  /* Y6 = NOT A6 */
    v[11] = buf_inv_tristate(oe1, oe2, v[7]);  /* Y7 = NOT A7 */
    v[10] = buf_inv_tristate(oe1, oe2, v[8]);  /* Y8 = NOT A8 */
}

static const IC_Def k_sn74hc540_def = {
    .name = "SN74HC540N",
    .pin_count = 20,
    .pins = k_sn74hc540_pins,
    .eval = sn74hc540_eval,
};

void ic_sn74hc540_register(void) {
    ic_registry_register(&k_sn74hc540_def);
}
