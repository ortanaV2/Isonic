#ifndef ISONIC_IC_AT28C64B_H
#define ISONIC_IC_AT28C64B_H

#include "../component.h"

#define AT28C64B_SIZE 8192 /* 8K x 8 */

/* Registers the AT28C64B-15PU (8K x 8 Parallel EEPROM, 28-pin DIP) with the
   IC registry. */
void ic_at28c64b_register(void);

/* True if c is a placed AT28C64B-15PU instance. */
int ic_at28c64b_is(const Component *c);

/* Pointer to this component's own AT28C64B_SIZE-byte memory (claims a pool
   slot on first use - the same lazy mechanism eval()/clock_edge() already
   use, see at28c64b.c). NULL if c isn't an AT28C64B-15PU. For the
   "Manage Data" editor UI (data_editor.h) to read/write content directly,
   bypassing pin-level read/write. */
unsigned char *ic_at28c64b_memory(Component *c);

/* The address currently sitting on A0..A12 (same decode as the simulation's
   own eval(), see at28c64b_address in at28c64b.c), read from this
   component's cached Pin.value rather than a live SignalValue* bus - purely
   for the "Manage Data" editor to highlight which row is currently
   addressed (data_editor.c), independent of CE#/OE#/WE# actually driving it
   out to the bus. -1 if c isn't an AT28C64B-15PU. */
int ic_at28c64b_current_address(const Component *c);

#endif
