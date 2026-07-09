#ifndef ISONIC_RENDER_H
#define ISONIC_RENDER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "camera.h"

void render_grid(SDL_Renderer *renderer, const Camera *cam, int window_w, int window_h);
/* All component labels (IC pins, Input/Output H/L) use font_large, rendered
   oversized and scaled down - see label_scale in render.c.
   highlight_*_a/b (-1 for none) draw up to two components and two wires in
   SELECTION_COLOR for this frame only, without touching their persistent
   .selected flag - used for the temporary "this is what you'd connect to"
   highlight while dragging out a wire (one slot for the fixed start anchor,
   one for whatever the cursor is currently over). */
void render_circuit(SDL_Renderer *renderer, TTF_Font *font_large, const Circuit *circuit, const Camera *cam,
                     int highlight_component_a, int highlight_wire_a,
                     int highlight_component_b, int highlight_wire_b);

/* Rubber-band line shown while the user drags out a new wire, before it is committed. */
void render_wire_preview(SDL_Renderer *renderer, const Camera *cam, int fx, int fy, int tx, int ty);

/* Ghost footprint shown while a placement tool is active, following the cursor. */
void render_placement_preview(SDL_Renderer *renderer, const Camera *cam, int gx, int gy, int w, int h, int valid);

/* Screen-space bounding box of an INPUT/OUTPUT wire's H/L label, shared by
   rendering and click hit-testing so the clickable area always exactly
   matches what's drawn. Returns 0 (rect left unset) for WIRE_KIND_NORMAL, if
   font_large is NULL, or if zoomed out too far for the label to be drawn. */
int render_wire_terminal_bounds(TTF_Font *font_large, const Camera *cam, const Wire *w, SignalValue value, SDL_Rect *out_rect);

#endif
