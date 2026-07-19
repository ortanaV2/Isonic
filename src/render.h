#ifndef ISONIC_RENDER_H
#define ISONIC_RENDER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "camera.h"
#include "diagnostics.h"

void render_grid(SDL_Renderer *renderer, const Camera *cam, int window_w, int window_h);
/* All component labels (IC pins, Input/Output H/L) use font_large, rendered
   oversized and scaled down - see label_scale in render.c.
   highlight_*_a/b (-1 for none) draw up to two components and two wires in
   SELECTION_COLOR for this frame only, without touching their persistent
   .selected flag - used both for the temporary "this is what you'd connect
   to" highlight while dragging out a wire (one slot for the fixed start
   anchor, one for whatever the cursor is currently over), and for the global
   mouse-hover highlight active in any tool (slot "a" only, see app.c). A
   wire that is selected or highlighted also gets its own two endpoints marked.
   diagnostics (may be NULL) colors any other wire red/yellow per the worst
   diagnostic that references it - selection/hover still wins over that.
   layer_preview, when true, colors any remaining wire by its own layer's
   color instead of the plain gray/green signal color (see App's
   shift_held/layer_preview_locked in app.h) - vias are always drawn
   regardless of layer_preview. */
void render_circuit(SDL_Renderer *renderer, TTF_Font *font_large, const Circuit *circuit, const Camera *cam,
                     const DiagnosticSet *diagnostics, int layer_preview,
                     int highlight_component_a, int highlight_wire_a,
                     int highlight_component_b, int highlight_wire_b);

/* Rubber-band line shown while the user drags out a new wire, before it is committed. */
void render_wire_preview(SDL_Renderer *renderer, const Camera *cam, int fx, int fy, int tx, int ty);

/* Ghost footprint shown while a placement tool is active, following the cursor. */
void render_placement_preview(SDL_Renderer *renderer, const Camera *cam, int gx, int gy, int w, int h, int valid);

/* Ghost preview shown while TOOL_VIA is active and the cursor is snapped to
   an existing wire node (see circuit_find_wire_node_near) - same role as
   render_placement_preview above, but for a single point instead of a
   footprint. valid is false when the node's own layer already matches the
   active layer (placing a via there would be a no-op, colored like
   render_placement_preview's own invalid case). */
void render_via_placement_preview(SDL_Renderer *renderer, const Camera *cam, int x, int y, int valid);

/* Rubber-band selection box, shown while a Select-mode marquee drag is in
   progress (see app->marquee_active). Screen-space corners, any order. */
void render_marquee_select(SDL_Renderer *renderer, int x0, int y0, int x1, int y1);

/* Screen-space bounding box of an INPUT/OUTPUT wire's H/L label, shared by
   rendering and click hit-testing so the clickable area always exactly
   matches what's drawn. Returns 0 (rect left unset) for WIRE_KIND_NORMAL, if
   font_large is NULL, or if zoomed out too far for the label to be drawn. */
int render_wire_terminal_bounds(TTF_Font *font_large, const Camera *cam, const Wire *w, SignalValue value, SDL_Rect *out_rect);

/* Paints every pin a diagnostic references in that diagnostic's color (red
   for DIAG_ERROR, yellow for DIAG_WARNING) - flagged wires are already
   colored by render_circuit itself (see its diagnostics param), this only
   covers pins with no wire of their own to carry the color (floating/
   fan-out). Errors are always painted last (on top of warnings) so a pin
   flagged by both reads as the more severe color. Purely visual - doesn't
   touch persistent .selected state. */
void render_diagnostic_highlights(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit,
                                   const DiagnosticSet *diagnostics);

/* Bottom-left stack of warning/error chips, one per diagnostic, growing
   left to right - red for errors, yellow for warnings, each labeled with
   its short summary. hover_x/hover_y (screen space) pick out which chip (if
   any) the cursor is over; returns that diagnostic's index into
   diagnostics->items, or -1 if none is hovered. */
int render_diagnostics_panel(SDL_Renderer *renderer, TTF_Font *font, int window_h,
                              const DiagnosticSet *diagnostics, int hover_x, int hover_y);

/* Word-wrapped detail box for one diagnostic, anchored near (anchor_x,
   anchor_y) - shown whether that hover came from a popup chip or a flagged
   canvas wire/pin, so both hover paths share one tooltip look. Flips below
   the anchor instead of above if there isn't room, and clamps horizontally
   to stay on screen. */
void render_diagnostic_tooltip(SDL_Renderer *renderer, TTF_Font *font, const Diagnostic *diag,
                                int anchor_x, int anchor_y, int window_w, int window_h);

/* Small "LayerA <-> LayerB" tooltip shown while hovering a via - same
   anchor/flip/clamp behavior as render_diagnostic_tooltip, but a single
   plain line instead of word-wrapped diagnostic detail text. */
void render_via_tooltip(SDL_Renderer *renderer, TTF_Font *font, const char *layer_a_name, const char *layer_b_name,
                         int anchor_x, int anchor_y, int window_w, int window_h);

#endif
