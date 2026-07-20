#ifndef ISONIC_SCHEMATIC_IO_H
#define ISONIC_SCHEMATIC_IO_H

#include "circuit.h"

/* Reads/writes the whole Circuit (layers, components incl. programmed
   AT28C64B EEPROM content, wires, vias) as the line-based ".isonic" text
   format - see schematic_io.c for the exact record shapes. Both return 1 on
   success, 0 on failure (bad path, malformed file, unknown top-of-file
   version tag). schematic_load fully resets *circuit first (circuit_init),
   so a failed load may still leave it as a fresh empty circuit rather than
   whatever it held before the call - callers that care should snapshot/
   confirm-discard before calling this, not rely on failure leaving the
   circuit untouched. */
int schematic_save(const Circuit *circuit, const char *path);
int schematic_load(Circuit *circuit, const char *path);

#endif
