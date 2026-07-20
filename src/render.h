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
   its short summary. Stops adding chips once the next one wouldn't fully
   fit before window_w - the rest are simply left off rather than spilling
   past the window edge or under whatever's docked on the right (Manage
   Data/Layers panel); nothing else indicates there were more. hover_x/
   hover_y (screen space) pick out which chip (if any) the cursor is over;
   returns that diagnostic's index into diagnostics->items, or -1 if none is
   hovered. */
int render_diagnostics_panel(SDL_Renderer *renderer, TTF_Font *font, int window_w, int window_h,
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

/* Section-Labeling: every in-use section's rectangle outline, label, lock
   icon, and (only while .selected and unlocked) its 4 corner resize handles
   - always drawn regardless of the active tool, so it's useful as a
   landmark no matter what you're doing (see circuit.h's Section). editing_id
   (a circuit->sections[] index, or -1) is which section's label is
   currently being retyped - if it matches, editing_text is shown live (with
   a blinking caret) in place of that section's own committed label, same
   idea as layer_panel.c's rename field. hover_x/hover_y light up a hovered
   lock icon. */
void render_sections(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam, const Circuit *circuit,
                      int editing_id, const char *editing_text, int hover_x, int hover_y);
/* A section rectangle that's been dragged out but not committed to the
   circuit yet - still being typed for the very first time (see
   CANVAS_EDIT_NEW_SECTION in app.h). No lock icon or handles - those only
   make sense for something that already exists. */
void render_section_preview(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam,
                             int x0, int y0, int x1, int y1, const char *editing_text);
/* Screen-space bounds of a section's label text and its lock icon (in that
   left-to-right order, both sitting just above the rectangle's top-right
   corner) - shared by rendering and input_handler.c's click hit-testing,
   same role as render_wire_terminal_bounds. 0 if font is NULL. */
int section_label_bounds(TTF_Font *font, const Camera *cam, const Section *s, SDL_Rect *out_label, SDL_Rect *out_lock);
/* Screen position of one of a section's 4 corner resize handles - corner is
   0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right. */
void section_corner_screen_pos(const Camera *cam, const Section *s, int corner, int *out_x, int *out_y);
/* Whether a section's lock icon should render/be clickable right now - only
   while the cursor is hovering the section's rectangle (plus a small
   margin) or its label, and only above a minimum zoom level below which it
   wouldn't render legibly or be easy to click anyway. Shared by rendering
   and input_handler.c's click hit-testing so a click is never accepted on
   something that isn't actually being drawn (or vice versa). */
int section_lock_icon_visible(TTF_Font *font, const Camera *cam, const Section *s, int hover_x, int hover_y);

/* A single freestanding line of placed text (see circuit.h's TextLabel) -
   always drawn regardless of active tool, same reasoning as Sections above.
   editing_id/editing_text work identically to render_sections'. */
void render_text_labels(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam, const Circuit *circuit,
                         int editing_id, const char *editing_text, int hover_x, int hover_y);
/* A text label placed but not committed yet - see CANVAS_EDIT_NEW_TEXT_LABEL. */
void render_text_label_preview(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam,
                                int x, int y, const char *editing_text);
/* Screen-space bounds of a text label's rendered text - shared with
   input_handler.c's click hit-testing, same role as section_label_bounds. */
int text_label_bounds(TTF_Font *font, const Camera *cam, const TextLabel *t, SDL_Rect *out);

#endif
