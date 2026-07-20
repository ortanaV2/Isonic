#include <math.h>
#include <stdio.h>
#include "data_editor.h"
#include "ics/at28c64b.h"
#include "text_util.h"

#define BUTTON_MARGIN 6
#define BUTTON_PADDING_X 14
#define PANEL_W 380
#define HEADER_H 28
/* one less than BIT_GAP (the gap between two rows' own bit-toggle boxes,
   see BIT_GAP below) - trimmed 1px off what would otherwise keep the
   header-to-list gap visually consistent with the list's own row spacing,
   to match layer_panel.c's own HEADER_ROW_GAP 1:1 (see its comment) after
   both were nudged down together for cross-panel alignment. */
#define HEADER_BODY_GAP (BIT_GAP - 1)
#define ROW_H 22
#define ROW_PAD_X 8
#define ADDR_DEC_W 46
#define ADDR_HEX_W 66
#define BIT_W 22
#define BIT_GAP 4
#define SCROLL_STEP 3 /* rows per wheel notch */
#define SCROLLBAR_W 10
#define SCROLLBAR_MARGIN 6
#define SCROLLBAR_PAD_Y BIT_GAP /* same gap above/below the track as HEADER_BODY_GAP uses */
#define SCROLLBAR_MIN_THUMB_H 20
#define ROW_HIGHLIGHT_SCROLLBAR_GAP 6 /* breathing room between the current-address highlight and the scrollbar */
#define CLOSE_X_THICKNESS 1.8f

static const SDL_Color BG_COLOR = { 32, 32, 36, 255 }; /* #202024, opaque - matches layer_panel.c/settings_panel.c */
static const SDL_Color BORDER_COLOR = { 20, 20, 22, 255 };
static const SDL_Color ADDR_COLOR = { 140, 140, 146, 255 }; /* dim/read-only, same as render.c's OUTPUT_LABEL_COLOR */
static const SDL_Color HEADER_COLOR = { 190, 190, 196, 255 };
static const SDL_Color BIT_BG = { 50, 50, 56, 255 };
static const SDL_Color BIT_BG_HOVER = { 64, 64, 72, 255 };
static const SDL_Color BIT_TEXT_COLOR = { 225, 225, 230, 255 };
static const SDL_Color BUTTON_COLOR = { 60, 60, 66, 255 };
static const SDL_Color BUTTON_COLOR_HOVER = { 76, 76, 84, 255 };
static const SDL_Color BUTTON_COLOR_ACTIVE = { 70, 130, 200, 255 };
static const SDL_Color TRACK_COLOR = { 26, 26, 30, 255 };
static const SDL_Color THUMB_COLOR = { 90, 90, 98, 255 };
static const SDL_Color THUMB_COLOR_HOVER = { 110, 110, 120, 255 };

void data_editor_init(DataEditor *de) {
    de->open = 0;
    de->editing = NULL;
    de->scroll_row = 0;
    de->button_rect = (SDL_Rect){ 0, 0, 0, 0 };
    de->panel_rect = (SDL_Rect){ 0, 0, 0, 0 };
    de->body_rect = (SDL_Rect){ 0, 0, 0, 0 };
    de->scrollbar_rect = (SDL_Rect){ 0, 0, 0, 0 };
    de->thumb_rect = (SDL_Rect){ 0, 0, 0, 0 };
    de->col_bits_x = 0;
    de->row_h = ROW_H;
    de->visible_rows = 0;
    de->dragging_scrollbar = 0;
}

/* Where the thumb sits within the track for the current scroll_row/
   max_scroll - shared by rendering and the click/motion handlers so they
   can never disagree about the mapping between thumb position and scroll
   offset. */
static SDL_Rect thumb_rect_for(const DataEditor *de, int max_scroll) {
    int track_h = de->scrollbar_rect.h;
    int thumb_h = (AT28C64B_SIZE > 0) ? (int)((long)de->visible_rows * track_h / AT28C64B_SIZE) : track_h;
    if (thumb_h < SCROLLBAR_MIN_THUMB_H) thumb_h = SCROLLBAR_MIN_THUMB_H;
    if (thumb_h > track_h) thumb_h = track_h;
    int travel = track_h - thumb_h;
    int thumb_y = de->scrollbar_rect.y + (max_scroll > 0 ? (int)((long)de->scroll_row * travel / max_scroll) : 0);
    return (SDL_Rect){ de->scrollbar_rect.x, thumb_y, de->scrollbar_rect.w, thumb_h };
}

/* Inverse of thumb_rect_for - which scroll_row a thumb centered at screen y
   corresponds to, clamped to [0, max_scroll]. Used both for "jump to where
   the track was clicked" and for dragging. */
static int scroll_row_for_thumb_y(const DataEditor *de, int thumb_y, int max_scroll) {
    int travel = de->scrollbar_rect.h - de->thumb_rect.h;
    if (travel <= 0) return 0;
    int rel = thumb_y - de->scrollbar_rect.y;
    if (rel < 0) rel = 0;
    if (rel > travel) rel = travel;
    int row = (int)((long)rel * max_scroll / travel);
    if (row < 0) row = 0;
    if (row > max_scroll) row = max_scroll;
    return row;
}

Component *data_editor_eligible(Circuit *circuit) {
    int comp_idx = -1, comp_count = 0, wire_count = 0;
    for (int i = 0; i < circuit->component_high_water; i++) {
        if (circuit->components[i].in_use && circuit->components[i].selected) {
            comp_idx = i;
            comp_count++;
        }
    }
    for (int i = 0; i < circuit->wire_high_water; i++) {
        if (circuit->wires[i].in_use && circuit->wires[i].selected) wire_count++;
    }
    if (comp_count != 1 || wire_count != 0) return NULL;

    Component *c = &circuit->components[comp_idx];
    return ic_at28c64b_is(c) ? c : NULL;
}

static int point_in(const SDL_Rect *r, int x, int y) {
    return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* A real filled quad instead of SDL_RenderDrawLine's fixed 1px, same
   technique taskbar.c's hand-drawn tool icons use (icon_fill_thick_line) -
   duplicated here rather than shared since it's this one small helper, not
   worth exposing a whole header for. */
static void draw_thick_line(SDL_Renderer *renderer, float x0, float y0, float x1, float y1, float thickness,
                            SDL_Color col) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = thickness * 0.5f;
    SDL_Vertex verts[4] = {
        { { x0 + nx * hw, y0 + ny * hw }, col, { 0, 0 } },
        { { x1 + nx * hw, y1 + ny * hw }, col, { 0, 0 } },
        { { x1 - nx * hw, y1 - ny * hw }, col, { 0, 0 } },
        { { x0 - nx * hw, y0 - ny * hw }, col, { 0, 0 } },
    };
    int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(renderer, NULL, verts, 4, indices, 6);
}

void data_editor_render(SDL_Renderer *renderer, TTF_Font *font, DataEditor *de, Component *eligible,
                        const Taskbar *tb, int window_w, int window_h, int hover_x, int hover_y) {
    /* only closes itself if the component it's editing was deleted from the
       circuit out from under it - anything else (the selection moving to a
       different component, or to nothing at all) leaves an already-open
       panel exactly as it was, see data_editor.h. */
    if (de->editing != NULL && !de->editing->in_use) {
        de->open = 0;
        de->editing = NULL;
    }

    /* the button itself: offered only while eligible, completely separate
       from whether the panel below is currently open/rendering */
    de->button_rect = (SDL_Rect){ 0, 0, 0, 0 };
    if (eligible != NULL) {
        const char *label = "Manage Data";
        int tw = 0, th = 0;
        if (font != NULL) text_util_measure(font, label, &tw, &th);
        const SDL_Rect *comp_btn = &tb->button_rects[TOOL_PLACE_IC];
        de->button_rect = (SDL_Rect){
            comp_btn->x + comp_btn->w + BUTTON_MARGIN, comp_btn->y,
            BUTTON_PADDING_X + tw + BUTTON_PADDING_X, comp_btn->h,
        };

        int btn_hovered = point_in(&de->button_rect, hover_x, hover_y);
        /* only blue for the EEPROM the panel is actually bound to right now
           - eligible alone isn't enough, since the panel stays open/bound to
           whatever it was opened for even after a different (also eligible)
           EEPROM gets selected on the canvas, see data_editor.h */
        int is_active = de->open && de->editing == eligible;
        SDL_Color btn_col = is_active ? BUTTON_COLOR_ACTIVE : (btn_hovered ? BUTTON_COLOR_HOVER : BUTTON_COLOR);
        SDL_SetRenderDrawColor(renderer, btn_col.r, btn_col.g, btn_col.b, 255);
        SDL_RenderFillRect(renderer, &de->button_rect);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &de->button_rect);
        if (font != NULL) {
            text_util_draw(renderer, font, label, de->button_rect.x + BUTTON_PADDING_X,
                            de->button_rect.y + (de->button_rect.h - th) / 2, BIT_TEXT_COLOR);
        }
    }

    if (!de->open || de->editing == NULL) return;

    de->panel_rect = (SDL_Rect){ window_w - PANEL_W, TASKBAR_HEIGHT, PANEL_W, window_h - TASKBAR_HEIGHT };
    SDL_SetRenderDrawColor(renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, BG_COLOR.a);
    SDL_RenderFillRect(renderer, &de->panel_rect);
    /* the outer border itself is drawn LAST (see the bottom of this
       function) - same reasoning as layer_panel.c's identical pattern: the
       current-address row highlight below fills edge-to-edge like every row
       in that panel does, and drawing the border first just let that full-
       width fill paint straight over its side pixels, reading as the
       highlight "poking out" of the panel by 1px. */

    int col_dec_x = de->panel_rect.x + ROW_PAD_X;
    int col_hex_x = col_dec_x + ADDR_DEC_W + ROW_PAD_X;
    de->col_bits_x = col_hex_x + ADDR_HEX_W + ROW_PAD_X * 2;

    if (font != NULL) {
        /* actually centered on text height instead of a fixed guessed
           offset - "Addr"/"Hex"/"Data..." have no descenders, so a fixed
           offset tuned by eye reads as a few px too high once you compare
           it against a properly centered box. */
        int hw, hh;
        text_util_measure(font, "Addr", &hw, &hh);
        (void)hw;
        int header_text_y = de->panel_rect.y + (HEADER_H - hh) / 2;
        text_util_draw(renderer, font, "Addr", col_dec_x, header_text_y, HEADER_COLOR);
        text_util_draw(renderer, font, "Hex", col_hex_x, header_text_y, HEADER_COLOR);
        text_util_draw(renderer, font, "Data (bit7..bit0)", de->col_bits_x, header_text_y, HEADER_COLOR);
    }

    /* X close button, top-right of the header */
    int close_size = HEADER_H - 8;
    de->close_rect = (SDL_Rect){ de->panel_rect.x + de->panel_rect.w - close_size - 6, de->panel_rect.y + 4,
                                  close_size, close_size };
    int close_hovered = point_in(&de->close_rect, hover_x, hover_y);
    SDL_Color close_col = close_hovered ? BIT_BG_HOVER : BIT_BG;
    SDL_SetRenderDrawColor(renderer, close_col.r, close_col.g, close_col.b, 255);
    SDL_RenderFillRect(renderer, &de->close_rect);
    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, &de->close_rect);
    draw_thick_line(renderer, de->close_rect.x + 5, de->close_rect.y + 5,
                     de->close_rect.x + de->close_rect.w - 5, de->close_rect.y + de->close_rect.h - 5,
                     CLOSE_X_THICKNESS, BIT_TEXT_COLOR);
    draw_thick_line(renderer, de->close_rect.x + de->close_rect.w - 5, de->close_rect.y + 5,
                     de->close_rect.x + 5, de->close_rect.y + de->close_rect.h - 5,
                     CLOSE_X_THICKNESS, BIT_TEXT_COLOR);

    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawLine(renderer, de->panel_rect.x, de->panel_rect.y + HEADER_H,
                        de->panel_rect.x + de->panel_rect.w, de->panel_rect.y + HEADER_H);

    de->row_h = ROW_H;
    de->body_rect = (SDL_Rect){
        de->panel_rect.x, de->panel_rect.y + HEADER_H + HEADER_BODY_GAP,
        de->panel_rect.w, de->panel_rect.h - HEADER_H - HEADER_BODY_GAP,
    };
    de->visible_rows = de->body_rect.h / de->row_h;

    int max_scroll = AT28C64B_SIZE - de->visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (de->scroll_row > max_scroll) de->scroll_row = max_scroll;
    if (de->scroll_row < 0) de->scroll_row = 0;

    de->scrollbar_rect = (SDL_Rect){
        de->panel_rect.x + de->panel_rect.w - SCROLLBAR_W - SCROLLBAR_MARGIN, de->body_rect.y + SCROLLBAR_PAD_Y,
        SCROLLBAR_W, de->body_rect.h - SCROLLBAR_PAD_Y * 2,
    };
    de->thumb_rect = thumb_rect_for(de, max_scroll);

    SDL_SetRenderDrawColor(renderer, TRACK_COLOR.r, TRACK_COLOR.g, TRACK_COLOR.b, 255);
    SDL_RenderFillRect(renderer, &de->scrollbar_rect);
    int thumb_hovered = point_in(&de->thumb_rect, hover_x, hover_y);
    SDL_Color thumb_col = (thumb_hovered || de->dragging_scrollbar) ? THUMB_COLOR_HOVER : THUMB_COLOR;
    SDL_SetRenderDrawColor(renderer, thumb_col.r, thumb_col.g, thumb_col.b, 255);
    SDL_RenderFillRect(renderer, &de->thumb_rect);

    unsigned char *mem = ic_at28c64b_memory(de->editing);
    int cur_addr = (mem != NULL) ? ic_at28c64b_current_address(de->editing) : -1;

    for (int row = 0; mem != NULL && row < de->visible_rows; row++) {
        int addr = de->scroll_row + row;
        if (addr >= AT28C64B_SIZE) break;
        int ry = de->body_rect.y + row * de->row_h;

        if (addr == cur_addr) {
            /* the byte currently sitting on A0..A12 - same shade as "+ Add
               Layer"'s idle background (layer_panel.c's BUTTON_BG), lighter
               than the panel itself so it reads as a highlight without
               fighting the bit-box/hover colors drawn on top of it below.
               Stops a few px short of the scrollbar's own left edge (not
               flush against it) so it visibly ends before the scrollbar
               rather than looking like it runs into/underneath it. */
            int row_bg_w = de->scrollbar_rect.x - de->panel_rect.x - ROW_HIGHLIGHT_SCROLLBAR_GAP;
            if (row_bg_w < 0) row_bg_w = 0;
            SDL_Rect row_bg = { de->panel_rect.x, ry, row_bg_w, de->row_h };
            SDL_SetRenderDrawColor(renderer, BIT_BG.r, BIT_BG.g, BIT_BG.b, 255);
            SDL_RenderFillRect(renderer, &row_bg);
        }

        if (font != NULL) {
            /* buffers sized well above GCC's own worst-case estimate for a
               plain %d/%X (based on int's full range, not addr's actual
               0..AT28C64B_SIZE-1 span) - same fix as diagnostics.c's
               earlier -Wformat-truncation, see its own comment there */
            char dec[16], hexs[16];
            snprintf(dec, sizeof(dec), "%d", addr);
            snprintf(hexs, sizeof(hexs), "0x%04X", addr);
            text_util_draw(renderer, font, dec, col_dec_x, ry + (de->row_h - 14) / 2, ADDR_COLOR);
            text_util_draw(renderer, font, hexs, col_hex_x, ry + (de->row_h - 14) / 2, ADDR_COLOR);
        }

        unsigned char byte = mem[addr];
        for (int bit = 0; bit < 8; bit++) {
            int bit_index = 7 - bit; /* MSB first, left to right */
            SDL_Rect box = { de->col_bits_x + bit * (BIT_W + BIT_GAP), ry + 2, BIT_W, de->row_h - 4 };
            int hovered = point_in(&box, hover_x, hover_y);
            SDL_Color box_col = hovered ? BIT_BG_HOVER : BIT_BG;
            SDL_SetRenderDrawColor(renderer, box_col.r, box_col.g, box_col.b, 255);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
            SDL_RenderDrawRect(renderer, &box);
            if (font != NULL) {
                const char *bit_text = (byte & (1 << bit_index)) ? "1" : "0";
                int btw, bth;
                text_util_measure(font, bit_text, &btw, &bth);
                text_util_draw(renderer, font, bit_text, box.x + (box.w - btw) / 2, box.y + (box.h - bth) / 2,
                               BIT_TEXT_COLOR);
            }
        }
    }

    /* drawn last (on top of every row fill above, including the
       current-address highlight) so it's always a crisp, unbroken frame -
       see the comment where the fill itself happens, near the top. */
    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, &de->panel_rect);
}

int data_editor_covers_point(const DataEditor *de, int x, int y) {
    if (point_in(&de->button_rect, x, y)) return 1;
    if (de->open && point_in(&de->panel_rect, x, y)) return 1;
    return 0;
}

int data_editor_handle_click(DataEditor *de, Component *eligible, int x, int y) {
    if (eligible != NULL && point_in(&de->button_rect, x, y)) {
        if (de->open && de->editing == eligible) {
            /* clicking the button for the EEPROM already open closes it */
            de->open = 0;
            de->editing = NULL;
        } else {
            /* closed, or open for a different EEPROM - open/switch straight
               to this one instead of toggling closed, so picking a
               different chip's button just replaces the panel's content */
            if (de->editing != eligible) de->scroll_row = 0;
            de->open = 1;
            de->editing = eligible;
        }
        return 1;
    }
    if (!de->open || de->editing == NULL) return 0;
    if (!point_in(&de->panel_rect, x, y)) return 0;

    if (point_in(&de->close_rect, x, y)) {
        de->open = 0;
        de->editing = NULL;
        return 1;
    }
    if (point_in(&de->scrollbar_rect, x, y)) {
        /* jump straight to the clicked position (thumb center lands under
           the cursor) and start a drag, so a click that's slightly off the
           thumb doesn't feel like it did nothing - same "click track to
           jump, then keep dragging" behavior most scrollbars use. */
        int max_scroll = AT28C64B_SIZE - de->visible_rows;
        if (max_scroll < 0) max_scroll = 0;
        de->scroll_row = scroll_row_for_thumb_y(de, y - de->thumb_rect.h / 2, max_scroll);
        de->dragging_scrollbar = 1;
        return 1;
    }
    if (y < de->body_rect.y) return 1; /* header - consumed, nothing else to act on */

    int row = (y - de->body_rect.y) / de->row_h;
    int addr = de->scroll_row + row;
    if (addr < 0 || addr >= AT28C64B_SIZE) return 1;

    int rel_x = x - de->col_bits_x;
    if (rel_x < 0) return 1;
    int bit = rel_x / (BIT_W + BIT_GAP);
    if (bit < 0 || bit >= 8) return 1;
    if (rel_x - bit * (BIT_W + BIT_GAP) >= BIT_W) return 1; /* clicked the gap between boxes */

    unsigned char *mem = ic_at28c64b_memory(de->editing);
    if (mem == NULL) return 1;
    int bit_index = 7 - bit;
    mem[addr] ^= (unsigned char)(1 << bit_index);
    return 1;
}

int data_editor_handle_wheel(DataEditor *de, int x, int y, int wheel_y) {
    if (!de->open || !point_in(&de->panel_rect, x, y)) return 0;

    de->scroll_row -= wheel_y * SCROLL_STEP;
    if (de->scroll_row < 0) de->scroll_row = 0;
    int max_scroll = AT28C64B_SIZE - de->visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (de->scroll_row > max_scroll) de->scroll_row = max_scroll;
    return 1;
}

void data_editor_handle_motion(DataEditor *de, int y) {
    if (!de->dragging_scrollbar) return;
    int max_scroll = AT28C64B_SIZE - de->visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    de->scroll_row = scroll_row_for_thumb_y(de, y - de->thumb_rect.h / 2, max_scroll);
}

void data_editor_handle_release(DataEditor *de) {
    de->dragging_scrollbar = 0;
}
