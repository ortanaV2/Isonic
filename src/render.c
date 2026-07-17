#include <math.h>
#include "render.h"
#include "text_util.h"

/* Falstad-style light blue, used both for persistent selection and for the
   temporary "this is what you'd connect to" highlight while dragging a wire. */
static const SDL_Color SELECTION_COLOR = { 90, 170, 255, 255 };
static const SDL_Color LABEL_COLOR = { 225, 225, 230, 255 };
static const SDL_Color OUTPUT_LABEL_COLOR = { 140, 140, 146, 255 }; /* dimmer, distinguishes Output from Input */
static const SDL_Color IC_NAME_LABEL_COLOR = { 140, 140, 146, 255 }; /* grayish body-name label shown once pins are too small to read */
/* every connection point (wire endpoints, IC pin tips, junctions) uses this
   neutral marker color - only the line/stub itself carries the signal color */
static const SDL_Color CONNECTION_COLOR = { 235, 235, 240, 255 };
static const SDL_Color IC_BORDER_COLOR = { 190, 190, 196, 255 };

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
   stretching up caused visible blur/pixelation. */
#define LABEL_FONT_POINT_SIZE 48.0f
#define LABEL_DISPLAY_POINT_SIZE 18.0f

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

/* Draws the IC body's top edge with a semicircular notch cut into its middle,
   like the orientation marking on a real DIP package - pin 1 (see
   component_init_ic) always sits just to its left. The straight edge is split
   in two around the notch instead of drawn full-width underneath it, so the
   arc reads as an actual cut rather than a bump added on top of an intact line. */
static void draw_top_edge_with_notch(SDL_Renderer *renderer, int left_x, int top_y, int body_w_px, float cell, float thickness) {
    float radius = notch_radius_px(cell);
    float cx = left_x + body_w_px * 0.5f;

    draw_thick_line(renderer, left_x, top_y, (int)lroundf(cx - radius), top_y, thickness);
    draw_thick_line(renderer, (int)lroundf(cx + radius), top_y, left_x + body_w_px, top_y, thickness);

    /* sweeps angle from PI down to 0 (through PI/2, not 3*PI/2) so sin(angle)
       stays positive and the arc dips to larger y - i.e. down into the body,
       since screen space y grows downward. Going the other way around traced
       the mirror image, poking the notch out above the body instead. */
    draw_arc_strip(renderer, cx, (float)top_y, radius, ISONIC_TAU * 0.5f, 0.0f, thickness);
}

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
    SDL_SetRenderDrawColor(renderer, SELECTION_COLOR.r, SELECTION_COLOR.g, SELECTION_COLOR.b, 255);
    draw_filled_circle(renderer, sfx, sfy, r);
    draw_filled_circle(renderer, stx, sty, r);
}

void render_placement_preview(SDL_Renderer *renderer, const Camera *cam, int gx, int gy, int w, int h, int valid) {
    int sx, sy;
    camera_grid_to_screen(cam, gx, gy, &sx, &sy);
    float cell = camera_cell_px(cam);
    SDL_Rect r = { sx, sy, (int)lroundf(w * cell), (int)lroundf(h * cell) };
    if (valid) {
        SDL_SetRenderDrawColor(renderer, 90, 160, 220, 90);
    } else {
        SDL_SetRenderDrawColor(renderer, 220, 70, 70, 90);
    }
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, 230, 230, 235, 180);
    SDL_RenderDrawRect(renderer, &r);
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

/* Line only - the endpoint dots are drawn later, in a pass over the top of
   every line/stub (see render_wire_dots), so a stub or another wire drawn
   afterwards can never slice back through an already-placed dot. */
static void render_wire_line(SDL_Renderer *renderer, const Camera *cam, const Wire *w, SignalValue value, int highlighted) {
    SDL_Color color = (w->selected || highlighted) ? SELECTION_COLOR : signal_color(value);
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
    SDL_SetRenderDrawColor(renderer, SELECTION_COLOR.r, SELECTION_COLOR.g, SELECTION_COLOR.b, 255);
    draw_filled_circle(renderer, sfx, sfy, r);
    draw_filled_circle(renderer, stx, sty, r);
}

int render_wire_terminal_bounds(TTF_Font *font_large, const Camera *cam, const Wire *w, SignalValue value, SDL_Rect *out_rect) {
    if (w->kind == WIRE_KIND_NORMAL || font_large == NULL) return 0;
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
    int gap = (int)lroundf(10 * scale);

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

    const char *text = (value == SIG_HIGH) ? "H" : "L";
    SDL_Color label_color = (w->kind == WIRE_KIND_INPUT) ? LABEL_COLOR : OUTPUT_LABEL_COLOR;
    float scale = label_scale(camera_cell_px(cam));
    text_util_draw_scaled(renderer, font_large, text, bounds.x, bounds.y, label_color, scale);
}

/* Below this cell size, individual pin labels turn into unreadable clutter
   - the body switches to one big centered IC name instead (see
   render_ic_body). Above it, pin names are shown as usual. */
#define PIN_LABEL_MIN_CELL_PX 9.0f

/* Schematic-symbol style IC: fixed-width rectangle body sized by pin count
   (see ic_dip_body_size), a real-DIP-style pin-1 notch on the top edge, pins
   drawn as short stubs poking out past the edge with a dot at the tip, and
   pin names labeled inside the body next to the stub - not a physical
   DIP/PCB footprint. Pin dots are NOT drawn here - see
   render_component_pin_dots, called later in a dedicated top layer so a stub
   line can never slice back through a dot. */
static void render_ic_body(SDL_Renderer *renderer, TTF_Font *font_large, const Camera *cam, const Component *c, int highlighted) {
    const IC_Def *def = c->ic_def;
    int body_w_cells, body_h_cells;
    ic_dip_body_size(def->pin_count, &body_w_cells, &body_h_cells);

    int sx, sy;
    camera_grid_to_screen(cam, c->grid_x, c->grid_y, &sx, &sy);
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    int w = (int)lroundf(body_w_cells * cell);
    int h = (int)lroundf(body_h_cells * cell);
    float thickness = wire_thickness_px(cell);

    /* pin edge anchors (screen-space) are computed once and reused by both the
       stub pass and the label pass below, instead of recomputing per pin twice */
    int is_left[MAX_PINS_PER_COMPONENT];
    int edge_sx[MAX_PINS_PER_COMPONENT], edge_sy[MAX_PINS_PER_COMPONENT];
    for (int pi = 0; pi < c->pin_count; pi++) {
        is_left[pi] = (c->pins[pi].local_dx < 0);
        int edge_x = is_left[pi] ? c->grid_x : c->grid_x + body_w_cells;
        int edge_y = c->grid_y + c->pins[pi].local_dy;
        camera_grid_to_screen(cam, edge_x, edge_y, &edge_sx[pi], &edge_sy[pi]);
    }

    /* stubs are drawn before the border (not after) so the border - opaque,
       drawn on top - cleanly caps off the seam where a stub's round end
       would otherwise bleed a little past the edge into the body interior */
    for (int pi = 0; pi < c->pin_count; pi++) {
        const Pin *p = &c->pins[pi];
        int tip_x, tip_y;
        component_pin_world_pos(c, pi, &tip_x, &tip_y);
        int stx, sty;
        camera_grid_to_screen(cam, tip_x, tip_y, &stx, &sty);

        SDL_Color col = signal_color(p->value);
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
        draw_thick_line(renderer, edge_sx[pi], edge_sy[pi], stx, sty, thickness);
    }

    SDL_Color border = (c->selected || highlighted) ? SELECTION_COLOR : IC_BORDER_COLOR;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    draw_thick_line(renderer, sx, sy + h, sx + w, sy + h, thickness); /* bottom */
    draw_thick_line(renderer, sx, sy, sx, sy + h, thickness);         /* left */
    draw_thick_line(renderer, sx + w, sy, sx + w, sy + h, thickness); /* right */
    draw_top_edge_with_notch(renderer, sx, sy, w, cell, thickness);

    if (font_large != NULL && cell >= PIN_LABEL_MIN_CELL_PX) {
        for (int pi = 0; pi < c->pin_count; pi++) {
            const Pin *p = &c->pins[pi];
            int tw, th;
            text_util_measure(font_large, p->name, &tw, &th);
            int stw = (int)lroundf(tw * scale);
            int sth = (int)lroundf(th * scale);
            int label_x = is_left[pi] ? edge_sx[pi] + (int)lroundf(10 * scale) : edge_sx[pi] - (int)lroundf(10 * scale) - stw;
            text_util_draw_scaled(renderer, font_large, p->name, label_x, edge_sy[pi] - sth / 2, LABEL_COLOR, scale);
        }
    } else if (font_large != NULL) {
        /* pins are too small to label individually - show one big name
           instead, running along the body's long (vertical) axis since the
           DIP body is always narrow (see ic_dip_body_size). No lower cell
           bound here - it should keep shrinking along with everything else
           instead of disappearing once zoomed out past some fixed cutoff. */
        int tw, th;
        text_util_measure(font_large, def->name, &tw, &th);
        if (tw > 0 && th > 0) {
            float fit_by_length = (h * 0.6f) / tw;    /* text width becomes the vertical extent once rotated */
            float fit_by_thickness = (w * 0.6f) / th; /* text height becomes the horizontal extent once rotated */
            float name_scale = fit_by_length < fit_by_thickness ? fit_by_length : fit_by_thickness;
            if (name_scale > 0.0f) {
                text_util_draw_scaled_rotated(renderer, font_large, def->name, sx + w / 2, sy + h / 2,
                                               IC_NAME_LABEL_COLOR, name_scale, -90.0f);
            }
        }
    }
}

static void render_component_pin_dots(SDL_Renderer *renderer, const Camera *cam, const Circuit *circuit, const Component *c) {
    for (int pi = 0; pi < c->pin_count; pi++) {
        int tip_x, tip_y;
        component_pin_world_pos(c, pi, &tip_x, &tip_y);
        draw_lone_connection_dot(renderer, cam, circuit, tip_x, tip_y);
    }
}

void render_circuit(SDL_Renderer *renderer, TTF_Font *font_large, const Circuit *circuit, const Camera *cam,
                     int highlight_component_a, int highlight_wire_a,
                     int highlight_component_b, int highlight_wire_b) {
    /* pass 1: lines, terminals and component bodies */
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        int highlighted = (i == highlight_wire_a || i == highlight_wire_b);
        render_wire_line(renderer, cam, w, circuit->wire_values[i], highlighted);
    }
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        render_wire_terminal(renderer, font_large, cam, w, circuit->wire_values[i]);
    }
    for (int i = 0; i < circuit->component_high_water; i++) {
        const Component *c = &circuit->components[i];
        if (!c->in_use || c->type != COMP_IC) continue;
        int highlighted = (i == highlight_component_a || i == highlight_component_b);
        render_ic_body(renderer, font_large, cam, c, highlighted);
    }

    /* pass 2: connection dots on top, so no line/stub can slice through one */
    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        render_wire_dots(renderer, cam, circuit, w);
    }
    for (int i = 0; i < circuit->component_high_water; i++) {
        const Component *c = &circuit->components[i];
        if (c->in_use) render_component_pin_dots(renderer, cam, circuit, c);
    }
    render_junctions(renderer, cam, circuit);

    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        int highlighted = (i == highlight_wire_a || i == highlight_wire_b);
        render_wire_endpoint_marks(renderer, cam, w, highlighted);
    }
}
