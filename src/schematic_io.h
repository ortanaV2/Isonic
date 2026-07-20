#ifndef ISONIC_SCHEMATIC_IO_H
#define ISONIC_SCHEMATIC_IO_H

#include "circuit.h"
#include "camera.h"

/* Reads/writes the whole Circuit (layers, components incl. programmed
   AT28C64B EEPROM content, wires, vias) plus the camera's position/zoom, as
   the line-based ".isonic" text format - see schematic_io.c for the exact
   record shapes. Both return 1 on success, 0 on failure (bad path,
   malformed file, unknown top-of-file version tag). schematic_load fully
   resets *circuit first (circuit_init), so a failed load may still leave it
   as a fresh empty circuit rather than whatever it held before the call -
   callers that care should snapshot/confirm-discard before calling this,
   not rely on failure leaving the circuit untouched.

   Saving the camera alongside the circuit means opening a schematic frames
   it exactly however the author last left it (effectively a free
   "thumbnail" view) instead of always resetting to the same default origin
   - see app_load_from_file. *camera is left untouched by schematic_load if
   the file predates this (no camera record at all) - the caller is
   expected to have already set it to whatever it wants as that fallback
   (app_load_from_file uses camera_init) before calling this. */
int schematic_save(const Circuit *circuit, const Camera *camera, const char *path);
int schematic_load(Circuit *circuit, Camera *camera, const char *path);

#endif
