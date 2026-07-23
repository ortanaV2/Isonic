#include <math.h>
#include <stdio.h>
#include <string.h>
#include "taskbar.h"

static const char *k_labels[TOOL_COUNT] = {
    "Select",
    "Wire",
    "Via",
    "Input",
    "Output",
    "Components",
    "Section-Labeling",
    "Text Label",
};

static const char *k_file_menu_labels[FILE_MENU_COUNT] = {
    "New Schematic",
    "New Window",
    "Open File...",
    "Import Schematic...",
    "Save",
    "Save As...",
    "Close Window",
};

static const char *k_category_labels[MENU_CAT_COUNT] = {
    "Logic Gates",
    "Multiplexers",
    "Demultiplexers",
    "Buffers",
    "Latches",
    "Counters",
    "Timers",
    "Memory",
    "Arithmetic",
    "Obsolete",
};

typedef struct {
    MenuCategory category;
    const char *label;
    const char *ic_name; /* IC registry key, see src/ics/ */
} MenuItem;

/* The full component catalog, Falstad-style categorized. Add a new IC here
   (and bump MENU_MAX_ITEMS in taskbar.h if this ever grows past it) -
   nothing else about the menu needs to change, it lays itself out from this
   table. */
static const MenuItem k_menu_items[] = {
    { MENU_CAT_LOGIC_GATES, "AND (SN74HC08N)",  "SN74HC08N" },
    { MENU_CAT_LOGIC_GATES, "OR (SN74HC32N)",   "SN74HC32N" },
    { MENU_CAT_LOGIC_GATES, "NOT (SN74HC540N)", "SN74HC540N" },
    { MENU_CAT_LOGIC_GATES, "NAND (SN74HC00N)", "SN74HC00N" },
    { MENU_CAT_LOGIC_GATES, "NOR (SN74HC02N)",  "SN74HC02N" },
    { MENU_CAT_LOGIC_GATES, "XOR (SN74HC86N)",  "SN74HC86N" },
    { MENU_CAT_MULTIPLEXERS, "8:1 MUX (SN74HC151N)",       "SN74HC151N" },
    { MENU_CAT_MULTIPLEXERS, "Dual 4:1 MUX (SN74HC153N)",  "SN74HC153N" },
    { MENU_CAT_DEMULTIPLEXERS, "1:8 DEMUX (CD74HCT238E)",     "CD74HCT238E" },
    { MENU_CAT_DEMULTIPLEXERS, "Dual 1:4 DEMUX (CD4555BE)",   "CD4555BE" },
    { MENU_CAT_BUFFERS, "Tri-State Buffer (SN74HC244N)", "SN74HC244N" },
    { MENU_CAT_LATCHES, "D-Latch (TC74HC373APF)",        "TC74HC373APF" },
    { MENU_CAT_COUNTERS, "12-Bit Binary Counter (SN74HC4040N)", "SN74HC4040N" },
    { MENU_CAT_TIMERS, "Timer (TLC555)", "TLC555" },
    { MENU_CAT_MEMORY, "8K x 8 EEPROM (AT28C64B-15PU)", "AT28C64B-15PU" },
    { MENU_CAT_ARITHMETIC, "4-Bit Adder (CD74HC283E)", "CD74HC283E" },
    { MENU_CAT_OBSOLETE, "NOT (CD74HC04E)", "CD74HC04E" },
};
#define MENU_ITEM_COUNT ((int)(sizeof(k_menu_items) / sizeof(k_menu_items[0])))

#define BUTTON_PADDING_X 14
#define BUTTON_MARGIN 6
#define MENU_ROW_H 26
#define MENU_ROW_INDENT 16
#define MENU_PANEL_W 300
/* Narrower than the Components dropdown - a flat 6-item action list doesn't
   need the extra width that panel's longer IC labels do. */
#define FILE_MENU_PANEL_W 200

/* Extra breathing room between the File/Settings group and the tool group,
   on top of the usual BUTTON_MARGIN each side already gets - wide enough
   that the divider line drawn through its middle reads as a real separator
   rather than a slightly uneven button gap. */
#define GROUP_GAP 18

/* Basic-tool button icons (Select/Wire/Input/Output - the Components trigger
   has no single-glyph metaphor, so it stays text-only). Hand-drawn vector
   shapes via SDL_RenderGeometry, same all-vector philosophy as render.c's
   circuit rendering - no image assets anywhere in this app. */
#define ICON_BOX 18
#define ICON_CIRCLE_SEGMENTS 12

/* Fan-triangulates pts[0..count-1] from pts[0] - valid for any shape that's
   star-shaped with respect to its first vertex (every icon shape below was
   authored with that in mind), same technique render.c's draw_filled_circle
   uses fanned from a center point instead of a boundary point. */
static void icon_fill_fan(SDL_Renderer *renderer, const SDL_FPoint *pts, int count, SDL_Color col) {
    if (count < 3 || count > 16) return;
    SDL_Vertex verts[16];
    for (int i = 0; i < count; i++) {
        verts[i].position = pts[i];
        verts[i].color = col;
        verts[i].tex_coord = (SDL_FPoint){ 0, 0 };
    }
    int indices[(16 - 2) * 3];
    int ic = 0;
    for (int i = 1; i < count - 1; i++) {
        indices[ic++] = 0;
        indices[ic++] = i;
        indices[ic++] = i + 1;
    }
    SDL_RenderGeometry(renderer, NULL, verts, count, indices, ic);
}

static void icon_fill_circle(SDL_Renderer *renderer, float cx, float cy, float radius, SDL_Color col) {
    SDL_Vertex verts[ICON_CIRCLE_SEGMENTS + 1];
    verts[0].position = (SDL_FPoint){ cx, cy };
    verts[0].color = col;
    verts[0].tex_coord = (SDL_FPoint){ 0, 0 };
    for (int i = 0; i < ICON_CIRCLE_SEGMENTS; i++) {
        float a = (float)i / ICON_CIRCLE_SEGMENTS * 6.28318530718f;
        verts[i + 1].position = (SDL_FPoint){ cx + cosf(a) * radius, cy + sinf(a) * radius };
        verts[i + 1].color = col;
        verts[i + 1].tex_coord = (SDL_FPoint){ 0, 0 };
    }
    int indices[ICON_CIRCLE_SEGMENTS * 3];
    for (int i = 0; i < ICON_CIRCLE_SEGMENTS; i++) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = (i + 1) % ICON_CIRCLE_SEGMENTS + 1;
    }
    SDL_RenderGeometry(renderer, NULL, verts, ICON_CIRCLE_SEGMENTS + 1, indices, ICON_CIRCLE_SEGMENTS * 3);
}

static void icon_fill_thick_line(SDL_Renderer *renderer, float x0, float y0, float x1, float y1, float thickness, SDL_Color col) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = thickness * 0.5f;
    SDL_FPoint pts[4] = {
        { x0 + nx * hw, y0 + ny * hw },
        { x1 + nx * hw, y1 + ny * hw },
        { x1 - nx * hw, y1 - ny * hw },
        { x0 - nx * hw, y0 - ny * hw },
    };
    icon_fill_fan(renderer, pts, 4, col);
}

/* Maps a normalized (0..1, 0..1) point onto an icon's actual on-screen box. */
static SDL_FPoint icon_pt(const SDL_Rect *box, float nx, float ny) {
    SDL_FPoint p = { box->x + nx * box->w, box->y + ny * box->h };
    return p;
}

/* Select: the original cursor silhouette, restored as-is - every later
   "fix" attempt drifted further from this (the only version that actually
   read as a cursor) instead of correcting it, and made things worse each
   time. */
static void draw_select_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    /* x-coordinates shifted +0.11 from the original so the shape's bounding
       box (originally x in [0.08,0.70], center 0.39) sits centered at 0.5 -
       the original was never actually centered in its own icon box, which
       is why it looked shifted left. */
    SDL_FPoint pts[7] = {
        icon_pt(box, 0.19f, 0.05f),
        icon_pt(box, 0.19f, 0.78f),
        icon_pt(box, 0.41f, 0.60f),
        icon_pt(box, 0.56f, 0.92f),
        icon_pt(box, 0.69f, 0.85f),
        icon_pt(box, 0.51f, 0.53f),
        icon_pt(box, 0.81f, 0.53f),
    };
    icon_fill_fan(renderer, pts, 7, col);
}

/* Wire: two connection dots joined by a right-angle elbow (not a straight
   line) - matches how wires actually route on the canvas, and the same
   endpoint-dot visual language the canvas already uses. Thinner than the
   first attempt, which read as a thick solid block rather than a wire. */
static void draw_wire_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint p0 = icon_pt(box, 0.14f, 0.22f);
    SDL_FPoint p1 = icon_pt(box, 0.55f, 0.22f);
    SDL_FPoint p2 = icon_pt(box, 0.55f, 0.62f);
    SDL_FPoint p3 = icon_pt(box, 0.86f, 0.62f);
    float thick = box->h * 0.09f;
    icon_fill_thick_line(renderer, p0.x, p0.y, p1.x, p1.y, thick, col);
    icon_fill_thick_line(renderer, p1.x, p1.y, p2.x, p2.y, thick, col);
    icon_fill_thick_line(renderer, p2.x, p2.y, p3.x, p3.y, thick, col);
    icon_fill_circle(renderer, p1.x, p1.y, thick * 0.5f, col); /* clean bend joints */
    icon_fill_circle(renderer, p2.x, p2.y, thick * 0.5f, col);
    icon_fill_circle(renderer, p0.x, p0.y, box->w * 0.13f, col); /* endpoint dots */
    icon_fill_circle(renderer, p3.x, p3.y, box->w * 0.13f, col);
}

/* Via: a plain ring - same annulus silhouette render_vias uses on the
   canvas, drawn as a closed loop of thick line segments (rather than a
   filled disc with a punched hole) so it doesn't need to know the button's
   current background color to look right whether hovered/active or not. */
static void draw_via_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint center = icon_pt(box, 0.5f, 0.5f);
    float radius = box->w * 0.32f;
    float thick = box->w * 0.16f;
    const int segs = ICON_CIRCLE_SEGMENTS;
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 6.28318530718f;
        float a1 = (float)(i + 1) / segs * 6.28318530718f;
        icon_fill_thick_line(renderer, center.x + cosf(a0) * radius, center.y + sinf(a0) * radius,
                              center.x + cosf(a1) * radius, center.y + sinf(a1) * radius, thick, col);
    }
}

/* Shared "door" for Input/Output - the classic log-in/log-out pictogram: an
   open bracket (no left edge) on the right side of the icon. Only the arrow
   differs between the two icons; the door itself is identical. */
static void draw_terminal_door(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint top_a = icon_pt(box, 0.50f, 0.15f);
    SDL_FPoint top_b = icon_pt(box, 0.85f, 0.15f);
    SDL_FPoint bot_a = icon_pt(box, 0.85f, 0.85f);
    SDL_FPoint bot_b = icon_pt(box, 0.50f, 0.85f);
    float thick = box->h * 0.12f;
    icon_fill_thick_line(renderer, top_a.x, top_a.y, top_b.x, top_b.y, thick, col);
    icon_fill_thick_line(renderer, top_b.x, top_b.y, bot_a.x, bot_a.y, thick, col);
    icon_fill_thick_line(renderer, bot_a.x, bot_a.y, bot_b.x, bot_b.y, thick, col);
    icon_fill_circle(renderer, top_b.x, top_b.y, thick * 0.5f, col); /* clean corner joints */
    icon_fill_circle(renderer, bot_a.x, bot_a.y, thick * 0.5f, col);
}

/* Input: the classic "log-in" pictogram - an arrow entering the open door
   from outside. */
static void draw_input_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    draw_terminal_door(renderer, box, col);
    SDL_FPoint s0 = icon_pt(box, 0.05f, 0.5f);
    SDL_FPoint s1 = icon_pt(box, 0.45f, 0.5f);
    icon_fill_thick_line(renderer, s0.x, s0.y, s1.x, s1.y, box->h * 0.12f, col);
    SDL_FPoint head[3] = {
        icon_pt(box, 0.45f, 0.32f),
        icon_pt(box, 0.45f, 0.68f),
        icon_pt(box, 0.65f, 0.50f),
    };
    icon_fill_fan(renderer, head, 3, col);
}

/* Output: the classic "log-out" pictogram - the same door, but the arrow
   exits it instead of entering. */
static void draw_output_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    draw_terminal_door(renderer, box, col);
    SDL_FPoint s0 = icon_pt(box, 0.65f, 0.5f);
    SDL_FPoint s1 = icon_pt(box, 0.25f, 0.5f);
    icon_fill_thick_line(renderer, s0.x, s0.y, s1.x, s1.y, box->h * 0.12f, col);
    SDL_FPoint head[3] = {
        icon_pt(box, 0.25f, 0.32f),
        icon_pt(box, 0.25f, 0.68f),
        icon_pt(box, 0.05f, 0.50f),
    };
    icon_fill_fan(renderer, head, 3, col);
}

/* File: a page with its top-right corner folded down (the universal
   "document" pictogram), plus a few text-line strokes so it doesn't read as
   a blank rectangle. Outline-only, same thick-line-plus-joint-dot technique
   as draw_wire_icon/draw_terminal_door. */
static void draw_file_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint a = icon_pt(box, 0.22f, 0.08f);
    SDL_FPoint b = icon_pt(box, 0.60f, 0.08f);
    SDL_FPoint f = icon_pt(box, 0.60f, 0.28f); /* folded-corner flap */
    SDL_FPoint c = icon_pt(box, 0.80f, 0.28f);
    SDL_FPoint d = icon_pt(box, 0.80f, 0.92f);
    SDL_FPoint e = icon_pt(box, 0.22f, 0.92f);
    float thick = box->h * 0.08f;

    icon_fill_thick_line(renderer, a.x, a.y, b.x, b.y, thick, col);
    icon_fill_thick_line(renderer, b.x, b.y, f.x, f.y, thick, col);
    icon_fill_thick_line(renderer, f.x, f.y, c.x, c.y, thick, col);
    icon_fill_thick_line(renderer, c.x, c.y, d.x, d.y, thick, col);
    icon_fill_thick_line(renderer, d.x, d.y, e.x, e.y, thick, col);
    icon_fill_thick_line(renderer, e.x, e.y, a.x, a.y, thick, col);
    SDL_FPoint joints[6] = { a, b, f, c, d, e };
    for (int i = 0; i < 6; i++) icon_fill_circle(renderer, joints[i].x, joints[i].y, thick * 0.5f, col);

    float ys[3] = { 0.48f, 0.62f, 0.76f };
    for (int i = 0; i < 3; i++) {
        SDL_FPoint s0 = icon_pt(box, 0.34f, ys[i]);
        SDL_FPoint s1 = icon_pt(box, 0.68f, ys[i]);
        icon_fill_thick_line(renderer, s0.x, s0.y, s1.x, s1.y, thick * 0.75f, col);
    }
}

/* Settings: a gear - the same ring-of-segments annulus draw_via_icon uses,
   with teeth radiating outward - the universal "configuration" pictogram. */
static void draw_settings_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint center = icon_pt(box, 0.5f, 0.5f);
    float ring_r = box->w * 0.24f;
    float ring_thick = box->w * 0.11f;
    const int segs = ICON_CIRCLE_SEGMENTS;
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 6.28318530718f;
        float a1 = (float)(i + 1) / segs * 6.28318530718f;
        icon_fill_thick_line(renderer, center.x + cosf(a0) * ring_r, center.y + sinf(a0) * ring_r,
                              center.x + cosf(a1) * ring_r, center.y + sinf(a1) * ring_r, ring_thick, col);
    }

    const int tooth_count = 8;
    /* Starts inside the ring's own band (ring_r +/- ring_thick/2), not right
       at its nominal outer edge - the ring is a coarse straight-segment
       polygon so its true edge dips slightly below that between vertices,
       which left a hairline gap before each tooth at that radius. */
    float r_in = ring_r;
    float r_out = box->w * 0.43f;
    float tooth_thick = box->w * 0.12f;
    for (int i = 0; i < tooth_count; i++) {
        float a = (float)i / tooth_count * 6.28318530718f;
        float ca = cosf(a), sa = sinf(a);
        icon_fill_thick_line(renderer, center.x + ca * r_in, center.y + sa * r_in,
                              center.x + ca * r_out, center.y + sa * r_out, tooth_thick, col);
    }
}

/* Section-Labeling: a rectangle outline with a small dot at each corner,
   hinting at the real feature's 4 draggable resize handles. */
static void draw_section_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint a = icon_pt(box, 0.15f, 0.20f);
    SDL_FPoint b = icon_pt(box, 0.85f, 0.20f);
    SDL_FPoint c = icon_pt(box, 0.85f, 0.80f);
    SDL_FPoint d = icon_pt(box, 0.15f, 0.80f);
    float thick = box->h * 0.08f;
    icon_fill_thick_line(renderer, a.x, a.y, b.x, b.y, thick, col);
    icon_fill_thick_line(renderer, b.x, b.y, c.x, c.y, thick, col);
    icon_fill_thick_line(renderer, c.x, c.y, d.x, d.y, thick, col);
    icon_fill_thick_line(renderer, d.x, d.y, a.x, a.y, thick, col);
    float r = box->w * 0.09f;
    icon_fill_circle(renderer, a.x, a.y, r, col);
    icon_fill_circle(renderer, b.x, b.y, r, col);
    icon_fill_circle(renderer, c.x, c.y, r, col);
    icon_fill_circle(renderer, d.x, d.y, r, col);
}

/* Text Label: a plain capital "A" - the universal text-tool pictogram. Two
   diagonal strokes from the apex to each bottom corner plus a horizontal
   crossbar, same thick-line-plus-joint-dot technique as draw_wire_icon. */
static void draw_text_label_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    SDL_FPoint apex = icon_pt(box, 0.5f, 0.13f);
    SDL_FPoint bottom_l = icon_pt(box, 0.14f, 0.87f);
    SDL_FPoint bottom_r = icon_pt(box, 0.86f, 0.87f);
    SDL_FPoint bar_l = icon_pt(box, 0.27f, 0.62f);
    SDL_FPoint bar_r = icon_pt(box, 0.73f, 0.62f);
    float thick = box->h * 0.14f;
    icon_fill_thick_line(renderer, apex.x, apex.y, bottom_l.x, bottom_l.y, thick, col);
    icon_fill_thick_line(renderer, apex.x, apex.y, bottom_r.x, bottom_r.y, thick, col);
    icon_fill_thick_line(renderer, bar_l.x, bar_l.y, bar_r.x, bar_r.y, thick * 0.85f, col);
    icon_fill_circle(renderer, apex.x, apex.y, thick * 0.5f, col);
    icon_fill_circle(renderer, bottom_l.x, bottom_l.y, thick * 0.5f, col);
    icon_fill_circle(renderer, bottom_r.x, bottom_r.y, thick * 0.5f, col);
}

/* TOOL_PLACE_IC (Components) has no icon - dispatched separately in
   taskbar_render, which only calls this for icon-only tools. */
static void draw_tool_icon(SDL_Renderer *renderer, Tool tool, const SDL_Rect *box, SDL_Color col) {
    switch (tool) {
        case TOOL_SELECT: draw_select_icon(renderer, box, col); break;
        case TOOL_WIRE:   draw_wire_icon(renderer, box, col); break;
        case TOOL_VIA:    draw_via_icon(renderer, box, col); break;
        case TOOL_INPUT:  draw_input_icon(renderer, box, col); break;
        case TOOL_OUTPUT: draw_output_icon(renderer, box, col); break;
        case TOOL_SECTION:    draw_section_icon(renderer, box, col); break;
        case TOOL_TEXT_LABEL: draw_text_label_icon(renderer, box, col); break;
        default: break;
    }
}

void taskbar_init(Taskbar *tb) {
    for (int i = 0; i < TOOL_COUNT; i++) tb->label_textures[i] = NULL;
    tb->file_label_texture = NULL;
    tb->settings_label_texture = NULL;
    for (int i = 0; i < FILE_MENU_COUNT; i++) tb->file_menu_item_textures[i] = NULL;
    tb->file_menu_open = 0;
    for (int c = 0; c < MENU_CAT_COUNT; c++) {
        tb->category_textures[c][0] = NULL;
        tb->category_textures[c][1] = NULL;
        tb->category_expanded[c] = 0;
    }
    for (int i = 0; i < MENU_MAX_ITEMS; i++) tb->item_textures[i] = NULL;
    tb->menu_open = 0;
    taskbar_layout(tb, 800);
}

void taskbar_shutdown(Taskbar *tb) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (tb->label_textures[i] != NULL) {
            SDL_DestroyTexture(tb->label_textures[i]);
            tb->label_textures[i] = NULL;
        }
    }
    if (tb->file_label_texture != NULL) {
        SDL_DestroyTexture(tb->file_label_texture);
        tb->file_label_texture = NULL;
    }
    if (tb->settings_label_texture != NULL) {
        SDL_DestroyTexture(tb->settings_label_texture);
        tb->settings_label_texture = NULL;
    }
    for (int i = 0; i < FILE_MENU_COUNT; i++) {
        if (tb->file_menu_item_textures[i] != NULL) {
            SDL_DestroyTexture(tb->file_menu_item_textures[i]);
            tb->file_menu_item_textures[i] = NULL;
        }
    }
    for (int c = 0; c < MENU_CAT_COUNT; c++) {
        for (int e = 0; e < 2; e++) {
            if (tb->category_textures[c][e] != NULL) {
                SDL_DestroyTexture(tb->category_textures[c][e]);
                tb->category_textures[c][e] = NULL;
            }
        }
    }
    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        if (tb->item_textures[i] != NULL) {
            SDL_DestroyTexture(tb->item_textures[i]);
            tb->item_textures[i] = NULL;
        }
    }
}

/* Button/row labels are static strings that never change after the app
   starts (aside from a category's expand arrow, see get_category_texture),
   so each texture is built once on first use and just blitted every frame
   after - rebuilding from scratch 60 times a second would be wasteful. */
static SDL_Texture *build_texture(SDL_Renderer *renderer, TTF_Font *font, const char *text) {
    SDL_Color white = { 235, 235, 235, 255 };
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, white);
    if (surface == NULL) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return tex;
}

static SDL_Texture *get_label_texture(SDL_Renderer *renderer, Taskbar *tb, TTF_Font *font, int i) {
    if (tb->label_textures[i] != NULL || font == NULL) return tb->label_textures[i];
    tb->label_textures[i] = build_texture(renderer, font, k_labels[i]);
    return tb->label_textures[i];
}

/* Same lazy-build-once caching as get_label_texture, but for a texture slot
   that isn't Tool-indexed (File/Settings) - takes the cache slot directly. */
static SDL_Texture *get_named_texture(SDL_Renderer *renderer, SDL_Texture **slot, TTF_Font *font, const char *text) {
    if (*slot != NULL || font == NULL) return *slot;
    *slot = build_texture(renderer, font, text);
    return *slot;
}

/* Draws a button's hover-name as a small floating tooltip just below it -
   shared by the tool buttons and the File/Settings buttons. */
static void draw_hover_tooltip(SDL_Renderer *renderer, SDL_Texture *label, const SDL_Rect *r) {
    if (label == NULL) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(label, NULL, NULL, &tw, &th);
    int pad = 6;
    SDL_Rect bg = { r->x, r->y + r->h + 4, tw + pad * 2, th + pad * 2 };
    SDL_SetRenderDrawColor(renderer, 25, 25, 28, 235);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 70, 70, 78, 255);
    SDL_RenderDrawRect(renderer, &bg);
    SDL_Rect dst = { bg.x + pad, bg.y + pad, tw, th };
    SDL_RenderCopy(renderer, label, NULL, &dst);
}

static SDL_Texture *get_category_texture(SDL_Renderer *renderer, Taskbar *tb, TTF_Font *font, int cat, int expanded) {
    int slot = expanded ? 1 : 0;
    if (tb->category_textures[cat][slot] != NULL || font == NULL) return tb->category_textures[cat][slot];
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s", expanded ? "-" : "+", k_category_labels[cat]);
    tb->category_textures[cat][slot] = build_texture(renderer, font, buf);
    return tb->category_textures[cat][slot];
}

static SDL_Texture *get_item_texture(SDL_Renderer *renderer, Taskbar *tb, TTF_Font *font, int item_index) {
    if (tb->item_textures[item_index] != NULL || font == NULL) return tb->item_textures[item_index];
    tb->item_textures[item_index] = build_texture(renderer, font, k_menu_items[item_index].label);
    return tb->item_textures[item_index];
}

static SDL_Texture *get_file_menu_item_texture(SDL_Renderer *renderer, Taskbar *tb, TTF_Font *font, int item_index) {
    if (tb->file_menu_item_textures[item_index] != NULL || font == NULL) return tb->file_menu_item_textures[item_index];
    tb->file_menu_item_textures[item_index] = build_texture(renderer, font, k_file_menu_labels[item_index]);
    return tb->file_menu_item_textures[item_index];
}

/* The File dropdown's rows, top-to-bottom, anchored just below the File
   trigger button - flat/uncategorized, structurally the simplest case of
   what layout_menu_rows below does for the Components dropdown. */
static int layout_file_menu_rows(const Taskbar *tb, SDL_Rect *out_rows, int max_rows) {
    int n = 0;
    int x = tb->file_button_rect.x;
    int y = TASKBAR_HEIGHT;
    for (int i = 0; i < FILE_MENU_COUNT && n < max_rows; i++) {
        out_rows[n] = (SDL_Rect){ x, y, FILE_MENU_PANEL_W, MENU_ROW_H };
        n++;
        y += MENU_ROW_H;
    }
    return n;
}

/* The four icon tools (Select/Wire/Input/Output) are icon-only buttons now -
   their label only shows up as a hover tooltip (see taskbar_render), so the
   button itself doesn't need to be sized to fit the text. Components has no
   icon and stays a normal always-visible text button. */
void taskbar_layout(Taskbar *tb, int window_w) {
    (void)window_w;
    int icon_btn_w = BUTTON_PADDING_X + ICON_BOX + BUTTON_PADDING_X;
    int btn_h = TASKBAR_HEIGHT - BUTTON_MARGIN * 2;

    int x = BUTTON_MARGIN;
    tb->file_button_rect = (SDL_Rect){ x, BUTTON_MARGIN, icon_btn_w, btn_h };
    x += icon_btn_w + BUTTON_MARGIN;
    tb->settings_button_rect = (SDL_Rect){ x, BUTTON_MARGIN, icon_btn_w, btn_h };
    x += icon_btn_w + GROUP_GAP;

    for (int i = 0; i < TOOL_COUNT; i++) {
        int w;
        if (i == TOOL_PLACE_IC) {
            int text_w = (int)(8 * strlen(k_labels[i]));
            w = BUTTON_PADDING_X + text_w + BUTTON_PADDING_X;
        } else {
            w = icon_btn_w;
        }
        tb->button_rects[i].x = x;
        tb->button_rects[i].y = BUTTON_MARGIN;
        tb->button_rects[i].w = w;
        tb->button_rects[i].h = btn_h;
        /* the annotation-tool group (Section-Labeling/Text Label) starts
           right after Components here, separated by the same GROUP_GAP the
           File/Settings group uses on the left, instead of just
           BUTTON_MARGIN - see taskbar_render's own divider line drawn
           through the middle of this gap. This is only the BASE position
           though - taskbar_position_annotation_group (called every frame,
           since Manage Data's own width depends on the real font and on
           whether an AT28C64B happens to be selected right now, neither of
           which taskbar_layout - called only once, on init/resize - has
           access to) shifts button_rects[TOOL_SECTION..TOOL_TEXT_LABEL]
           further right by Manage Data's actual width whenever that button
           is showing, so it never overlaps this group. */
        x += w + (i == TOOL_PLACE_IC ? GROUP_GAP : BUTTON_MARGIN);
    }
}

void taskbar_position_annotation_group(Taskbar *tb, int manage_data_w) {
    int x = tb->button_rects[TOOL_PLACE_IC].x + tb->button_rects[TOOL_PLACE_IC].w;
    if (manage_data_w > 0) x += BUTTON_MARGIN + manage_data_w;
    x += GROUP_GAP;
    for (int i = TOOL_SECTION; i <= TOOL_TEXT_LABEL; i++) {
        tb->button_rects[i].x = x;
        x += tb->button_rects[i].w + BUTTON_MARGIN;
    }
}

typedef struct {
    int is_category;
    int category;   /* MenuCategory */
    int item_index;  /* into k_menu_items, only meaningful if !is_category */
    SDL_Rect rect;
} MenuRow;

/* Computes the dropdown's currently visible rows (every category header,
   plus a category's items when it's expanded) top-to-bottom, anchored just
   below the Components trigger button - shared by rendering, click
   hit-testing and taskbar_covers_point so none of them can ever disagree
   about where a row actually is. */
static int layout_menu_rows(const Taskbar *tb, MenuRow *out_rows, int max_rows) {
    int n = 0;
    int x = tb->button_rects[TOOL_PLACE_IC].x;
    int y = TASKBAR_HEIGHT;
    for (int cat = 0; cat < MENU_CAT_COUNT; cat++) {
        if (n >= max_rows) break;
        out_rows[n].is_category = 1;
        out_rows[n].category = cat;
        out_rows[n].item_index = -1;
        out_rows[n].rect = (SDL_Rect){ x, y, MENU_PANEL_W, MENU_ROW_H };
        n++;
        y += MENU_ROW_H;
        if (!tb->category_expanded[cat]) continue;
        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            if ((int)k_menu_items[i].category != cat) continue;
            if (n >= max_rows) break;
            out_rows[n].is_category = 0;
            out_rows[n].category = cat;
            out_rows[n].item_index = i;
            out_rows[n].rect = (SDL_Rect){ x, y, MENU_PANEL_W, MENU_ROW_H };
            n++;
            y += MENU_ROW_H;
        }
    }
    return n;
}

void taskbar_render(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, Tool active_tool, int hover_x, int hover_y) {
    SDL_Rect bar = { 0, 0, 4096, TASKBAR_HEIGHT };
    SDL_SetRenderDrawColor(renderer, 40, 40, 44, 255);
    SDL_RenderFillRect(renderer, &bar);

    /* File/Settings - their own group, separate from the tool buttons (see
       GROUP_GAP in taskbar_layout). No "active" state of their own, just
       hover, since they're one-shot actions rather than a persistent mode. */
    int hovered_file = (hover_x >= tb->file_button_rect.x && hover_x < tb->file_button_rect.x + tb->file_button_rect.w &&
                         hover_y >= tb->file_button_rect.y && hover_y < tb->file_button_rect.y + tb->file_button_rect.h);
    int hovered_settings = (hover_x >= tb->settings_button_rect.x && hover_x < tb->settings_button_rect.x + tb->settings_button_rect.w &&
                             hover_y >= tb->settings_button_rect.y && hover_y < tb->settings_button_rect.y + tb->settings_button_rect.h);
    SDL_Color group_icon_col = { 235, 235, 235, 255 };

    SDL_SetRenderDrawColor(renderer, hovered_file ? 76 : 60, hovered_file ? 76 : 60, hovered_file ? 84 : 66, 255);
    SDL_RenderFillRect(renderer, &tb->file_button_rect);
    SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
    SDL_RenderDrawRect(renderer, &tb->file_button_rect);
    SDL_Rect file_icon_box = { tb->file_button_rect.x + (tb->file_button_rect.w - ICON_BOX) / 2,
                                tb->file_button_rect.y + (tb->file_button_rect.h - ICON_BOX) / 2, ICON_BOX, ICON_BOX };
    draw_file_icon(renderer, &file_icon_box, group_icon_col);

    SDL_SetRenderDrawColor(renderer, hovered_settings ? 76 : 60, hovered_settings ? 76 : 60, hovered_settings ? 84 : 66, 255);
    SDL_RenderFillRect(renderer, &tb->settings_button_rect);
    SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
    SDL_RenderDrawRect(renderer, &tb->settings_button_rect);
    SDL_Rect settings_icon_box = { tb->settings_button_rect.x + (tb->settings_button_rect.w - ICON_BOX) / 2,
                                    tb->settings_button_rect.y + (tb->settings_button_rect.h - ICON_BOX) / 2, ICON_BOX, ICON_BOX };
    draw_settings_icon(renderer, &settings_icon_box, group_icon_col);

    int divider_x = (tb->settings_button_rect.x + tb->settings_button_rect.w + tb->button_rects[TOOL_SELECT].x) / 2;
    SDL_SetRenderDrawColor(renderer, 60, 60, 66, 255);
    SDL_RenderDrawLine(renderer, divider_x, BUTTON_MARGIN + 3, divider_x, TASKBAR_HEIGHT - BUTTON_MARGIN - 3);

    /* same divider, marking off the Section-Labeling/Text Label annotation
       group from whatever precedes it - Components alone, or Components +
       Manage Data when that button is showing. button_rects[TOOL_SECTION].x
       is always exactly GROUP_GAP past that "whatever precedes it" edge (see
       taskbar_position_annotation_group, called every frame), so backing off
       half a GROUP_GAP from it lands the divider centered in the actual gap
       without this code needing to know Manage Data's width itself. */
    int divider_x2 = tb->button_rects[TOOL_SECTION].x - GROUP_GAP / 2;
    SDL_SetRenderDrawColor(renderer, 60, 60, 66, 255);
    SDL_RenderDrawLine(renderer, divider_x2, BUTTON_MARGIN + 3, divider_x2, TASKBAR_HEIGHT - BUTTON_MARGIN - 3);

    /* -1 = no icon tool is hovered; used below to draw that button's label
       as a floating tooltip once every button/icon has already been drawn,
       so the tooltip is never drawn over by a later button. */
    int hovered_tool = -1;

    for (int i = 0; i < TOOL_COUNT; i++) {
        const SDL_Rect *r = &tb->button_rects[i];
        int hovered = (hover_x >= r->x && hover_x < r->x + r->w && hover_y >= r->y && hover_y < r->y + r->h);
        if (hovered) hovered_tool = i;

        if (i == (int)active_tool) {
            SDL_SetRenderDrawColor(renderer, hovered ? 82 : 70, hovered ? 145 : 130, hovered ? 215 : 200, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, hovered ? 76 : 60, hovered ? 76 : 60, hovered ? 84 : 66, 255);
        }
        SDL_RenderFillRect(renderer, r);
        SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
        SDL_RenderDrawRect(renderer, r);

        if (i == TOOL_PLACE_IC) {
            /* no icon for Components - its label is the only way to tell
               it apart, so it stays always-visible instead of hover-only */
            SDL_Texture *label = get_label_texture(renderer, tb, font, i);
            if (label != NULL) {
                int tw = 0, th = 0;
                SDL_QueryTexture(label, NULL, NULL, &tw, &th);
                SDL_Rect dst = { r->x + (r->w - tw) / 2, r->y + (r->h - th) / 2, tw, th };
                SDL_RenderCopy(renderer, label, NULL, &dst);
            }
        } else {
            SDL_Rect icon_box = { r->x + (r->w - ICON_BOX) / 2, r->y + (r->h - ICON_BOX) / 2, ICON_BOX, ICON_BOX };
            SDL_Color icon_col = { 235, 235, 235, 255 };
            draw_tool_icon(renderer, (Tool)i, &icon_box, icon_col);
        }
    }

    /* the hovered icon tool's name, as a small floating tooltip just below
       its button - the only place that label shows up now that the button
       itself is icon-only */
    if (hovered_tool >= 0 && hovered_tool != TOOL_PLACE_IC) {
        draw_hover_tooltip(renderer, get_label_texture(renderer, tb, font, hovered_tool), &tb->button_rects[hovered_tool]);
    }
    /* both labels are suppressed together while the File dropdown is open -
       it visually sits right behind both buttons, so a floating "File" or
       "Settings" tooltip would peek out from underneath it. The Settings
       popup is a separate, centered modal that doesn't sit behind either
       button, so it never suppresses these - hovering still shows them
       normally (just dimmed under the popup's own scrim, same as the rest
       of the taskbar). */
    if (hovered_file && !tb->file_menu_open) {
        draw_hover_tooltip(renderer, get_named_texture(renderer, &tb->file_label_texture, font, "File"), &tb->file_button_rect);
    }
    if (hovered_settings && !tb->file_menu_open) {
        draw_hover_tooltip(renderer, get_named_texture(renderer, &tb->settings_label_texture, font, "Settings"), &tb->settings_button_rect);
    }
}

void taskbar_render_dropdowns(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, const char *active_ic_name,
                               int hover_x, int hover_y) {
    if (tb->file_menu_open) {
        SDL_Rect frows[FILE_MENU_COUNT];
        int fn = layout_file_menu_rows(tb, frows, FILE_MENU_COUNT);
        if (fn > 0) {
            SDL_Rect panel = { frows[0].x, frows[0].y, FILE_MENU_PANEL_W,
                                frows[fn - 1].y + frows[fn - 1].h - frows[0].y };
            SDL_SetRenderDrawColor(renderer, 32, 32, 36, 245);
            SDL_RenderFillRect(renderer, &panel);
            SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
            SDL_RenderDrawRect(renderer, &panel);

            for (int i = 0; i < fn; i++) {
                int hovered = (hover_x >= frows[i].x && hover_x < frows[i].x + frows[i].w &&
                               hover_y >= frows[i].y && hover_y < frows[i].y + frows[i].h);
                SDL_SetRenderDrawColor(renderer, hovered ? 56 : 40, hovered ? 56 : 40, hovered ? 62 : 44, 255);
                SDL_RenderFillRect(renderer, &frows[i]);
                SDL_Texture *label = get_file_menu_item_texture(renderer, tb, font, i);
                if (label != NULL) {
                    int tw = 0, th = 0;
                    SDL_QueryTexture(label, NULL, NULL, &tw, &th);
                    SDL_Rect dst = { frows[i].x + MENU_ROW_INDENT, frows[i].y + (frows[i].h - th) / 2, tw, th };
                    SDL_RenderCopy(renderer, label, NULL, &dst);
                }
            }
        }
    }

    if (!tb->menu_open) return;

    MenuRow rows[MENU_MAX_ROWS];
    int n = layout_menu_rows(tb, rows, MENU_MAX_ROWS);
    if (n == 0) return;

    SDL_Rect panel = { rows[0].rect.x, rows[0].rect.y, MENU_PANEL_W, rows[n - 1].rect.y + rows[n - 1].rect.h - rows[0].rect.y };
    SDL_SetRenderDrawColor(renderer, 32, 32, 36, 245);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
    SDL_RenderDrawRect(renderer, &panel);

    for (int i = 0; i < n; i++) {
        const MenuRow *row = &rows[i];
        int hovered = (hover_x >= row->rect.x && hover_x < row->rect.x + row->rect.w &&
                       hover_y >= row->rect.y && hover_y < row->rect.y + row->rect.h);
        SDL_Texture *label;
        int label_x;
        if (row->is_category) {
            if (hovered) SDL_SetRenderDrawColor(renderer, 64, 64, 72, 255);
            else SDL_SetRenderDrawColor(renderer, 50, 50, 56, 255);
            SDL_RenderFillRect(renderer, &row->rect);
            label = get_category_texture(renderer, tb, font, row->category, tb->category_expanded[row->category]);
            label_x = row->rect.x + 10;
        } else {
            const MenuItem *item = &k_menu_items[row->item_index];
            int selected = (active_ic_name != NULL && strcmp(item->ic_name, active_ic_name) == 0);
            if (selected) {
                SDL_SetRenderDrawColor(renderer, hovered ? 85 : 70, hovered ? 145 : 130, hovered ? 215 : 200, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, hovered ? 56 : 40, hovered ? 56 : 40, hovered ? 62 : 44, 255);
            }
            SDL_RenderFillRect(renderer, &row->rect);
            label = get_item_texture(renderer, tb, font, row->item_index);
            label_x = row->rect.x + MENU_ROW_INDENT;
        }
        if (label != NULL) {
            int tw = 0, th = 0;
            SDL_QueryTexture(label, NULL, NULL, &tw, &th);
            SDL_Rect dst = { label_x, row->rect.y + (row->rect.h - th) / 2, tw, th };
            SDL_RenderCopy(renderer, label, NULL, &dst);
        }
    }
}

TaskbarClickKind taskbar_handle_click(Taskbar *tb, int x, int y, Tool *out_tool, const char **out_ic_name,
                                       FileMenuItem *out_file_item) {
    const SDL_Rect *fr = &tb->file_button_rect;
    if (x >= fr->x && x < fr->x + fr->w && y >= fr->y && y < fr->y + fr->h) {
        tb->menu_open = 0;
        tb->file_menu_open = !tb->file_menu_open;
        return TASKBAR_CLICK_CONSUMED;
    }
    const SDL_Rect *sr = &tb->settings_button_rect;
    if (x >= sr->x && x < sr->x + sr->w && y >= sr->y && y < sr->y + sr->h) {
        tb->menu_open = 0;
        tb->file_menu_open = 0;
        return TASKBAR_CLICK_SETTINGS;
    }

    for (int i = 0; i < TOOL_COUNT; i++) {
        const SDL_Rect *r = &tb->button_rects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
            tb->file_menu_open = 0;
            if (i == TOOL_PLACE_IC) {
                tb->menu_open = !tb->menu_open;
                return TASKBAR_CLICK_CONSUMED;
            }
            tb->menu_open = 0;
            *out_tool = (Tool)i;
            return TASKBAR_CLICK_TOOL;
        }
    }

    if (tb->file_menu_open) {
        SDL_Rect frows[FILE_MENU_COUNT];
        int fn = layout_file_menu_rows(tb, frows, FILE_MENU_COUNT);
        for (int i = 0; i < fn; i++) {
            const SDL_Rect *r = &frows[i];
            if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
                tb->file_menu_open = 0;
                *out_file_item = (FileMenuItem)i;
                return TASKBAR_CLICK_FILE_MENU_ITEM;
            }
        }
        tb->file_menu_open = 0; /* clicked elsewhere while open - dismiss it, same as any dropdown */
    }

    if (tb->menu_open) {
        MenuRow rows[MENU_MAX_ROWS];
        int n = layout_menu_rows(tb, rows, MENU_MAX_ROWS);
        for (int i = 0; i < n; i++) {
            const SDL_Rect *r = &rows[i].rect;
            if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
                if (rows[i].is_category) {
                    tb->category_expanded[rows[i].category] = !tb->category_expanded[rows[i].category];
                    return TASKBAR_CLICK_CONSUMED;
                }
                tb->menu_open = 0;
                *out_ic_name = k_menu_items[rows[i].item_index].ic_name;
                return TASKBAR_CLICK_IC;
            }
        }
        tb->menu_open = 0; /* clicked elsewhere while open - dismiss it, same as any dropdown */
    }

    return TASKBAR_CLICK_NONE;
}

int taskbar_covers_point(const Taskbar *tb, int x, int y) {
    if (y < TASKBAR_HEIGHT) return 1;

    if (tb->file_menu_open) {
        SDL_Rect frows[FILE_MENU_COUNT];
        int fn = layout_file_menu_rows(tb, frows, FILE_MENU_COUNT);
        if (fn > 0) {
            int fx0 = frows[0].x, fy0 = frows[0].y, fx1 = fx0 + FILE_MENU_PANEL_W;
            int fy1 = frows[fn - 1].y + frows[fn - 1].h;
            if (x >= fx0 && x < fx1 && y >= fy0 && y < fy1) return 1;
        }
    }

    if (!tb->menu_open) return 0;

    MenuRow rows[MENU_MAX_ROWS];
    int n = layout_menu_rows(tb, rows, MENU_MAX_ROWS);
    if (n == 0) return 0;
    /* the panel is a solid rectangle covering every row, not just the ones
       directly under the cursor - a click in its background between rows
       should still be swallowed rather than fall through to the circuit */
    int x0 = rows[0].rect.x, y0 = rows[0].rect.y, x1 = x0 + MENU_PANEL_W;
    int y1 = rows[n - 1].rect.y + rows[n - 1].rect.h;
    return x >= x0 && x < x1 && y >= y0 && y < y1;
}
