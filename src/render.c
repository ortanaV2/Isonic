#include <math.h>
#include <stdio.h>
#include "render.h"
#include "text_util.h"

/* Falstad-style light blue, used both for persistent selection and for the
   temporary "this is what you'd connect to" highlight while dragging a wire. */
static const SDL_Color SELECTION_COLOR = { 90, 170, 255, 255 };
/* Lighter than SELECTION_COLOR, used only for the connection dots at a
   selected/highlighted wire's endpoints - mirrors how a normal wire's own
   dots (CONNECTION_COLOR, near-white) already read lighter than its line. */
static const SDL_Color SELECTION_DOT_COLOR = { 165, 210, 255, 255 };
static const SDL_Color LABEL_COLOR = { 225, 225, 230, 255 };
static const SDL_Color OUTPUT_LABEL_COLOR = { 140, 140, 146, 255 }; /* dimmer, distinguishes Output from Input */
static const SDL_Color IC_NAME_LABEL_COLOR = { 140, 140, 146, 255 }; /* grayish body-name label shown once pins are too small to read */
/* every connection point (wire endpoints, IC pin tips, junctions) uses this
   neutral marker color - only the line/stub itself carries the signal color */
static const SDL_Color CONNECTION_COLOR = { 235, 235, 240, 255 };
static const SDL_Color IC_BORDER_COLOR = { 190, 190, 196, 255 };
static const SDL_Color DIAG_ERROR_COLOR = { 220, 70, 70, 255 };
static const SDL_Color DIAG_WARNING_COLOR = { 235, 190, 40, 255 };
/* A bit more saturated than DIAG_ERROR_COLOR/DIAG_WARNING_COLOR above - used
   only for the bottom-left diagnostics panel's chips (render_diagnostics_panel),
   which read a bit washed-out/pastel at the plain diagnostic color and a
   small block of solid color, unlike a thin wire outline, can afford to be
   a little punchier without looking gaudy on the canvas itself. Blue nudged
   up a touch from a first pass at this that leaned too warm/orange for both
   colors (red reading coral-ish, amber reading orange-ish). */
static const SDL_Color DIAG_PANEL_ERROR_COLOR = { 220, 65, 60, 255 };
static const SDL_Color DIAG_PANEL_WARNING_COLOR = { 235, 175, 45, 255 };
static const SDL_Color VIA_RING_COLOR = { 220, 220, 225, 255 };
/* Must match app.c's SDL_RenderClear color - render_vias punches its ring's
   hole with a same-color circle, the simplest way to get an annulus out of
   the plain filled-circle helper everything else here already uses. */
static const SDL_Color CANVAS_BG_COLOR = { 24, 24, 28, 255 };

/* Thickness as a fraction of the current grid cell size, not a flat pixel
   count, so wires/stubs/borders stay proportionally the same thickness
   relative to the drawn content at any zoom level (matches ~3px at zoom 1.0).
   Stays a float for smooth (non-stepped) scaling. Unlike connection dots,
   this DOES need its own floor: a genuinely sub-pixel-wide diagonal line
   barely covers any real sample in the 2x supersample buffer, so after the
   downscale it reads as faded/see-through rather than merely thin - fading
   the alpha to compensate just made it look more transparent, not less, so
   the floor keeps a solid, fully-opaque minimum width instead. */
#define WIRE_THICKNESS_CELL_FRACTION 0.15f
#define WIRE_THICKNESS_MIN_PX 1.5f

static float wire_thickness_px(float cell) {
    float t = cell * WIRE_THICKNESS_CELL_FRACTION;
    return t < WIRE_THICKNESS_MIN_PX ? WIRE_THICKNESS_MIN_PX : t;
}

/* Same reasoning as wire_thickness_px above - no extra floor here either, see
   draw_filled_circle's own 1px-radius floor for the actual safety net. */
#define CONNECTION_DOT_CELL_FRACTION 0.12f

static float connection_dot_radius_px(float cell) {
    return cell * CONNECTION_DOT_CELL_FRACTION;
}

/* Radius of the pin-1 orientation notch, same idea as a real DIP package's
   half-moon cutout - small relative to the (fixed-width) IC body. */
#define NOTCH_CELL_FRACTION 0.35f

static float notch_radius_px(float cell) {
    float r = cell * NOTCH_CELL_FRACTION;
    return r < 2.0f ? 2.0f : r;
}

/* The font is loaded at a much higher point size than it is ever displayed at
   (see text_util_load_font call in app.c) so labels stay crisp when the
   scaled blit below magnifies them at higher zoom - rendering small and
   stretching up caused visible blur/pixelation. Past label_scale() > 1.0
   (roughly 2x zoom, since LABEL_FONT_POINT_SIZE is 96 against an 18pt
   display size) the fixed-resolution bitmap starts getting magnified beyond
   its own native resolution and blurs again - 96pt pushes that threshold
   out further than the old 48pt did, though it's still a fixed ceiling, not
   a true fix (see text_util.c: no per-zoom re-rasterization or caching). */
#define LABEL_FONT_POINT_SIZE 96.0f
#define LABEL_DISPLAY_POINT_SIZE 18.0f

/* Screen-space gap, in pixels at zoom 1.0, between a pin/terminal edge and
   its label - scales with zoom_factor(), not label_scale(), see below. */
#define LABEL_EDGE_GAP_PX 4.0f

/* Pure zoom multiplier - cell size relative to BASE_CELL_PX, with no font
   metrics mixed in (unlike label_scale, which also carries the fixed
   LABEL_DISPLAY_POINT_SIZE/LABEL_FONT_POINT_SIZE ratio). Screen-space
   paddings/gaps around labels (e.g. "10px of breathing room, at this zoom")
   need this, not label_scale - using label_scale there made every such gap
   silently shrink when LABEL_FONT_POINT_SIZE went from 48 to 96, since that
   ratio is baked into label_scale's output. */
static float zoom_factor(float cell) {
    return cell / BASE_CELL_PX;
}

static float label_scale(float cell) {
    return (cell / BASE_CELL_PX) * (LABEL_DISPLAY_POINT_SIZE / LABEL_FONT_POINT_SIZE);
}

/* Falstad-style: only gray (off/unknown/conflict) or green (high) - no red/yellow. */
static SDL_Color signal_color(SignalValue v) {
    if (v == SIG_HIGH) return (SDL_Color){ 55, 205, 85, 255 };
    return (SDL_Color){ 140, 140, 145, 255 };
}

#define CIRCLE_SEGMENTS 24
#define ISONIC_TAU 6.28318530717958647692f /* avoids relying on M_PI, which -std=c11 doesn't guarantee */

/* Filled polygon fan instead of a per-scanline approximation - the old scanline
   version was blocky at small radii (looked pixelated even under supersampling,
   since its edges were quantized to whole pixel rows rather than true
   sub-pixel positions the AA downscale could actually smooth out). */
static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, float radius) {
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    if (radius < 1.0f) radius = 1.0f;
    SDL_Color col = { r, g, b, a };

    SDL_Vertex verts[CIRCLE_SEGMENTS + 1];
    verts[0].position = (SDL_FPoint){ (float)cx, (float)cy };
    verts[0].color = col;
    verts[0].tex_coord = (SDL_FPoint){ 0, 0 };
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float angle = (float)i / CIRCLE_SEGMENTS * ISONIC_TAU;
        verts[i + 1].position = (SDL_FPoint){ cx + cosf(angle) * radius, cy + sinf(angle) * radius };
        verts[i + 1].color = col;
        verts[i + 1].tex_coord = (SDL_FPoint){ 0, 0 };
    }

    int indices[CIRCLE_SEGMENTS * 3];
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = (i + 1) % CIRCLE_SEGMENTS + 1;
    }
    SDL_RenderGeometry(renderer, NULL, verts, CIRCLE_SEGMENTS + 1, indices, CIRCLE_SEGMENTS * 3);
}

/* Draws one solid filled quad along the line instead of stamping several
   1px-offset parallel SDL_RenderDrawLine calls - the old approach rasterized
   diagonals inconsistently (visibly looked like 2-3 separate thin lines
   instead of one solid band). A round cap is added at both ends (same color)
   so two lines meeting at any angle - IC border corners, wire-to-wire and
   wire-to-stub joints - blend smoothly instead of leaving a flat-edged notch.
   thickness is a float; wire_thickness_px already floors it above the point
   where it would otherwise fade into the supersample downscale (see its
   comment), so there's no separate fade/floor logic needed in here. */
static void draw_thick_line(SDL_Renderer *renderer, int x0, int y0, int x1, int y1, float thickness) {
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = thickness * 0.5f;
    if (hw < 0.5f) hw = 0.5f; /* defensive floor only, in case of a future caller with no floor of its own */

    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    SDL_Color col = { r, g, b, a };

    SDL_Vertex verts[4] = {
        { { x0 + nx * hw, y0 + ny * hw }, col, { 0, 0 } },
        { { x1 + nx * hw, y1 + ny * hw }, col, { 0, 0 } },
        { { x1 - nx * hw, y1 - ny * hw }, col, { 0, 0 } },
        { { x0 - nx * hw, y0 - ny * hw }, col, { 0, 0 } },
    };
    int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(renderer, NULL, verts, 4, indices, 6);

    /* uses the exact same hw as the quad above (no independent rounding) so the
       cap radius can never end up larger than the line's real half-width -
       a mismatch there would make the cap visibly poke out sideways */
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    draw_filled_circle(renderer, x0, y0, hw);
    draw_filled_circle(renderer, x1, y1, hw);
}

/* Strokes a circular arc as a single triangle-strip ribbon, instead of
   chaining several draw_thick_line calls - each of those adds its own round
   end caps, and at internal joints between short segments those caps bulge
   out to both sides of the curve, which at low segment counts or small radii
   (e.g. zoomed far out) reads as a lumpy/rectangular blob rather than a
   smooth curve. A single strip has no internal caps to bulge, so it stays
   smooth regardless of zoom - and since it's a true circle, the per-vertex
   stroke normal is exactly the radial direction, no need to average adjacent
   segment directions like a general polyline stroke would. */
#define NOTCH_ARC_SEGMENTS 16

static void draw_arc_strip(SDL_Renderer *renderer, float cx, float cy, float radius, float angle_from, float angle_to, float thickness) {
    float hw = thickness * 0.5f;
    if (hw < 0.5f) hw = 0.5f;

    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    SDL_Color col = { r, g, b, a };

    SDL_Vertex verts[(NOTCH_ARC_SEGMENTS + 1) * 2];
    for (int i = 0; i <= NOTCH_ARC_SEGMENTS; i++) {
        float t = (float)i / NOTCH_ARC_SEGMENTS;
        float angle = angle_from + (angle_to - angle_from) * t;
        float nx = cosf(angle), ny = sinf(angle); /* radial direction == stroke normal, exact for a circle */
        float px = cx + nx * radius, py = cy + ny * radius;
        verts[i * 2 + 0] = (SDL_Vertex){ { px + nx * hw, py + ny * hw }, col, { 0, 0 } };
        verts[i * 2 + 1] = (SDL_Vertex){ { px - nx * hw, py - ny * hw }, col, { 0, 0 } };
    }

    int indices[NOTCH_ARC_SEGMENTS * 6];
    for (int i = 0; i < NOTCH_ARC_SEGMENTS; i++) {
        int base = i * 2;
        indices[i * 6 + 0] = base + 0;
        indices[i * 6 + 1] = base + 1;
        indices[i * 6 + 2] = base + 2;
        indices[i * 6 + 3] = base + 1;
        indices[i * 6 + 4] = base + 3;
        indices[i * 6 + 5] = base + 2;
    }
    SDL_RenderGeometry(renderer, NULL, verts, (NOTCH_ARC_SEGMENTS + 1) * 2, indices, NOTCH_ARC_SEGMENTS * 6);
}

/* Draws one edge of the IC body's border with a semicircular notch cut into
   its middle, like the orientation marking on a real DIP package - pin 1
   (see component_init_ic) always sits just to its left of wherever this
   edge currently is (see the Edge enum below - the notch stays on whichever
   edge component_init_ic's "top" rotates to, see render_ic_body). The edge
   runs from (x0,y0) to (x1,y1); (inward_nx,inward_ny) is the unit normal
   pointing INTO the body, i.e. which way the notch bulges - this one
   function covers all 4 possible edges (top/bottom/left/right) generically
   instead of 4 hand-mirrored copies, since the geometry is identical up to
   which direction is "inward". The straight edge is split in two around the
   notch instead of drawn full-width underneath it, so the arc reads as an
   actual cut rather than a bump added on top of an intact line. */
static void draw_edge_with_notch(SDL_Renderer *renderer, float x0, float y0, float x1, float y1,
                                  float inward_nx, float inward_ny, float cell, float thickness) {
    float radius = notch_radius_px(cell);
    float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float ux = dx / len, uy = dy / len; /* unit tangent along the edge */

    draw_thick_line(renderer, (int)lroundf(x0), (int)lroundf(y0),
                     (int)lroundf(mx - ux * radius), (int)lroundf(my - uy * radius), thickness);
    draw_thick_line(renderer, (int)lroundf(mx + ux * radius), (int)lroundf(my + uy * radius),
                     (int)lroundf(x1), (int)lroundf(y1), thickness);

    /* a half-circle strip centered on the inward-normal direction always
       bulges into the body regardless of which edge this is - see the
       comment above. */
    float angle_center = atan2f(inward_ny, inward_nx);
    draw_arc_strip(renderer, mx, my, radius, angle_center - ISONIC_TAU * 0.25f, angle_center + ISONIC_TAU * 0.25f,
                    thickness);
}

/* Which side of the body's current (rotation-applied) bounding box an edge
   is - deliberately numbered in counterclockwise rotation order (LEFT ->
   BOTTOM -> RIGHT -> TOP -> LEFT) so "rotate this edge by c->rotation
   quarter-turns" is just (edge + rotation) % 4 - see component_pin_world_pos
   for the matching point-rotation math this must stay consistent with. */
typedef enum { EDGE_LEFT = 0, EDGE_BOTTOM = 1, EDGE_RIGHT = 2, EDGE_TOP = 3 } BodyEdge;

void render_grid(SDL_Renderer *renderer, const Camera *cam, int window_w, int window_h) {
    /* caller clears the background before invoking this; only grid dots are drawn here */
    float cell = camera_cell_px(cam);
    if (cell < 6.0f) return; /* too dense to draw meaningfully */

    int gx0, gy0, gx1, gy1;
    camera_screen_to_grid(cam, 0, 0, &gx0, &gy0);
    camera_screen_to_grid(cam, window_w, window_h, &gx1, &gy1);

    SDL_SetRenderDrawColor(renderer, 48, 48, 54, 255);
    for (int gy = gy0 - 1; gy <= gy1 + 1; gy++) {
        for (int gx = gx0 - 1; gx <= gx1 + 1; gx++) {
            int sx, sy;
            camera_grid_to_screen(cam, gx, gy, &sx, &sy);
            SDL_RenderDrawPoint(renderer, sx, sy);
        }
    }
}

void render_wire_preview(SDL_Renderer *renderer, const Camera *cam, int fx, int fy, int tx, int ty) {
    /* still exactly on the start point - no length yet, so there is nothing
       to preview. Without this, a click that hasn't moved yet (or a drag
       that rounds back to the same grid point) would still show two
       overlapping dots, which reads as a wire having been placed at a single
       point even though circuit_add_wire (rightly) refuses to create one. */
    if (fx == tx && fy == ty) return;

    int sfx, sfy, stx, sty;
    camera_grid_to_screen(cam, fx, fy, &sfx, &sfy);
    camera_grid_to_screen(cam, tx, ty, &stx, &sty);

    float cell = camera_cell_px(cam);
    SDL_SetRenderDrawColor(renderer, SELECTION_COLOR.r, SELECTION_COLOR.g, SELECTION_COLOR.b, 255);
    draw_thick_line(renderer, sfx, sfy, stx, sty, wire_thickness_px(cell));

    float r = connection_dot_radius_px(cell);
    SDL_SetRenderDrawColor(renderer, SELECTION_DOT_COLOR.r, SELECTION_DOT_COLOR.g, SELECTION_DOT_COLOR.b, 255);
    draw_filled_circle(renderer, sfx, sfy, r);
    draw_filled_circle(renderer, stx, sty, r);
}

void render_via_placement_preview(SDL_Renderer *renderer, const Camera *cam, int x, int y, int valid) {
    /* pure proportional scaling, same as render_junctions' plain
       connection_dot_radius_px(cell) - no minimum-size floor here, only
       draw_filled_circle's own 1px floor as the actual safety net (see its
       comment), so a via never grows relatively bigger than a junction dot
       as you zoom out. */
    float outer_r = connection_dot_radius_px(camera_cell_px(cam)) * 1.6f;
    float inner_r = outer_r * 0.5f;
    int sx, sy;
    camera_grid_to_screen(cam, x, y, &sx, &sy);

    SDL_Color ring = valid ? VIA_RING_COLOR : DIAG_ERROR_COLOR;
    SDL_SetRenderDrawColor(renderer, ring.r, ring.g, ring.b, 150);
    draw_filled_circle(renderer, sx, sy, outer_r);
    /* see render_vias' matching comment - fades out smoothly instead of a
       hard cutoff */
    float hole_fade = (inner_r - 1.0f) / 1.0f;
    if (hole_fade < 0.0f) hole_fade = 0.0f;
    if (hole_fade > 1.0f) hole_fade = 1.0f;
    int hole_alpha = (int)(hole_fade * 150.0f);
    if (hole_alpha > 0) {
        SDL_SetRenderDrawColor(renderer, CANVAS_BG_COLOR.r, CANVAS_BG_COLOR.g, CANVAS_BG_COLOR.b, hole_alpha);
        draw_filled_circle(renderer, sx, sy, inner_r);
    }
}

void render_marquee_select(SDL_Renderer *renderer, int x0, int y0, int x1, int y1) {
    SDL_Rect r = {
        x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
        (x0 < x1 ? x1 - x0 : x0 - x1), (y0 < y1 ? y1 - y0 : y0 - y1),
    };
    SDL_SetRenderDrawColor(renderer, SELECTION_COLOR.r, SELECTION_COLOR.g, SELECTION_COLOR.b, 45);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, SELECTION_COLOR.r, SELECTION_COLOR.g, SELECTION_COLOR.b, 200);
    SDL_RenderDrawRect(renderer, &r);
}

/* Highest-severity diagnostic (if any) that references this wire - ERROR
   outranks WARNING when a wire happens to be flagged by both. */
static int wire_diag_color(const DiagnosticSet *diagnostics, int wire_id, SDL_Color *out) {
    if (diagnostics == NULL) return 0;
    int found = 0;
    DiagSeverity worst = DIAG_WARNING;
    for (int i = 0; i < diagnostics->count; i++) {
        if (!diagnostic_has_wire(&diagnostics->items[i], wire_id)) continue;
        if (!found || diagnostics->items[i].severity > worst) worst = diagnostics->items[i].severity;
        found = 1;
    }
    if (found) *out = (worst == DIAG_ERROR) ? DIAG_ERROR_COLOR : DIAG_WARNING_COLOR;
    return found;
}

/* Line only - the endpoint dots are drawn later, in a pass over the top of
   every line/stub (see render_wire_dots), so a stub or another wire drawn
   afterwards can never slice back through an already-placed dot. Color
   priority: selection/hover always wins (so a flagged wire you're pointing
   at or have selected reads as blue, not red/yellow), then any diagnostic
   flagging this wire, then - while layer_preview is active (Shift held or
   locked, see app.h) - that wire's own layer color, otherwise its plain
   gray/green signal color. */
static void render_wire_line(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit, const Wire *w,
                              int wire_id, SignalValue value, int highlighted, const DiagnosticSet *diagnostics,
                              int layer_preview) {
    SDL_Color color;
    SDL_Color diag_color;
    if (w->selected || highlighted) {
        color = SELECTION_COLOR;
    } else if (wire_diag_color(diagnostics, wire_id, &diag_color)) {
        color = diag_color;
    } else if (layer_preview) {
        const Layer *l = &circuit->layers[w->layer_slot];
        color.r = l->color_r;
        color.g = l->color_g;
        color.b = l->color_b;
        color.a = 255;
    } else {
        color = signal_color(value);
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

    int sfx, sfy, stx, sty;
    camera_grid_to_screen(cam, w->from_x, w->from_y, &sfx, &sfy);
    camera_grid_to_screen(cam, w->to_x, w->to_y, &stx, &sty);
    draw_thick_line(renderer, sfx, sfy, stx, sty, wire_thickness_px(camera_cell_px(cam)));
}

/* A dot is only ever drawn at a point that is either a real junction (3+
   endpoints, or a mid-span tap - see circuit->junctions) or a lone, totally
   unconnected pin/wire-end (nothing else there at all). An ordinary 1-to-1
   connection (e.g. one wire ending exactly on one pin) needs no marker. */
static void draw_lone_connection_dot(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit, int x, int y) {
    if (circuit_point_is_junction(circuit, x, y)) return;
    if (circuit_point_connection_count(circuit, x, y) != 1) return;

    int sx, sy;
    camera_grid_to_screen(cam, x, y, &sx, &sy);
    float r = connection_dot_radius_px(camera_cell_px(cam));
    SDL_SetRenderDrawColor(renderer, CONNECTION_COLOR.r, CONNECTION_COLOR.g, CONNECTION_COLOR.b, 255);
    draw_filled_circle(renderer, sx, sy, r);
}

static void render_wire_dots(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit, const Wire *w) {
    /* the "from" end of an INPUT/OUTPUT wire gets its H/L label instead of a
       plain dot (see render_wire_terminal) - only the open "to" end follows
       the normal lone/junction dot rules */
    if (w->kind == WIRE_KIND_NORMAL) {
        draw_lone_connection_dot(renderer, cam, circuit, w->from_x, w->from_y);
    }
    draw_lone_connection_dot(renderer, cam, circuit, w->to_x, w->to_y);
}

static void render_junctions(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit) {
    float r = connection_dot_radius_px(camera_cell_px(cam));
    SDL_SetRenderDrawColor(renderer, CONNECTION_COLOR.r, CONNECTION_COLOR.g, CONNECTION_COLOR.b, 255);
    for (int i = 0; i < circuit->junction_count; i++) {
        int sx, sy;
        camera_grid_to_screen(cam, circuit->junctions[i].x, circuit->junctions[i].y, &sx, &sy);
        draw_filled_circle(renderer, sx, sy, r);
    }
}

/* A via is drawn as an annulus (filled ring) - visually distinct from a
   plain connection dot, since it means something electrically special
   (bridges two specific layers here, see Via's doc comment in circuit.h)
   rather than just "a wire ends/junctions here". Punched via a second,
   smaller circle in the canvas's own background color rather than true
   ring geometry - simplest way to get a ring shape out of the same
   draw_filled_circle helper everything else here already uses. */
static void render_vias(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit) {
    /* pure proportional scaling, same as render_junctions' plain
       connection_dot_radius_px(cell) - no minimum-size floor here, only
       draw_filled_circle's own 1px floor as the actual safety net (see its
       comment), so a via never grows relatively bigger than a junction dot
       as you zoom out. */
    float outer_r = connection_dot_radius_px(camera_cell_px(cam)) * 1.6f;
    float inner_r = outer_r * 0.5f;
    /* below inner_r == 1px, draw_filled_circle's own floor would clamp it up
       to (or past) outer_r, painting the "hole" fully over the ring instead
       of merely shrinking it. Rather than a hard on/off switch (which made
       the hole visibly pop in/out at one exact zoom level), fade its alpha
       out smoothly over the last 1px of shrinkage, so it's already fully
       transparent well before the floor would otherwise make it collide
       with the ring - a continuous shrink-to-nothing instead of a snap. */
    float hole_fade = (inner_r - 1.0f) / 1.0f;
    if (hole_fade < 0.0f) hole_fade = 0.0f;
    if (hole_fade > 1.0f) hole_fade = 1.0f;
    int hole_alpha = (int)(hole_fade * 255.0f);

    for (int i = 0; i < circuit->via_high_water; i++) {
        const Via *v = &circuit->vias[i];
        if (!v->in_use) continue;
        int sx, sy;
        camera_grid_to_screen(cam, v->x, v->y, &sx, &sy);
        SDL_SetRenderDrawColor(renderer, VIA_RING_COLOR.r, VIA_RING_COLOR.g, VIA_RING_COLOR.b, 255);
        draw_filled_circle(renderer, sx, sy, outer_r);
        if (hole_alpha > 0) {
            SDL_SetRenderDrawColor(renderer, CANVAS_BG_COLOR.r, CANVAS_BG_COLOR.g, CANVAS_BG_COLOR.b, hole_alpha);
            draw_filled_circle(renderer, sx, sy, inner_r);
        }
    }
}

/* Selected/highlighted wires get their own two endpoints marked in
   SELECTION_COLOR, drawn on top of whatever render_wire_dots/render_junctions
   already put there - even an "ordinary" 1-to-1 connection that normally has
   no dot at all gets one while its wire is selected/hovered, so the grab
   points a Select-mode drag would move are always visible. */
static void render_wire_endpoint_marks(SDL_Renderer *renderer, const Camera *cam, const Wire *w, int highlighted) {
    if (!w->selected && !highlighted) return;
    int sfx, sfy, stx, sty;
    camera_grid_to_screen(cam, w->from_x, w->from_y, &sfx, &sfy);
    camera_grid_to_screen(cam, w->to_x, w->to_y, &stx, &sty);
    float r = connection_dot_radius_px(camera_cell_px(cam));
    SDL_SetRenderDrawColor(renderer, SELECTION_DOT_COLOR.r, SELECTION_DOT_COLOR.g, SELECTION_DOT_COLOR.b, 255);
    draw_filled_circle(renderer, sfx, sfy, r);
    draw_filled_circle(renderer, stx, sty, r);
}

/* An Input wire's terminal always shows what the user actually set it to
   (w->input_value), never the net's resolved value - once two disagreeing
   Input wires share a net, sim.c resolves the whole net (including this
   wire's own circuit->wire_values entry) to SIG_CONFLICT, which would
   otherwise make the terminal you just set to HIGH silently read back as
   LOW. Output wires still show the real resolved net value, since they're
   read-only monitors of what's actually on the wire. */
static SignalValue terminal_display_value(const Wire *w, SignalValue net_value) {
    return (w->kind == WIRE_KIND_INPUT) ? (w->input_value ? SIG_HIGH : SIG_LOW) : net_value;
}

int render_wire_terminal_bounds(TTF_Font *font_large, const Camera *cam, const Wire *w, SignalValue value, SDL_Rect *out_rect) {
    if (w->kind == WIRE_KIND_NORMAL || font_large == NULL) return 0;
    value = terminal_display_value(w, value);
    float cell = camera_cell_px(cam);
    if (cell < 6.0f) return 0;
    float scale = label_scale(cell);

    int asx, asy;
    camera_grid_to_screen(cam, w->from_x, w->from_y, &asx, &asy);

    /* direction is computed from the fixed grid coordinates, not the recomputed
       screen ones - for a near-diagonal wire, screen-space dx/dy drift very
       slightly at different zoom levels (floating point pan/cell math), which
       was enough to occasionally flip which side "wins" in the comparison
       below and make the label jump/snap left-right while zooming */
    int gdx = w->from_x - w->to_x, gdy = w->from_y - w->to_y;
    float glen = sqrtf((float)(gdx * gdx + gdy * gdy));
    float ux = (glen > 0.0001f) ? gdx / glen : 1.0f;
    float uy = (glen > 0.0001f) ? gdy / glen : 0.0f;

    const char *text = (value == SIG_HIGH) ? "H" : "L";
    int tw, th;
    text_util_measure(font_large, text, &tw, &th);
    int stw = (int)lroundf(tw * scale);
    int sth = (int)lroundf(th * scale);
    int gap = (int)lroundf(LABEL_EDGE_GAP_PX * zoom_factor(cell));

    int label_x, label_y;
    if (fabsf(ux) >= fabsf(uy)) {
        int anchor_x = asx + (int)lroundf(ux * gap);
        label_x = (ux >= 0) ? anchor_x : anchor_x - stw;
        label_y = asy - sth / 2;
    } else {
        int anchor_y = asy + (int)lroundf(uy * gap);
        label_x = asx - stw / 2;
        label_y = (uy >= 0) ? anchor_y : anchor_y - sth;
    }
    out_rect->x = label_x;
    out_rect->y = label_y;
    out_rect->w = stw;
    out_rect->h = sth;
    return 1;
}

/* Input/Output are ordinary wires (WIRE_KIND_INPUT/OUTPUT) drawn like any
   other - drag-to-length, any angle, chainable/T-tappable exactly like a
   plain wire. The only difference is the "from" end: instead of a plain
   connection dot, it gets a clickable "H"/"L" label placed just past the
   endpoint, continuing outward along the wire's own direction so it never
   overlaps the line. Input labels use the normal label color; Output is
   dimmer and read-only (see input_handler.c). Labels never show a selection
   outline (even if the wire is selected) - the line itself turns SELECTION_COLOR. */
static void render_wire_terminal(SDL_Renderer *renderer, TTF_Font *font_large, const Camera *cam,
                                  const Wire *w, SignalValue value) {
    SDL_Rect bounds;
    if (!render_wire_terminal_bounds(font_large, cam, w, value, &bounds)) return;

    value = terminal_display_value(w, value);
    const char *text = (value == SIG_HIGH) ? "H" : "L";
    SDL_Color label_color = (w->kind == WIRE_KIND_INPUT) ? LABEL_COLOR : OUTPUT_LABEL_COLOR;
    float scale = label_scale(camera_cell_px(cam));
    text_util_draw_scaled(renderer, font_large, text, bounds.x, bounds.y, label_color, scale);
}

/* Below this cell size, individual pin labels turn into unreadable clutter
   - the body switches to one big centered IC name instead (see
   render_ic_body). Above it, pin names are shown as usual. */
#define PIN_LABEL_MIN_CELL_PX 9.0f

/* Pin labels are drawn inside the body, growing inward from their own edge
   (see render_ic_body) - and IC_DIP_WIDTH is a fixed body width regardless
   of pin count or name length, so a long name (e.g. TLC555's "RESET"/
   "THRES"/"DISCH" - much longer than the 1-3 char names every other IC in
   the catalog happens to have) can run past the body's horizontal center
   and collide with whatever label is growing inward from the opposite side.
   Shrinks the scale for just THIS one label until it fits within
   available_px; short labels that already fit are returned unchanged, so
   this never affects any pin whose name doesn't actually need it. */
static float fit_label_scale(TTF_Font *font, const char *text, float scale, float available_px) {
    int tw, th;
    text_util_measure(font, text, &tw, &th);
    (void)th;
    float text_w = tw * scale;
    if (tw <= 0 || text_w <= available_px) return scale;
    return scale * (available_px / text_w);
}

/* Schematic-symbol style IC: fixed-width rectangle body sized by pin count
   (see ic_dip_body_size), a real-DIP-style pin-1 notch on the top edge, pins
   drawn as short stubs poking out past the edge with a dot at the tip, and
   pin names labeled inside the body next to the stub - not a physical
   DIP/PCB footprint. Pin dots are NOT drawn here - see
   render_component_pin_dots, called later in a dedicated top layer so a stub
   line can never slice back through a dot. */
static void render_ic_body(SDL_Renderer *renderer, TTF_Font *font_large, const Camera *cam, const Component *c,
                            int highlighted, int show_pin_labels, int ghost, int ghost_valid) {
    /* what "highlighted" means for a ghost - solid SELECTION_COLOR while it
       could actually be placed there, the same red DIAG_ERROR_COLOR/
       render_via_placement_preview already use for "can't drop this here"
       when it can't (see render_ic_ghost) - replaces the old separate
       translucent footprint-box overlay this used to be layered under,
       which showed through the body's own notch cutout as a mismatched
       rectangle instead of following the body's actual outline. Unused
       (and irrelevant) when ghost is false. */
    SDL_Color ghost_col = ghost_valid ? SELECTION_COLOR : DIAG_ERROR_COLOR;
    const IC_Def *def = c->ic_def;
    int body_w_cells, body_h_cells;
    component_get_size(c, &body_w_cells, &body_h_cells); /* already swapped for a 90/270 rotation */

    int sx, sy;
    camera_grid_to_screen(cam, c->grid_x, c->grid_y, &sx, &sy);
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    int w = (int)lroundf(body_w_cells * cell);
    int h = (int)lroundf(body_h_cells * cell);
    float thickness = wire_thickness_px(cell);

    /* pin edge anchors (screen-space) are computed once and reused by both the
       stub pass and the label pass below, instead of recomputing per pin twice.
       cur_edge is which side of the CURRENT (rotated) bounding box this pin
       attaches to - the pin's base side (left/right, see component_init_ic)
       rotated the same c->rotation quarter-turns as everything else, using
       the same counterclockwise BodyEdge numbering component_pin_world_pos's
       point-rotation math is built to agree with. */
    BodyEdge cur_edge[MAX_PINS_PER_COMPONENT];
    int edge_sx[MAX_PINS_PER_COMPONENT], edge_sy[MAX_PINS_PER_COMPONENT];
    int tip_sx[MAX_PINS_PER_COMPONENT], tip_sy[MAX_PINS_PER_COMPONENT];
    for (int pi = 0; pi < c->pin_count; pi++) {
        BodyEdge base_edge = (c->pins[pi].local_dx < 0) ? EDGE_LEFT : EDGE_RIGHT;
        cur_edge[pi] = (BodyEdge)((base_edge + c->rotation) & 3);

        int tip_x, tip_y;
        component_pin_world_pos(c, pi, &tip_x, &tip_y);
        camera_grid_to_screen(cam, tip_x, tip_y, &tip_sx[pi], &tip_sy[pi]);

        switch (cur_edge[pi]) {
            case EDGE_LEFT:   edge_sx[pi] = sx;     edge_sy[pi] = tip_sy[pi]; break;
            case EDGE_RIGHT:  edge_sx[pi] = sx + w; edge_sy[pi] = tip_sy[pi]; break;
            case EDGE_TOP:    edge_sx[pi] = tip_sx[pi]; edge_sy[pi] = sy;     break;
            default /* BOTTOM */: edge_sx[pi] = tip_sx[pi]; edge_sy[pi] = sy + h; break;
        }
    }

    /* stubs are drawn before the border (not after) so the border - opaque,
       drawn on top - cleanly caps off the seam where a stub's round end
       would otherwise bleed a little past the edge into the body interior */
    for (int pi = 0; pi < c->pin_count; pi++) {
        const Pin *p = &c->pins[pi];
        /* a ghost's pins have no real signal value to show (nothing's been
           simulated yet - see render_ic_ghost) - ghost_col throughout
           instead, same highlighted look every other ghost element (wires,
           sections, text labels) already uses */
        SDL_Color col = ghost ? ghost_col : signal_color(p->value);
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
        draw_thick_line(renderer, edge_sx[pi], edge_sy[pi], tip_sx[pi], tip_sy[pi], thickness);
    }

    SDL_Color border = ghost ? ghost_col : ((c->selected || highlighted) ? SELECTION_COLOR : IC_BORDER_COLOR);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    /* one straight segment per side of the current bounding box, in BodyEdge
       order - whichever one the notch (always the IC's "pin 1" side, base
       EDGE_TOP, rotated the same as everything else) currently lands on gets
       drawn with draw_edge_with_notch instead of a plain line. */
    float ex0[4] = { (float)sx, (float)sx, (float)(sx + w), (float)sx };
    float ey0[4] = { (float)sy, (float)(sy + h), (float)sy, (float)sy };
    float ex1[4] = { (float)sx, (float)(sx + w), (float)(sx + w), (float)(sx + w) };
    float ey1[4] = { (float)(sy + h), (float)(sy + h), (float)(sy + h), (float)sy };
    float enx[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
    float eny[4] = { 0.0f, -1.0f, 0.0f, 1.0f };
    int notch_edge = (EDGE_TOP + c->rotation) & 3;
    for (int e = 0; e < 4; e++) {
        if (e == notch_edge) {
            draw_edge_with_notch(renderer, ex0[e], ey0[e], ex1[e], ey1[e], enx[e], eny[e], cell, thickness);
        } else {
            draw_thick_line(renderer, (int)lroundf(ex0[e]), (int)lroundf(ey0[e]), (int)lroundf(ex1[e]),
                             (int)lroundf(ey1[e]), thickness);
        }
    }

    if (font_large != NULL && cell >= PIN_LABEL_MIN_CELL_PX && show_pin_labels) {
        /* a label may not grow past the body's own center along whichever
           axis it grows on - leaves a small safety gap so two labels facing
           each other never touch even when both happen to be exactly at
           their fitted width. Left/right-edge pins grow horizontally
           (constrained by half the body's width), top/bottom-edge pins grow
           vertically (constrained by half its height) - only one of the two
           ever applies to a real (non-square) DIP body's pins at once, but
           both are computed since a rotated body can have either kind. */
        float gap_px = LABEL_EDGE_GAP_PX * zoom_factor(cell);
        float available_x = w * 0.5f - gap_px - 2.0f;
        if (available_x < 4.0f) available_x = 4.0f;
        float available_y = h * 0.5f - gap_px - 2.0f;
        if (available_y < 4.0f) available_y = 4.0f;

        for (int pi = 0; pi < c->pin_count; pi++) {
            const Pin *p = &c->pins[pi];
            int horizontal_growth = (cur_edge[pi] == EDGE_LEFT || cur_edge[pi] == EDGE_RIGHT);
            /* fit_label_scale itself always measures/constrains the text's
               UNROTATED width (tw) - exactly the dimension that ends up
               running along the growth axis either way: unrotated for a
               left/right pin (available_x), or rotated 90 degrees into the
               vertical for a top/bottom pin (available_y) - so passing the
               right `available` here is the only thing that needs to change
               between the two cases, not fit_label_scale itself. */
            float pin_scale = fit_label_scale(font_large, p->name, scale, horizontal_growth ? available_x : available_y);
            int tw, th;
            text_util_measure(font_large, p->name, &tw, &th);
            int stw = (int)lroundf(tw * pin_scale);
            int sth = (int)lroundf(th * pin_scale);

            if (horizontal_growth) {
                int label_x, label_y;
                if (cur_edge[pi] == EDGE_LEFT) {
                    label_x = edge_sx[pi] + (int)lroundf(gap_px);
                } else {
                    label_x = edge_sx[pi] - (int)lroundf(gap_px) - stw;
                }
                label_y = edge_sy[pi] - sth / 2;
                text_util_draw_scaled(renderer, font_large, p->name, label_x, label_y, LABEL_COLOR, pin_scale);
            } else {
                /* a top/bottom-edge pin (only reachable at a 90/270 degree
                   IC rotation) packs its neighbors just as tightly along the
                   edge as a left/right pin's own name normally grows freely
                   - drawing it unrotated would collide with the next pin's
                   label exactly like the report screenshot this was fixed
                   from. Rotating it 90 degrees turns the same "grows inward,
                   away from the edge" layout sideways so it uses the gap
                   between the pin row and the body's center instead, the
                   same free space a horizontal label already had. */
                int center_x = edge_sx[pi];
                int center_y = (cur_edge[pi] == EDGE_TOP)
                                    ? edge_sy[pi] + (int)lroundf(gap_px) + stw / 2
                                    : edge_sy[pi] - (int)lroundf(gap_px) - stw / 2;
                text_util_draw_scaled_rotated(renderer, font_large, p->name, center_x, center_y, LABEL_COLOR,
                                               pin_scale, -90.0f);
            }
        }
    } else if (font_large != NULL) {
        /* pins are too small to label individually - show one big name
           instead, running along the body's long axis (see ic_dip_body_size -
           vertical at rotation 0/180, horizontal at 90/270 since w/h swap
           with it, same as the body itself). No lower cell bound here - it
           should keep shrinking along with everything else instead of
           disappearing once zoomed out past some fixed cutoff. */
        int tw, th;
        text_util_measure(font_large, def->name, &tw, &th);
        if (tw > 0 && th > 0) {
            /* at rotation 0/180 the body is tall/narrow, so the name is
               drawn rotated -90 (vertical) to run along it - tw (text
               width) becomes the body's vertical extent and th its
               horizontal one. At rotation 90/270 ("flach liegend") the body
               is wide/short instead, and the name is drawn upright (0
               degrees, actually readable rather than sideways) - so tw/th
               map onto w/h directly instead, NOT swapped the same way. Using
               the swapped mapping here regardless of angle (the original
               bug) starved the fit against the body's now-tiny short axis,
               which is why the name rendered far smaller than it should
               have specifically on a rotated IC. */
            float fit_a, fit_b;
            if (c->rotation & 1) {
                fit_a = (w * 0.6f) / tw;
                fit_b = (h * 0.6f) / th;
            } else {
                fit_a = (h * 0.6f) / tw;
                fit_b = (w * 0.6f) / th;
            }
            float name_scale = fit_a < fit_b ? fit_a : fit_b;
            float text_angle = (c->rotation & 1) ? 0.0f : -90.0f;
            if (name_scale > 0.0f) {
                SDL_Color name_col = ghost ? ghost_col : IC_NAME_LABEL_COLOR;
                text_util_draw_scaled_rotated(renderer, font_large, def->name, sx + w / 2, sy + h / 2,
                                               name_col, name_scale, text_angle);
            }
        }
    }
}

void render_ic_ghost(SDL_Renderer *renderer, TTF_Font *font_large, const Camera *cam, const IC_Def *def,
                      int grid_x, int grid_y, int rotation, int valid) {
    Component ghost = { 0 };
    component_init_ic(&ghost, grid_x, grid_y, def);
    ghost.rotation = rotation;
    /* never showing individual pin labels (just the big centered part name,
       same as a real placed IC falls back to once zoomed out too far for
       those to fit - see render_ic_body's own show_pin_labels branch), and
       ghost=1 forces the border/pin-stubs/name into a solid highlighted
       color throughout (SELECTION_COLOR if valid, DIAG_ERROR_COLOR-red if
       not - see render_ic_body's own ghost_col) instead of a not-yet-placed
       component's nonexistent net/signal state - this IS the validity
       indicator now, replacing the old separate translucent footprint-box
       overlay entirely (see the caller). */
    render_ic_body(renderer, font_large, cam, &ghost, 0, 0, 1, valid);
}

static void render_component_pin_dots(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit, const Component *c) {
    for (int pi = 0; pi < c->pin_count; pi++) {
        int tip_x, tip_y;
        component_pin_world_pos(c, pi, &tip_x, &tip_y);
        draw_lone_connection_dot(renderer, cam, circuit, tip_x, tip_y);
    }
}

/* ── Section-Labeling / Text Labels ──────────────────────────────────────────
   Purely organizational canvas annotations (see circuit.h's Section/
   TextLabel) - no pins, no nets, nothing electrical. Same light gray as
   IC_BORDER_COLOR (the app's one existing "neutral UI chrome" gray) rather
   than inventing a second one. */
static const SDL_Color SECTION_COLOR = { 190, 190, 196, 255 };
#define SECTION_LABEL_GAP_PX 6.0f  /* screen-space, at zoom 1.0 - see zoom_factor */
#define SECTION_LOCK_ICON_PX 14.0f
#define SECTION_HANDLE_PX 8.0f
/* small aesthetic-only lift on top of the label's plain cell-centered
   position - see section_label_bounds. */
#define SECTION_LABEL_EXTRA_LIFT_PX 3.0f

/* Blinking text-entry caret shared by both Section-label and Text-Label
   editing below. Height is the font's own ASCENT (baseline-to-top) scaled
   by zoom - deliberately the SAME metric cell_vcenter_offset positions
   text_y with (see its own comment), not the fixed "M" glyph's full
   ascent-to-descent box a first pass at this used: text_y already assumes
   a height of exactly one ascent when centering on the cell, so drawing
   the caret any taller (or shorter) than that made it visibly extend past
   center instead of spanning the cell symmetrically. Unlike layer_panel.c's
   own draw_text_cursor, which uses a fixed 14px because its text lives at
   a fixed UI scale - canvas text is scaled by the current zoom (see
   `scale`), so a fixed pixel height here would either dwarf or vanish
   under the letters depending on zoom. */
static void draw_canvas_text_cursor(SDL_Renderer *renderer, TTF_Font *font, const char *text, int text_x, int text_y,
                                     float scale, SDL_Color col) {
    if ((SDL_GetTicks() / 500) % 2 != 0) return;
    int tw = 0, th = 0;
    if (font != NULL) text_util_measure(font, text, &tw, &th);
    (void)th;
    int ascent = 0, descent = 0;
    if (font != NULL) text_util_font_metrics(font, &ascent, &descent);
    (void)descent;
    int stw = (int)lroundf(tw * scale);
    int sh = (int)lroundf(ascent * scale);
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
    SDL_RenderDrawLine(renderer, text_x + stw + 1, text_y, text_x + stw + 1, text_y + sh);
}

/* Padlock pictogram: a body, two straight "riser" legs, and a shackle arc
   joining their tops via draw_arc_strip (same technique the IC body's
   pin-1 notch uses for its own arc) - the legs run straight up a bit before
   the arc takes over, same as a real shackle, rather than the arc sitting
   flush on the body as a bare semicircle (which read as barely visible).
   Vertically balanced around the icon box's own middle (the arc's peak
   reaches nearly to the top, the body's bottom nearly to the bottom)
   instead of being weighted toward the bottom half, which is what made the
   very first version look like it was sitting too low relative to
   whatever text sits next to it (see section_label_bounds, which centers
   this box on the label's own height - only works if the icon's drawn
   content is itself centered in that box).

   Closed (locked) draws both risers, reading as a complete staple. Open
   (unlocked) is the EXACT SAME shape, just with the right riser left out -
   the arc's right end and the body below it are no longer connected by
   anything, reading as "unhooked on that side" while the left leg still
   visibly holds the shackle in place. A rotated copy of the whole shackle
   (the first two passes at this) either just looked offset or looked
   identical to closed at this icon's size - simply dropping a leg reads as
   unmistakably open even this small. */
static void draw_lock_icon(SDL_Renderer *renderer, const SDL_Rect *box, int locked, SDL_Color col) {
    float cx = box->x + box->w * 0.5f;
    float body_top = box->y + box->h * 0.50f;
    float body_h = box->h * 0.42f;
    float body_w = box->w * 0.78f;
    float thick = box->w * 0.11f;
    float shackle_r = box->w * 0.22f;
    float riser_h = box->h * 0.20f;
    float shackle_base_y = body_top - riser_h;

    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
    draw_thick_line(renderer, (int)lroundf(cx - shackle_r), (int)lroundf(body_top),
                     (int)lroundf(cx - shackle_r), (int)lroundf(shackle_base_y), thick);
    if (locked) {
        draw_thick_line(renderer, (int)lroundf(cx + shackle_r), (int)lroundf(body_top),
                         (int)lroundf(cx + shackle_r), (int)lroundf(shackle_base_y), thick);
    }
    draw_arc_strip(renderer, cx, shackle_base_y, shackle_r, ISONIC_TAU * 0.5f, ISONIC_TAU, thick);

    SDL_Rect body = { (int)lroundf(cx - body_w * 0.5f), (int)lroundf(body_top), (int)lroundf(body_w), (int)lroundf(body_h) };
    SDL_RenderFillRect(renderer, &body);
}

/* Vertical offset (screen px, already zoom-scaled) from a grid CELL's
   top-left lattice point up to where a single line of text anchored to
   that cell should actually start drawing, so the text (and its cursor
   while typing) sits vertically CENTERED within the cell - not centered ON
   the lattice point itself, which splits the difference between that cell
   and the one above it instead of sitting inside either.

   Centers on the font's ASCENT alone (baseline-to-top), not the full
   ascent-to-descent line box text_util_measure/TTF_RenderUTF8_Blended
   actually render into: ordinary text with no descenders (g/y/p/q/j) only
   ever draws ink in the ascent portion, so treating the full box (which
   includes the descent's empty space below the baseline) as "the glyph"
   pulls the centering down below where the text visually reads as
   centered. Using a font-level metric instead of measuring any particular
   string also means the position never jitters as you type/backspace
   different characters. Shared by section_label_bounds, text_label_bounds,
   and both their _preview siblings, so a not-yet-committed label previews
   at the exact same spot its committed self will render at, and reused
   as-is for the blinking caret (draw_canvas_text_cursor) so both sit at
   the exact same height - no separate correction applied to one but not
   the other. */
static int cell_vcenter_offset(TTF_Font *font, float scale, float cell) {
    int ascent = 0, descent = 0;
    text_util_font_metrics(font, &ascent, &descent);
    (void)descent;
    float half_ascent = ascent * scale * 0.5f;
    float half_cell = cell * 0.5f;
    return (int)lroundf(half_ascent - half_cell);
}

int section_label_bounds(TTF_Font *font, const Camera *cam, const Section *s, SDL_Rect *out_label, SDL_Rect *out_lock) {
    if (font == NULL) return 0;
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    float zoom = zoom_factor(cell);
    int tw, th;
    text_util_measure(font, (s->label[0] != '\0') ? s->label : " ", &tw, &th);
    int stw = (int)lroundf(tw * scale);
    int sth = (int)lroundf(th * scale);
    float gap = SECTION_LABEL_GAP_PX * zoom;

    /* the label lives in the grid cell directly above the section's top
       edge (from y0-1 to y0), vertically centered in THAT cell exactly like
       a Text Label centers in its own anchor cell - not a fixed pixel gap
       above the rectangle, which gave the label no relationship to the
       grid at all (see cell_vcenter_offset). Still right-aligned to the
       section's own right edge (x1) horizontally, unchanged. */
    int sx1, cell_top_sy;
    camera_grid_to_screen(cam, s->x1, s->y0 - 1, &sx1, &cell_top_sy);
    /* a small extra lift on top of the plain cell-centered position - purely
       an aesthetic nudge (unlike cell_vcenter_offset, not trying to line up
       with anything in particular), since sitting exactly cell-centered
       read as very slightly low for this specific label. */
    int extra_lift = (int)lroundf(SECTION_LABEL_EXTRA_LIFT_PX * zoom);
    int label_top = cell_top_sy - cell_vcenter_offset(font, scale, cell) - extra_lift;
    *out_label = (SDL_Rect){ sx1 - stw, label_top, stw, sth };

    int lock_size = (int)lroundf(SECTION_LOCK_ICON_PX * zoom);
    int lock_right = out_label->x - (int)lroundf(gap * 0.5f);
    *out_lock = (SDL_Rect){ lock_right - lock_size, label_top + (sth - lock_size) / 2, lock_size, lock_size };
    return 1;
}

void section_corner_screen_pos(const Camera *cam, const Section *s, int corner, int *out_x, int *out_y) {
    int gx = (corner == 0 || corner == 2) ? s->x0 : s->x1; /* 0=TL, 1=TR, 2=BL, 3=BR */
    int gy = (corner == 0 || corner == 1) ? s->y0 : s->y1;
    camera_grid_to_screen(cam, gx, gy, out_x, out_y);
}

/* Screen-space margin (at zoom 1.0) around a section's own rectangle that
   still counts as "near" for showing its lock icon - see
   section_lock_icon_visible. */
#define SECTION_LOCK_HOVER_RADIUS_PX 16.0f
/* Below this cell size the icon would render as a near-illegible speck AND
   be genuinely hard to land a click on - hidden entirely rather than either,
   same "just stop offering it" reasoning PIN_LABEL_MIN_CELL_PX uses for pin
   labels. Deliberately well above that 9.0f: a resize handle or lock toggle
   needs more room to stay comfortably clickable than a label just needs to
   stay legible. */
#define SECTION_LOCK_MIN_CELL_PX 14.0f

/* Whether a section's lock icon should be shown/interactive at all right
   now - hidden by default, and only revealed by exactly two triggers: the
   cursor sits within a small circle around the icon's own center, or the
   cursor is hovering the section's label text (which always has its own
   hitbox, regardless of the icon's visibility). A lock toggle sitting
   permanently in view next to every section's label was visual noise most
   of the time it wasn't actually being used - and merely being somewhere
   over the section's body/border used to count too, which revealed it far
   more often than intended. Also hidden below SECTION_LOCK_MIN_CELL_PX
   regardless of hover, once zoomed out far enough that it wouldn't render
   cleanly or be easy to hit anyway. Shared by rendering and
   input_handler.c's click hit-testing so a click is never accepted on
   something that isn't actually being drawn (or vice versa). */
int section_lock_icon_visible(TTF_Font *font, const Camera *cam, const Section *s, int hover_x, int hover_y) {
    float cell = camera_cell_px(cam);
    if (cell < SECTION_LOCK_MIN_CELL_PX) return 0;

    SDL_Rect label_rect, lock_rect;
    if (!section_label_bounds(font, cam, s, &label_rect, &lock_rect)) return 0;
    if (hover_x >= label_rect.x && hover_x < label_rect.x + label_rect.w &&
        hover_y >= label_rect.y && hover_y < label_rect.y + label_rect.h) {
        return 1;
    }

    float lock_cx = lock_rect.x + lock_rect.w * 0.5f;
    float lock_cy = lock_rect.y + lock_rect.h * 0.5f;
    float radius = SECTION_LOCK_HOVER_RADIUS_PX * zoom_factor(cell);
    float dx = hover_x - lock_cx, dy = hover_y - lock_cy;
    return (dx * dx + dy * dy) <= radius * radius;
}

/* Shared by render_sections/render_section_preview - draws just the
   rectangle outline (no label/lock/handles, those differ enough between a
   committed section and one still being typed for the first time that each
   caller draws them itself). */
static void draw_section_rect(SDL_Renderer *renderer, const Camera *cam, int x0, int y0, int x1, int y1, SDL_Color col) {
    float cell = camera_cell_px(cam);
    float thickness = wire_thickness_px(cell);
    int sx0, sy0, sx1, sy1;
    camera_grid_to_screen(cam, x0, y0, &sx0, &sy0);
    camera_grid_to_screen(cam, x1, y1, &sx1, &sy1);
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
    draw_thick_line(renderer, sx0, sy0, sx1, sy0, thickness);
    draw_thick_line(renderer, sx1, sy0, sx1, sy1, thickness);
    draw_thick_line(renderer, sx1, sy1, sx0, sy1, thickness);
    draw_thick_line(renderer, sx0, sy1, sx0, sy0, thickness);
}

void render_sections(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam, const Circuit *circuit,
                      int editing_id, const char *editing_text, int hover_x, int hover_y) {
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);

    for (int i = 0; i < circuit->section_high_water; i++) {
        const Section *s = &circuit->sections[i];
        if (!s->in_use) continue;
        int being_edited = (i == editing_id);

        SDL_Color rect_col = s->selected ? SELECTION_COLOR : SECTION_COLOR;
        draw_section_rect(renderer, cam, s->x0, s->y0, s->x1, s->y1, rect_col);

        SDL_Rect label_rect, lock_rect;
        if (section_label_bounds(font, cam, s, &label_rect, &lock_rect)) {
            const char *shown = being_edited ? editing_text : s->label;
            /* white, same as every other on-canvas label (LABEL_COLOR) - not
               SECTION_COLOR, which is for the rectangle/lock icon's own
               "chrome" gray, not for text meant to actually be read. */
            SDL_Color text_col = s->selected ? SELECTION_COLOR : LABEL_COLOR;
            text_util_draw_scaled(renderer, font, shown, label_rect.x, label_rect.y, text_col, scale);
            if (being_edited) draw_canvas_text_cursor(renderer, font, editing_text, label_rect.x, label_rect.y, scale, text_col);

            if (section_lock_icon_visible(font, cam, s, hover_x, hover_y)) {
                int lock_hovered = (hover_x >= lock_rect.x && hover_x < lock_rect.x + lock_rect.w &&
                                     hover_y >= lock_rect.y && hover_y < lock_rect.y + lock_rect.h);
                SDL_Color lock_col = lock_hovered ? SELECTION_COLOR : SECTION_COLOR;
                draw_lock_icon(renderer, &lock_rect, s->locked, lock_col);
            }
        }

        if (s->selected && !s->locked) {
            /* round, same shape/color family as a selected wire's own
               endpoint dots (SELECTION_DOT_COLOR) - not a square, so a
               resize handle reads as "grab this point" the same way every
               other connection point in the app already does. */
            float handle_r = (SECTION_HANDLE_PX * 0.5f) * zoom_factor(cell);
            SDL_SetRenderDrawColor(renderer, SELECTION_DOT_COLOR.r, SELECTION_DOT_COLOR.g, SELECTION_DOT_COLOR.b, 255);
            for (int corner = 0; corner < 4; corner++) {
                int hx, hy;
                section_corner_screen_pos(cam, s, corner, &hx, &hy);
                draw_filled_circle(renderer, hx, hy, handle_r);
            }
        }
    }
}

void render_section_preview(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam, int x0, int y0, int x1, int y1,
                             const char *editing_text, int ghost) {
    draw_section_rect(renderer, cam, x0, y0, x1, y1, ghost ? SELECTION_COLOR : SECTION_COLOR);
    if (font == NULL) return;
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    int tw, th;
    text_util_measure(font, editing_text, &tw, &th);
    (void)th;
    int stw = (int)lroundf(tw * scale);
    /* same grid-cell-centered vertical placement as a committed section's
       own label (section_label_bounds) - the cell directly above the
       section's top edge, not a fixed pixel gap. */
    int sx1, cell_top_sy;
    camera_grid_to_screen(cam, x1, y0 - 1, &sx1, &cell_top_sy);
    int label_x = sx1 - stw;
    int extra_lift = (int)lroundf(SECTION_LABEL_EXTRA_LIFT_PX * zoom_factor(cell));
    int label_y = cell_top_sy - cell_vcenter_offset(font, scale, cell) - extra_lift;
    SDL_Color text_col = ghost ? SELECTION_COLOR : LABEL_COLOR;
    text_util_draw_scaled(renderer, font, editing_text, label_x, label_y, text_col, scale);
    /* a ghost isn't actively being typed into - no blinking caret, unlike
       the in-progress-drawing preview this function also renders */
    if (!ghost) draw_canvas_text_cursor(renderer, font, editing_text, label_x, label_y, scale, text_col);
}

int text_label_bounds(TTF_Font *font, const Camera *cam, const TextLabel *t, SDL_Rect *out) {
    if (font == NULL) return 0;
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    int tw, th;
    text_util_measure(font, (t->text[0] != '\0') ? t->text : " ", &tw, &th);
    int sx, sy;
    camera_grid_to_screen(cam, t->x, t->y, &sx, &sy);
    *out = (SDL_Rect){ sx, sy - cell_vcenter_offset(font, scale, cell), (int)lroundf(tw * scale), (int)lroundf(th * scale) };
    return 1;
}

void render_text_labels(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam, const Circuit *circuit,
                         int editing_id, const char *editing_text, int hover_x, int hover_y) {
    (void)hover_x; (void)hover_y; /* no hover-only affordance on a plain text label, unlike a section's lock icon */
    float scale = label_scale(camera_cell_px(cam));
    for (int i = 0; i < circuit->text_label_high_water; i++) {
        const TextLabel *t = &circuit->text_labels[i];
        if (!t->in_use) continue;
        int being_edited = (i == editing_id);
        SDL_Rect bounds;
        if (!text_label_bounds(font, cam, t, &bounds)) continue;
        const char *shown = being_edited ? editing_text : t->text;
        SDL_Color col = t->selected ? SELECTION_COLOR : LABEL_COLOR;
        text_util_draw_scaled(renderer, font, shown, bounds.x, bounds.y, col, scale);
        if (being_edited) draw_canvas_text_cursor(renderer, font, editing_text, bounds.x, bounds.y, scale, col);
    }
}

void render_text_label_preview(SDL_Renderer *renderer, TTF_Font *font, const Camera *cam, int x, int y,
                                const char *editing_text, int ghost) {
    if (font == NULL) return;
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    int sx, sy;
    camera_grid_to_screen(cam, x, y, &sx, &sy);
    sy -= cell_vcenter_offset(font, scale, cell);
    SDL_Color color = ghost ? SELECTION_COLOR : LABEL_COLOR;
    text_util_draw_scaled(renderer, font, editing_text, sx, sy, color, scale);
    /* a ghost isn't actively being typed into - no blinking caret, unlike
       the in-progress-typing preview this function also renders */
    if (!ghost) draw_canvas_text_cursor(renderer, font, editing_text, sx, sy, scale, color);
}

/* Every wire-drawing pass in render_circuit below iterates wires in THIS
   order rather than plain array index - layer_order[0] is the topmost layer
   visually (see circuit.h's own comment on layer_order), so its wires must
   be the LAST ones drawn, painting over anything from a lower layer
   wherever two wires happen to cross on screen, the same way a real board's
   copper layers physically stack. Built once per frame and reused by every
   pass (line, terminal, dots, endpoint marks) so they all agree on the same
   stacking instead of only the line itself respecting it. */
static int build_wire_draw_order(const Circuit *circuit, int *out_order, int max_out) {
    int n = 0;
    for (int pos = circuit->layer_order_count - 1; pos >= 0 && n < max_out; pos--) {
        int slot = circuit->layer_order[pos];
        for (int i = 0; i < circuit->wire_high_water && n < max_out; i++) {
            if (!circuit->wires[i].in_use || circuit->wires[i].layer_slot != slot) continue;
            out_order[n++] = i;
        }
    }
    return n;
}

void render_circuit(SDL_Renderer *renderer, TTF_Font *font_large, const Circuit *circuit, const Camera *cam,
                     const DiagnosticSet *diagnostics, int layer_preview,
                     int highlight_component_a, int highlight_wire_a,
                     int highlight_component_b, int highlight_wire_b) {
    int wire_order[MAX_WIRES];
    int wire_order_n = build_wire_draw_order(circuit, wire_order, MAX_WIRES);

    /* pass 1: lines, terminals and component bodies */
    for (int oi = 0; oi < wire_order_n; oi++) {
        int i = wire_order[oi];
        const Wire *w = &circuit->wires[i];
        int highlighted = (i == highlight_wire_a || i == highlight_wire_b);
        render_wire_line(renderer, cam, circuit, w, i, circuit->wire_values[i], highlighted, diagnostics, layer_preview);
    }
    for (int oi = 0; oi < wire_order_n; oi++) {
        const Wire *w = &circuit->wires[wire_order[oi]];
        render_wire_terminal(renderer, font_large, cam, w, circuit->wire_values[wire_order[oi]]);
    }
    for (int i = 0; i < circuit->component_high_water; i++) {
        const Component *c = &circuit->components[i];
        if (!c->in_use || c->type != COMP_IC) continue;
        int highlighted = (i == highlight_component_a || i == highlight_component_b);
        render_ic_body(renderer, font_large, cam, c, highlighted, 1, 0, 1); /* ghost=0 - ghost_valid unused */
    }

    /* pass 2: connection dots on top, so no line/stub can slice through one -
       same layer-stacked order as pass 1, so a dot from a topmost-layer wire
       still wins over a lower-layer wire's line/dot passing underneath it. */
    for (int oi = 0; oi < wire_order_n; oi++) {
        render_wire_dots(renderer, cam, circuit, &circuit->wires[wire_order[oi]]);
    }
    for (int i = 0; i < circuit->component_high_water; i++) {
        const Component *c = &circuit->components[i];
        if (c->in_use) render_component_pin_dots(renderer, cam, circuit, c);
    }
    render_junctions(renderer, cam, circuit);
    render_vias(renderer, cam, circuit);

    for (int oi = 0; oi < wire_order_n; oi++) {
        int i = wire_order[oi];
        const Wire *w = &circuit->wires[i];
        int highlighted = (i == highlight_wire_a || i == highlight_wire_b);
        render_wire_endpoint_marks(renderer, cam, w, highlighted);
    }
}

/* Wires flagged by a diagnostic get their color from render_wire_line
   (via render_circuit's diagnostics param) instead, so selection/hover can
   correctly take priority and the wire's own endpoint dots stay drawn on
   top - see wire_diag_color. This pass only marks flagged pins, since a
   floating/fan-out pin has no wire of its own to carry the color. */
void render_diagnostic_highlights(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit,
                                   const DiagnosticSet *diagnostics) {
    float cell = camera_cell_px(cam);
    /* same radius as an ordinary connection dot (draw_lone_connection_dot
       etc.) and the same reliance on draw_filled_circle's own 1px floor, so
       this scales identically with zoom instead of standing out as a fixed
       minimum size once the normal dots shrink past it */
    float dot_r = connection_dot_radius_px(cell);

    /* two passes so DIAG_ERROR always paints over DIAG_WARNING on any
       pin flagged by both, instead of draw-order picking the color */
    for (int pass = 0; pass < 2; pass++) {
        DiagSeverity want = (pass == 0) ? DIAG_WARNING : DIAG_ERROR;
        for (int di = 0; di < diagnostics->count; di++) {
            const Diagnostic *d = &diagnostics->items[di];
            if (d->severity != want) continue;
            SDL_Color col = (d->severity == DIAG_ERROR) ? DIAG_ERROR_COLOR : DIAG_WARNING_COLOR;
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);

            for (int pi = 0; pi < d->pin_count; pi++) {
                const Component *c = &circuit->components[d->pins[pi].component_id];
                if (!c->in_use) continue;
                int x, y;
                component_pin_world_pos(c, d->pins[pi].pin_index, &x, &y);
                int sx, sy;
                camera_grid_to_screen(cam, x, y, &sx, &sy);
                draw_filled_circle(renderer, sx, sy, dot_r);
            }
        }
    }
}

#define DIAG_CHIP_H 26
#define DIAG_CHIP_PAD_X 10
#define DIAG_CHIP_MARGIN 8

int render_diagnostics_panel(SDL_Renderer *renderer, TTF_Font *font, int window_w, int window_h,
                              const DiagnosticSet *diagnostics, int hover_x, int hover_y) {
    if (font == NULL) return -1;
    int hovered = -1;
    int x = DIAG_CHIP_MARGIN;
    int y = window_h - DIAG_CHIP_MARGIN - DIAG_CHIP_H;

    for (int i = 0; i < diagnostics->count; i++) {
        const Diagnostic *d = &diagnostics->items[i];
        SDL_Color col = (d->severity == DIAG_ERROR) ? DIAG_PANEL_ERROR_COLOR : DIAG_PANEL_WARNING_COLOR;

        int tw, th;
        text_util_measure(font, d->summary, &tw, &th);
        SDL_Rect chip = { x, y, tw + DIAG_CHIP_PAD_X * 2, DIAG_CHIP_H };

        /* stop once this chip (or the next one after it) wouldn't fully fit
           before the window's right edge, rather than spilling past it or
           running under whatever's docked there - see the header comment. */
        if (chip.x + chip.w + DIAG_CHIP_MARGIN > window_w) break;

        int is_hovered = (hover_x >= chip.x && hover_x < chip.x + chip.w &&
                           hover_y >= chip.y && hover_y < chip.y + chip.h);
        if (is_hovered) hovered = i;

        /* fully opaque - this sits below the Manage Data/Layers panels now
           (see app.c's render order), so it no longer needs to read as a
           translucent overlay above the canvas the way it did when it was
           the very last thing drawn. */
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
        SDL_RenderFillRect(renderer, &chip);
        SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
        SDL_RenderDrawRect(renderer, &chip);

        /* dark text - reads better on bright yellow/red chips than the
           usual near-white UI label color would */
        SDL_Color text_col = { 25, 25, 28, 255 };
        text_util_draw(renderer, font, d->summary, chip.x + DIAG_CHIP_PAD_X, chip.y + (DIAG_CHIP_H - th) / 2, text_col);

        x += chip.w + DIAG_CHIP_MARGIN;
    }
    return hovered;
}

#define DIAG_TOOLTIP_MAX_W 320
#define DIAG_TOOLTIP_LINE_H 18
#define DIAG_TOOLTIP_PAD 8

/* Shared by the line-count dry run and the actual draw below - walks text
   word by word, greedily packing each line up to max_w. draw_col is NULL
   for the dry run (measure only, no drawing). Returns the line count either way. */
static int wrap_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, int max_w, const SDL_Color *draw_col) {
    char line[256] = "";
    int line_y = y;
    int lines = 0;
    const char *p = text;
    while (*p != '\0') {
        const char *word_start = p;
        while (*p != '\0' && *p != ' ') p++;
        int word_len = (int)(p - word_start);
        if (*p == ' ') p++;

        char trial[256];
        if (line[0] != '\0') snprintf(trial, sizeof(trial), "%s %.*s", line, word_len, word_start);
        else snprintf(trial, sizeof(trial), "%.*s", word_len, word_start);

        int tw, th;
        text_util_measure(font, trial, &tw, &th);
        if (tw > max_w && line[0] != '\0') {
            if (draw_col != NULL) text_util_draw(renderer, font, line, x, line_y, *draw_col);
            line_y += DIAG_TOOLTIP_LINE_H;
            lines++;
            snprintf(line, sizeof(line), "%.*s", word_len, word_start);
        } else {
            snprintf(line, sizeof(line), "%s", trial);
        }
    }
    if (line[0] != '\0') {
        if (draw_col != NULL) text_util_draw(renderer, font, line, x, line_y, *draw_col);
        lines++;
    }
    return lines;
}

void render_diagnostic_tooltip(SDL_Renderer *renderer, TTF_Font *font, const Diagnostic *diag,
                                int anchor_x, int anchor_y, int window_w, int window_h) {
    if (font == NULL) return;
    int line_count = wrap_text(renderer, font, diag->detail, 0, 0, DIAG_TOOLTIP_MAX_W, NULL);
    if (line_count <= 0) return;

    int box_w = DIAG_TOOLTIP_MAX_W + DIAG_TOOLTIP_PAD * 2;
    int box_h = line_count * DIAG_TOOLTIP_LINE_H + DIAG_TOOLTIP_PAD * 2;

    int box_x = anchor_x;
    int box_y = anchor_y - box_h - 6; /* default: just above the anchor */
    if (box_y < 0) box_y = anchor_y + 24;       /* not enough room above - flip below instead */
    if (box_x + box_w > window_w) box_x = window_w - box_w - 4;
    if (box_x < 0) box_x = 4;
    if (box_y + box_h > window_h) box_y = window_h - box_h - 4;

    SDL_Rect box = { box_x, box_y, box_w, box_h };
    SDL_SetRenderDrawColor(renderer, 25, 25, 28, 240);
    SDL_RenderFillRect(renderer, &box);
    SDL_Color border = (diag->severity == DIAG_ERROR) ? DIAG_ERROR_COLOR : DIAG_WARNING_COLOR;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(renderer, &box);

    SDL_Color text_col = { 230, 230, 235, 255 };
    wrap_text(renderer, font, diag->detail, box_x + DIAG_TOOLTIP_PAD, box_y + DIAG_TOOLTIP_PAD, DIAG_TOOLTIP_MAX_W, &text_col);
}

void render_via_tooltip(SDL_Renderer *renderer, TTF_Font *font, const char *layer_a_name, const char *layer_b_name,
                         int anchor_x, int anchor_y, int window_w, int window_h) {
    if (font == NULL) return;
    char text[64];
    snprintf(text, sizeof(text), "%s <-> %s", layer_a_name, layer_b_name);
    int text_w, text_h;
    text_util_measure(font, text, &text_w, &text_h);
    if (text_w <= 0) return;

    int box_w = text_w + DIAG_TOOLTIP_PAD * 2;
    int box_h = text_h + DIAG_TOOLTIP_PAD * 2;

    int box_x = anchor_x;
    int box_y = anchor_y - box_h - 6; /* default: just above the anchor */
    if (box_y < 0) box_y = anchor_y + 24;       /* not enough room above - flip below instead */
    if (box_x + box_w > window_w) box_x = window_w - box_w - 4;
    if (box_x < 0) box_x = 4;
    if (box_y + box_h > window_h) box_y = window_h - box_h - 4;

    SDL_Rect box = { box_x, box_y, box_w, box_h };
    SDL_SetRenderDrawColor(renderer, 25, 25, 28, 240);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, VIA_RING_COLOR.r, VIA_RING_COLOR.g, VIA_RING_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, &box);

    SDL_Color text_col = { 230, 230, 235, 255 };
    text_util_draw(renderer, font, text, box_x + DIAG_TOOLTIP_PAD, box_y + DIAG_TOOLTIP_PAD, text_col);
}
