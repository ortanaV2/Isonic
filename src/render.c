#include <math.h>
#include "render.h"
#include "text_util.h"

/* Falstad-style light blue, used both for persistent selection and for the
   temporary "this is what you'd connect to" highlight while dragging a wire. */
static const SDL_Color SELECTION_COLOR = { 90, 170, 255, 255 };
static const SDL_Color LABEL_COLOR = { 225, 225, 230, 255 };
static const SDL_Color OUTPUT_LABEL_COLOR = { 140, 140, 146, 255 }; /* dimmer, distinguishes Output from Input */
/* every connection point (wire endpoints, IC pin tips, junctions) uses this
   neutral marker color - only the line/stub itself carries the signal color */
static const SDL_Color CONNECTION_COLOR = { 235, 235, 240, 255 };
static const SDL_Color IC_BORDER_COLOR = { 190, 190, 196, 255 };

/* Thickness as a fraction of the current grid cell size, not a flat pixel
   count, so wires/stubs/borders stay proportionally the same thickness
   relative to the drawn content at any zoom level (matches ~3px at zoom 1.0). */
#define WIRE_THICKNESS_CELL_FRACTION 0.15f

static int wire_thickness_px(float cell) {
    int t = (int)lroundf(cell * WIRE_THICKNESS_CELL_FRACTION);
    return t < 1 ? 1 : t;
}

#define CONNECTION_DOT_CELL_FRACTION 0.12f

static int connection_dot_radius_px(float cell) {
    int r = (int)lroundf(cell * CONNECTION_DOT_CELL_FRACTION);
    return r < 1 ? 1 : r;
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
    if (radius < 1.0f) radius = 1.0f;

    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
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
   wire-to-stub joints - blend smoothly instead of leaving a flat-edged notch. */
static void draw_thick_line(SDL_Renderer *renderer, int x0, int y0, int x1, int y1, int thickness) {
    if (thickness <= 1) {
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
        return;
    }
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = thickness * 0.5f;

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
       that mismatch was making the cap visibly poke out sideways at some
       zoom levels, whenever the integer thickness rounded up unevenly */
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    draw_filled_circle(renderer, x0, y0, hw);
    draw_filled_circle(renderer, x1, y1, hw);
}

static void draw_thick_rect(SDL_Renderer *renderer, SDL_Rect r, int thickness) {
    draw_thick_line(renderer, r.x, r.y, r.x + r.w, r.y, thickness);
    draw_thick_line(renderer, r.x, r.y + r.h, r.x + r.w, r.y + r.h, thickness);
    draw_thick_line(renderer, r.x, r.y, r.x, r.y + r.h, thickness);
    draw_thick_line(renderer, r.x + r.w, r.y, r.x + r.w, r.y + r.h, thickness);
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
    int sfx, sfy, stx, sty;
    camera_grid_to_screen(cam, fx, fy, &sfx, &sfy);
    camera_grid_to_screen(cam, tx, ty, &stx, &sty);

    float cell = camera_cell_px(cam);
    SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
    draw_thick_line(renderer, sfx, sfy, stx, sty, wire_thickness_px(cell));

    int r = connection_dot_radius_px(cell);
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
    int r = connection_dot_radius_px(camera_cell_px(cam));
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
    int r = connection_dot_radius_px(camera_cell_px(cam));
    SDL_SetRenderDrawColor(renderer, CONNECTION_COLOR.r, CONNECTION_COLOR.g, CONNECTION_COLOR.b, 255);
    for (int i = 0; i < circuit->junction_count; i++) {
        int sx, sy;
        camera_grid_to_screen(cam, circuit->junctions[i].x, circuit->junctions[i].y, &sx, &sy);
        draw_filled_circle(renderer, sx, sy, r);
    }
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

/* Schematic-symbol style IC: plain rectangle body, pins drawn as short stubs
   poking out past the edge with a dot at the tip, and pin names labeled
   inside the body next to the stub - not a physical DIP/PCB footprint.
   Pin dots are NOT drawn here - see render_component_pin_dots, called later
   in a dedicated top layer so a stub line can never slice back through a dot. */
static void render_ic_body(SDL_Renderer *renderer, TTF_Font *font_large, const Camera *cam, const Component *c, int highlighted) {
    const IC_Def *def = c->ic_def;
    int sx, sy;
    camera_grid_to_screen(cam, c->grid_x, c->grid_y, &sx, &sy);
    float cell = camera_cell_px(cam);
    float scale = label_scale(cell);
    int w = (int)lroundf(def->width * cell);
    int h = (int)lroundf(def->height * cell);
    int thickness = wire_thickness_px(cell);

    /* pin edge anchors (screen-space) are computed once and reused by both the
       stub pass and the label pass below, instead of recomputing per pin twice */
    int is_left[MAX_PINS_PER_COMPONENT];
    int edge_sx[MAX_PINS_PER_COMPONENT], edge_sy[MAX_PINS_PER_COMPONENT];
    for (int pi = 0; pi < c->pin_count; pi++) {
        is_left[pi] = (c->pins[pi].local_dx < 0);
        int edge_x = is_left[pi] ? c->grid_x : c->grid_x + def->width;
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

    SDL_Rect body = { sx, sy, w, h };
    SDL_Color border = (c->selected || highlighted) ? SELECTION_COLOR : IC_BORDER_COLOR;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    draw_thick_rect(renderer, body, thickness);

    if (font_large != NULL && cell >= 6.0f) {
        for (int pi = 0; pi < c->pin_count; pi++) {
            const Pin *p = &c->pins[pi];
            int tw, th;
            text_util_measure(font_large, p->name, &tw, &th);
            int stw = (int)lroundf(tw * scale);
            int sth = (int)lroundf(th * scale);
            int label_x = is_left[pi] ? edge_sx[pi] + (int)lroundf(6 * scale) : edge_sx[pi] - (int)lroundf(6 * scale) - stw;
            text_util_draw_scaled(renderer, font_large, p->name, label_x, edge_sy[pi] - sth / 2, LABEL_COLOR, scale);
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
}
