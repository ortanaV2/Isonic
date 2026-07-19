#include <math.h>
#include <stdio.h>
#include <string.h>
#include "layer_panel.h"
#include "text_util.h"

/* Column layout, left to right within a row:
   [pad] swatch [gap] number [gap] name (flexible) [gap] up|down|delete [pad]
   The three reorder/delete buttons are a fixed-size cluster anchored to the
   row's right edge, so their position never depends on the name text's
   length. The name column itself grows to fit the longest current layer
   name (see layer_panel_render) rather than clipping it. */
#define HEADER_H 28          /* matches data_editor.c's HEADER_H exactly */
#define ROW_H 22             /* matches data_editor.c's ROW_H exactly */
#define ROW_PAD_X 8          /* matches data_editor.c's ROW_PAD_X exactly */
#define SWATCH_SIZE 14
#define NUM_W 14
#define COL_GAP 6
#define BTN_SIZE 18
#define BTN_GAP 3
#define NAME_GAP 6
#define BUTTON_CLUSTER_W (BTN_SIZE * 3 + BTN_GAP * 2)
#define MIN_NAME_W 60        /* keeps the panel from looking cramped even when every name is short ("GND", "+5V") */
/* extra gap after the header separator before the first row starts, so it
   matches the gap between any two rows' own buttons - a row's button sits
   (ROW_H-BTN_SIZE)/2 below its own top edge, and the row below it starts
   another (ROW_H-BTN_SIZE)/2 further before its own button, so two
   consecutive rows' buttons end up that same amount apart twice over;
   without this, the header-to-first-row gap is only half that. */
#define HEADER_ROW_GAP ((ROW_H - BTN_SIZE) / 2)

#define CLOSE_X_THICKNESS 1.8f  /* matches data_editor.c's own close-X stroke weight exactly */

/* Every color below is copied verbatim from data_editor.c rather than
   shared through a header - deliberately duplicated (same reasoning as
   render.c's SELECTION_COLOR elsewhere) so the two panels' palettes can
   never silently drift apart from whichever one happens to get edited
   first, while still not forcing a shared-constants header for a handful of
   colors. An ordinary row is NOT given its own (lighter) background fill -
   same hierarchy data_editor.c uses, where only individual interactive
   boxes get a fill and the rest of a row sits directly on the panel's own
   BG_COLOR - except the active layer's row, which does (see
   ROW_BG_ACTIVE below). */
static const SDL_Color BG_COLOR = { 32, 32, 36, 245 };
static const SDL_Color BORDER_COLOR = { 20, 20, 22, 255 };
static const SDL_Color HEADER_COLOR = { 190, 190, 196, 255 };
static const SDL_Color TEXT_COLOR = { 225, 225, 230, 255 };          /* == data_editor's BIT_TEXT_COLOR */
static const SDL_Color DIM_TEXT_COLOR = { 140, 140, 146, 255 };      /* == data_editor's ADDR_COLOR */
static const SDL_Color BUTTON_BG = { 50, 50, 56, 255 };              /* == data_editor's BIT_BG */
static const SDL_Color BUTTON_BG_HOVER = { 64, 64, 72, 255 };        /* == data_editor's BIT_BG_HOVER */
static const SDL_Color BUTTON_BG_DISABLED = { 38, 38, 42, 255 };
static const SDL_Color BUTTON_ICON_COLOR = { 225, 225, 230, 255 };
static const SDL_Color BUTTON_ICON_DISABLED_COLOR = { 90, 90, 96, 255 };
/* matches render.c's private SELECTION_COLOR - kept as a separate copy since
   that one isn't exposed outside render.c */
static const SDL_Color EDIT_HIGHLIGHT_COLOR = { 90, 170, 255, 255 };
/* light gray full-row fill marking the active layer - the one deliberate
   exception to "rows don't get their own background fill" above, since this
   is the one thing in the panel that always needs to be visible at a glance. */
static const SDL_Color ROW_BG_ACTIVE = { 76, 76, 82, 255 };

/* Vector icon helpers for the reorder/delete buttons - hand-drawn via
   SDL_RenderGeometry, same all-vector approach taskbar.c's tool icons use,
   so these always render crisp and identically sized instead of depending
   on a font's caret/v/x glyph metrics. */
static void fill_triangle(SDL_Renderer *renderer, SDL_FPoint p0, SDL_FPoint p1, SDL_FPoint p2, SDL_Color col) {
    SDL_Vertex verts[3];
    verts[0].position = p0; verts[0].color = col; verts[0].tex_coord = (SDL_FPoint){ 0, 0 };
    verts[1].position = p1; verts[1].color = col; verts[1].tex_coord = (SDL_FPoint){ 0, 0 };
    verts[2].position = p2; verts[2].color = col; verts[2].tex_coord = (SDL_FPoint){ 0, 0 };
    SDL_RenderGeometry(renderer, NULL, verts, 3, NULL, 0);
}

/* Same technique as data_editor.c's own draw_thick_line - duplicated rather
   than shared for the same one-small-helper reasoning that one gives. */
static void fill_thick_line(SDL_Renderer *renderer, float x0, float y0, float x1, float y1, float thickness, SDL_Color col) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = thickness * 0.5f;
    SDL_Vertex verts[4];
    SDL_FPoint p0 = { x0 + nx * hw, y0 + ny * hw };
    SDL_FPoint p1 = { x1 + nx * hw, y1 + ny * hw };
    SDL_FPoint p2 = { x1 - nx * hw, y1 - ny * hw };
    SDL_FPoint p3 = { x0 - nx * hw, y0 - ny * hw };
    verts[0].position = p0; verts[0].color = col; verts[0].tex_coord = (SDL_FPoint){ 0, 0 };
    verts[1].position = p1; verts[1].color = col; verts[1].tex_coord = (SDL_FPoint){ 0, 0 };
    verts[2].position = p2; verts[2].color = col; verts[2].tex_coord = (SDL_FPoint){ 0, 0 };
    verts[3].position = p3; verts[3].color = col; verts[3].tex_coord = (SDL_FPoint){ 0, 0 };
    int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(renderer, NULL, verts, 4, indices, 6);
}

static void draw_up_arrow_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    float cx = box->x + box->w * 0.5f;
    float top = box->y + box->h * 0.32f, bottom = box->y + box->h * 0.68f;
    float half_w = box->w * 0.22f;
    fill_triangle(renderer, (SDL_FPoint){ cx, top }, (SDL_FPoint){ cx - half_w, bottom }, (SDL_FPoint){ cx + half_w, bottom }, col);
}

static void draw_down_arrow_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    float cx = box->x + box->w * 0.5f;
    float top = box->y + box->h * 0.32f, bottom = box->y + box->h * 0.68f;
    float half_w = box->w * 0.22f;
    fill_triangle(renderer, (SDL_FPoint){ cx, bottom }, (SDL_FPoint){ cx - half_w, top }, (SDL_FPoint){ cx + half_w, top }, col);
}

/* Same fixed stroke weight as data_editor.c's own close-X (CLOSE_X_THICKNESS)
   instead of a thickness scaled off the button's own size - a proportional
   stroke made this read visibly heavier/thicker than Manage Data's close
   button despite the two being nearly the same size. */
static void draw_delete_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    float pad = box->w * 0.28f;
    fill_thick_line(renderer, box->x + pad, box->y + pad, box->x + box->w - pad, box->y + box->h - pad, CLOSE_X_THICKNESS, col);
    fill_thick_line(renderer, box->x + box->w - pad, box->y + pad, box->x + pad, box->y + box->h - pad, CLOSE_X_THICKNESS, col);
}

/* Preset palette offered when picking a layer color - green excluded, it's
   the HIGH signal color, so a layer using it would be visually ambiguous
   with an active signal wire whenever layer preview is on. */
static const SDL_Color k_presets[8] = {
    { 235, 220, 40, 255 },  /* yellow */
    { 60, 110, 235, 255 },  /* blue */
    { 235, 150, 40, 255 },  /* orange */
    { 170, 90, 235, 255 },  /* violet */
    { 220, 70, 70, 255 },   /* red */
    { 70, 200, 220, 255 },  /* cyan */
    { 230, 230, 230, 255 }, /* white */
    { 180, 120, 80, 255 },  /* brown */
};
#define PRESET_COUNT 8
#define PRESET_SWATCH 20

static int point_in(const SDL_Rect *r, int x, int y) {
    return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Blinking caret at the end of the currently-typed text, same 500ms on/off
   cadence as a normal OS text field - without this there's no visual sign
   the rename/new-layer field is actually accepting keystrokes right now. */
static void draw_text_cursor(SDL_Renderer *renderer, TTF_Font *font, const char *text, int text_x, int text_y) {
    if ((SDL_GetTicks() / 500) % 2 != 0) return;
    int tw = 0, th = 0;
    if (font != NULL) text_util_measure(font, text, &tw, &th);
    /* height deliberately fixed at 14, NOT the measured th - TTF_SizeUTF8
       reports the font's full ascent+descent line metric, which reserves
       headroom below the baseline for descenders (g/y/p) that most typed
       names never use. Using that full height made the cursor visibly
       overshoot past the bottom of the actual glyphs it sits next to,
       reading as "too low" - 14 matches the same assumed text height every
       other label in this panel already centers against (see the row
       number/"+ Add Layer" text below). */
    (void)th;
    SDL_SetRenderDrawColor(renderer, TEXT_COLOR.r, TEXT_COLOR.g, TEXT_COLOR.b, 255);
    SDL_RenderDrawLine(renderer, text_x + tw + 1, text_y, text_x + tw + 1, text_y + 14);
}

void layer_panel_init(LayerPanel *lp) {
    memset(lp, 0, sizeof(*lp));
    lp->color_popup_for = -1;
    lp->editing_pos = -1;
}

int layer_panel_is_editing(const LayerPanel *lp) {
    return lp->editing_pos != -1;
}

void layer_panel_render(SDL_Renderer *renderer, TTF_Font *font, LayerPanel *lp, const Circuit *circuit,
                         int active_layer_slot, int window_w, int panel_right_margin, int hover_x, int hover_y) {
    int n = circuit->layer_order_count;

    /* the name column grows to fit whatever's currently the widest text it
       has to show - every layer's name, plus the field actively being
       typed into (so the panel visibly widens as you type past the
       longest existing name instead of clipping) - rather than a fixed
       width that either wastes space or squeezes long names under the
       buttons. */
    int name_w = MIN_NAME_W;
    if (font != NULL) {
        for (int pos = 0; pos < n; pos++) {
            int slot = circuit->layer_order[pos];
            int tw = 0, th = 0;
            text_util_measure(font, circuit->layers[slot].name, &tw, &th);
            if (tw > name_w) name_w = tw;
        }
        if (lp->editing_pos != -1) {
            int tw = 0, th = 0;
            text_util_measure(font, lp->edit_buffer, &tw, &th);
            if (tw > name_w) name_w = tw;
        }
    }
    name_w += 6; /* breathing room past the exact glyph width, and room for the blinking cursor */

    int panel_w = ROW_PAD_X * 2 + SWATCH_SIZE + COL_GAP + NUM_W + COL_GAP + name_w + NAME_GAP + BUTTON_CLUSTER_W;
    int panel_h = HEADER_H + HEADER_ROW_GAP + (n + 1) * ROW_H;
    lp->panel_rect = (SDL_Rect){ window_w - panel_w - panel_right_margin, TASKBAR_HEIGHT, panel_w, panel_h };

    SDL_SetRenderDrawColor(renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, BG_COLOR.a);
    SDL_RenderFillRect(renderer, &lp->panel_rect);

    if (font != NULL) {
        int hw = 0, hh = 0;
        text_util_measure(font, "Layers", &hw, &hh);
        (void)hw;
        text_util_draw(renderer, font, "Layers", lp->panel_rect.x + ROW_PAD_X,
                        lp->panel_rect.y + (HEADER_H - hh) / 2, HEADER_COLOR);
    }

    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawLine(renderer, lp->panel_rect.x, lp->panel_rect.y + HEADER_H,
                        lp->panel_rect.x + lp->panel_rect.w, lp->panel_rect.y + HEADER_H);

    /* the button cluster's x is the same in every row regardless of name
       length, so it never drifts or overlaps whatever's to its left */
    int right_buttons_x = lp->panel_rect.x + panel_w - ROW_PAD_X - BUTTON_CLUSTER_W;

    int y = lp->panel_rect.y + HEADER_H + HEADER_ROW_GAP;
    for (int pos = 0; pos < n; pos++) {
        int slot = circuit->layer_order[pos];
        const Layer *l = &circuit->layers[slot];
        SDL_Rect row = { lp->panel_rect.x, y, panel_w, ROW_H };
        lp->row_rects[pos] = row;

        if (slot == active_layer_slot) {
            SDL_SetRenderDrawColor(renderer, ROW_BG_ACTIVE.r, ROW_BG_ACTIVE.g, ROW_BG_ACTIVE.b, 255);
            SDL_RenderFillRect(renderer, &row);
        }

        int x = row.x + ROW_PAD_X;

        SDL_Rect swatch = { x, row.y + (ROW_H - SWATCH_SIZE) / 2, SWATCH_SIZE, SWATCH_SIZE };
        lp->swatch_rects[pos] = swatch;
        SDL_SetRenderDrawColor(renderer, l->color_r, l->color_g, l->color_b, 255);
        SDL_RenderFillRect(renderer, &swatch);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &swatch);
        x += SWATCH_SIZE + COL_GAP;

        if (font != NULL) {
            char numbuf[4];
            snprintf(numbuf, sizeof(numbuf), "%d", pos + 1);
            text_util_draw(renderer, font, numbuf, x, row.y + (ROW_H - 14) / 2, DIM_TEXT_COLOR);
        }
        x += NUM_W + COL_GAP;

        int this_name_w = right_buttons_x - NAME_GAP - x;
        if (this_name_w < 0) this_name_w = 0;
        SDL_Rect name_rect = { x, row.y + 2, this_name_w, ROW_H - 4 };
        lp->name_rects[pos] = name_rect;
        if (lp->editing_pos == pos) {
            SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
            SDL_RenderFillRect(renderer, &name_rect);
            SDL_SetRenderDrawColor(renderer, EDIT_HIGHLIGHT_COLOR.r, EDIT_HIGHLIGHT_COLOR.g, EDIT_HIGHLIGHT_COLOR.b, 255);
            SDL_RenderDrawRect(renderer, &name_rect);
            if (font != NULL) {
                text_util_draw(renderer, font, lp->edit_buffer, name_rect.x + 4, name_rect.y + 3, TEXT_COLOR);
                draw_text_cursor(renderer, font, lp->edit_buffer, name_rect.x + 4, name_rect.y + 3);
            }
        } else if (font != NULL) {
            text_util_draw(renderer, font, l->name, name_rect.x + 2, name_rect.y + 3, TEXT_COLOR);
        }

        int bx = right_buttons_x;
        SDL_Rect up = { bx, row.y + (ROW_H - BTN_SIZE) / 2, BTN_SIZE, BTN_SIZE };
        bx += BTN_SIZE + BTN_GAP;
        SDL_Rect down = { bx, row.y + (ROW_H - BTN_SIZE) / 2, BTN_SIZE, BTN_SIZE };
        bx += BTN_SIZE + BTN_GAP;
        SDL_Rect del = { bx, row.y + (ROW_H - BTN_SIZE) / 2, BTN_SIZE, BTN_SIZE };
        lp->up_rects[pos] = up;
        lp->down_rects[pos] = down;
        lp->delete_rects[pos] = del;

        int up_enabled = pos > 0;
        int down_enabled = pos < n - 1;
        int can_delete = (l->role == LAYER_ROLE_NORMAL) && !circuit_layer_in_use(circuit, slot);

        SDL_Color up_bg = !up_enabled ? BUTTON_BG_DISABLED : (point_in(&up, hover_x, hover_y) ? BUTTON_BG_HOVER : BUTTON_BG);
        SDL_Color down_bg = !down_enabled ? BUTTON_BG_DISABLED : (point_in(&down, hover_x, hover_y) ? BUTTON_BG_HOVER : BUTTON_BG);
        SDL_Color del_bg = !can_delete ? BUTTON_BG_DISABLED : (point_in(&del, hover_x, hover_y) ? BUTTON_BG_HOVER : BUTTON_BG);
        SDL_SetRenderDrawColor(renderer, up_bg.r, up_bg.g, up_bg.b, 255);
        SDL_RenderFillRect(renderer, &up);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &up);
        SDL_SetRenderDrawColor(renderer, down_bg.r, down_bg.g, down_bg.b, 255);
        SDL_RenderFillRect(renderer, &down);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &down);
        SDL_SetRenderDrawColor(renderer, del_bg.r, del_bg.g, del_bg.b, 255);
        SDL_RenderFillRect(renderer, &del);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &del);

        draw_up_arrow_icon(renderer, &up, up_enabled ? BUTTON_ICON_COLOR : BUTTON_ICON_DISABLED_COLOR);
        draw_down_arrow_icon(renderer, &down, down_enabled ? BUTTON_ICON_COLOR : BUTTON_ICON_DISABLED_COLOR);
        draw_delete_icon(renderer, &del, can_delete ? BUTTON_ICON_COLOR : BUTTON_ICON_DISABLED_COLOR);

        y += ROW_H;
    }

    SDL_Rect add_row = { lp->panel_rect.x, y, panel_w, ROW_H };
    lp->add_row_rect = add_row;
    if (lp->editing_pos == LAYER_PANEL_EDITING_NEW) {
        SDL_Rect edit_rect = { add_row.x + ROW_PAD_X, add_row.y + 2, panel_w - ROW_PAD_X * 2, ROW_H - 4 };
        SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
        SDL_RenderFillRect(renderer, &edit_rect);
        SDL_SetRenderDrawColor(renderer, EDIT_HIGHLIGHT_COLOR.r, EDIT_HIGHLIGHT_COLOR.g, EDIT_HIGHLIGHT_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &edit_rect);
        if (font != NULL) {
            text_util_draw(renderer, font, lp->edit_buffer, edit_rect.x + 4, edit_rect.y + 3, TEXT_COLOR);
            draw_text_cursor(renderer, font, lp->edit_buffer, edit_rect.x + 4, edit_rect.y + 3);
        }
    } else {
        /* same button family as the row buttons above, not the (now
           removed) lighter row fill - "+ Add Layer" is itself a button */
        SDL_Color add_bg = point_in(&add_row, hover_x, hover_y) ? BUTTON_BG_HOVER : BUTTON_BG;
        SDL_SetRenderDrawColor(renderer, add_bg.r, add_bg.g, add_bg.b, 255);
        SDL_RenderFillRect(renderer, &add_row);
        if (font != NULL) text_util_draw(renderer, font, "+ Add Layer", add_row.x + ROW_PAD_X, add_row.y + (ROW_H - 14) / 2, TEXT_COLOR);
    }

    /* drawn last (on top of every row/add-row fill above) so it's always a
       crisp, unbroken frame - drawing it first let each row's own fill
       (spanning the same left/right x as the panel itself) paint straight
       over the border's side pixels, which read as the row "poking out" of
       the panel by 1px. */
    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, &lp->panel_rect);

    if (lp->color_popup_for >= 0 && lp->color_popup_for < n) {
        SDL_Rect anchor = lp->swatch_rects[lp->color_popup_for];
        int popup_w = PRESET_COUNT * (PRESET_SWATCH + 4) + 4;
        int popup_x = anchor.x;
        if (popup_x + popup_w > lp->panel_rect.x + panel_w) popup_x = lp->panel_rect.x + panel_w - popup_w;
        SDL_Rect popup = { popup_x, anchor.y + SWATCH_SIZE + 6, popup_w, PRESET_SWATCH + 8 };
        lp->color_popup_rect = popup;
        SDL_SetRenderDrawColor(renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, 255);
        SDL_RenderFillRect(renderer, &popup);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &popup);
        for (int i = 0; i < PRESET_COUNT; i++) {
            SDL_Rect sw = { popup.x + 4 + i * (PRESET_SWATCH + 4), popup.y + 4, PRESET_SWATCH, PRESET_SWATCH };
            SDL_SetRenderDrawColor(renderer, k_presets[i].r, k_presets[i].g, k_presets[i].b, 255);
            SDL_RenderFillRect(renderer, &sw);
            SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
            SDL_RenderDrawRect(renderer, &sw);
        }
    } else {
        lp->color_popup_rect = (SDL_Rect){ 0, 0, 0, 0 };
    }
}

int layer_panel_covers_point(const LayerPanel *lp, int x, int y) {
    if (point_in(&lp->panel_rect, x, y)) return 1;
    if (lp->color_popup_for >= 0 && point_in(&lp->color_popup_rect, x, y)) return 1;
    return 0;
}

/* If a rename/new-layer field is active and this click isn't on that same
   field, treat it as "click away" - commits a non-empty name (same as
   Enter) or just cancels an empty one, and swallows this click either way
   (so confirming a field never also fires whatever's underneath it). */
static LayerPanelClickResult commit_if_editing_elsewhere(LayerPanel *lp, Circuit *circuit, int x, int y) {
    if (lp->editing_pos == -1) return LAYER_PANEL_CLICK_MISS;

    int on_active_field = (lp->editing_pos == LAYER_PANEL_EDITING_NEW)
                               ? point_in(&lp->add_row_rect, x, y)
                               : (lp->editing_pos < MAX_LAYERS && point_in(&lp->name_rects[lp->editing_pos], x, y));
    if (on_active_field) return LAYER_PANEL_CLICK_MISS;

    LayerPanelClickResult result = LAYER_PANEL_CLICK_CONSUMED;
    if (lp->edit_len > 0) {
        if (lp->editing_pos == LAYER_PANEL_EDITING_NEW) {
            circuit_add_layer(circuit, lp->edit_buffer, k_presets[0].r, k_presets[0].g, k_presets[0].b);
        } else if (lp->editing_pos < circuit->layer_order_count) {
            int slot = circuit->layer_order[lp->editing_pos];
            snprintf(circuit->layers[slot].name, sizeof(circuit->layers[slot].name), "%s", lp->edit_buffer);
        }
        result = LAYER_PANEL_CLICK_CHANGED;
    }
    lp->editing_pos = -1;
    SDL_StopTextInput();
    return result;
}

LayerPanelClickResult layer_panel_handle_click(LayerPanel *lp, Circuit *circuit, int *active_layer_slot, int x, int y,
                                                int double_click) {
    LayerPanelClickResult commit_result = commit_if_editing_elsewhere(lp, circuit, x, y);
    if (commit_result != LAYER_PANEL_CLICK_MISS) return commit_result;

    int n = circuit->layer_order_count;

    if (lp->color_popup_for >= 0) {
        int pos = lp->color_popup_for;
        LayerPanelClickResult result = LAYER_PANEL_CLICK_CONSUMED;
        if (point_in(&lp->color_popup_rect, x, y)) {
            int rel_x = x - (lp->color_popup_rect.x + 4);
            int idx = rel_x / (PRESET_SWATCH + 4);
            if (idx >= 0 && idx < PRESET_COUNT && pos < n) {
                int slot = circuit->layer_order[pos];
                circuit->layers[slot].color_r = k_presets[idx].r;
                circuit->layers[slot].color_g = k_presets[idx].g;
                circuit->layers[slot].color_b = k_presets[idx].b;
                result = LAYER_PANEL_CLICK_CHANGED;
            }
        }
        lp->color_popup_for = -1;
        return result;
    }

    if (!point_in(&lp->panel_rect, x, y)) return LAYER_PANEL_CLICK_MISS;

    for (int pos = 0; pos < n; pos++) {
        int slot = circuit->layer_order[pos];

        if (point_in(&lp->swatch_rects[pos], x, y)) {
            *active_layer_slot = slot;
            lp->color_popup_for = pos;
            return LAYER_PANEL_CLICK_CONSUMED;
        }
        if (point_in(&lp->name_rects[pos], x, y)) {
            *active_layer_slot = slot;
            if (double_click) {
                lp->editing_pos = pos;
                snprintf(lp->edit_buffer, sizeof(lp->edit_buffer), "%s", circuit->layers[slot].name);
                lp->edit_len = (int)strlen(lp->edit_buffer);
                SDL_StartTextInput();
            }
            return LAYER_PANEL_CLICK_CONSUMED;
        }
        if (point_in(&lp->up_rects[pos], x, y)) {
            if (pos > 0) {
                circuit_move_layer(circuit, pos, -1);
                return LAYER_PANEL_CLICK_CHANGED;
            }
            return LAYER_PANEL_CLICK_CONSUMED;
        }
        if (point_in(&lp->down_rects[pos], x, y)) {
            if (pos < n - 1) {
                circuit_move_layer(circuit, pos, 1);
                return LAYER_PANEL_CLICK_CHANGED;
            }
            return LAYER_PANEL_CLICK_CONSUMED;
        }
        if (point_in(&lp->delete_rects[pos], x, y)) {
            if (circuit_remove_layer(circuit, pos)) {
                if (*active_layer_slot == slot) {
                    *active_layer_slot = (circuit->layer_order_count > 0) ? circuit->layer_order[0] : -1;
                }
                return LAYER_PANEL_CLICK_CHANGED;
            }
            return LAYER_PANEL_CLICK_CONSUMED;
        }
        /* clicking anywhere else on the row - the gaps between the
           controls above, not any one of them specifically - still picks
           this layer as active, same as the 1-9 keybinds do, just by
           clicking the row instead of memorizing its number */
        if (point_in(&lp->row_rects[pos], x, y)) {
            *active_layer_slot = slot;
            return LAYER_PANEL_CLICK_CONSUMED;
        }
    }

    if (point_in(&lp->add_row_rect, x, y)) {
        lp->editing_pos = LAYER_PANEL_EDITING_NEW;
        lp->edit_buffer[0] = '\0';
        lp->edit_len = 0;
        SDL_StartTextInput();
        return LAYER_PANEL_CLICK_CONSUMED;
    }

    return LAYER_PANEL_CLICK_CONSUMED; /* panel background/header */
}

void layer_panel_text_input(LayerPanel *lp, const char *text) {
    if (lp->editing_pos == -1) return;
    for (const char *p = text; *p != '\0' && lp->edit_len < LAYER_NAME_MAX_LEN; p++) {
        lp->edit_buffer[lp->edit_len++] = *p;
    }
    lp->edit_buffer[lp->edit_len] = '\0';
}

LayerPanelClickResult layer_panel_handle_key(LayerPanel *lp, Circuit *circuit, SDL_Scancode sc) {
    if (lp->editing_pos == -1) return LAYER_PANEL_CLICK_MISS;

    if (sc == SDL_SCANCODE_BACKSPACE) {
        if (lp->edit_len > 0) {
            lp->edit_len--;
            lp->edit_buffer[lp->edit_len] = '\0';
        }
        return LAYER_PANEL_CLICK_CONSUMED;
    }
    if (sc == SDL_SCANCODE_ESCAPE) {
        lp->editing_pos = -1;
        SDL_StopTextInput();
        return LAYER_PANEL_CLICK_CONSUMED;
    }
    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
        LayerPanelClickResult result = LAYER_PANEL_CLICK_CONSUMED;
        if (lp->edit_len > 0) {
            if (lp->editing_pos == LAYER_PANEL_EDITING_NEW) {
                circuit_add_layer(circuit, lp->edit_buffer, k_presets[0].r, k_presets[0].g, k_presets[0].b);
            } else if (lp->editing_pos < circuit->layer_order_count) {
                int slot = circuit->layer_order[lp->editing_pos];
                snprintf(circuit->layers[slot].name, sizeof(circuit->layers[slot].name), "%s", lp->edit_buffer);
            }
            result = LAYER_PANEL_CLICK_CHANGED;
        }
        lp->editing_pos = -1;
        SDL_StopTextInput();
        return result;
    }
    return LAYER_PANEL_CLICK_CONSUMED; /* swallow every other key while editing (e.g. 'w' shouldn't switch tools) */
}
