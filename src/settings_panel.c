#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_panel.h"
#include "taskbar.h" /* TASKBAR_HEIGHT */
#include "text_util.h"

/* Palette duplicated verbatim from layer_panel.c/data_editor.c - deliberate,
   same reasoning as everywhere else in this codebase (see layer_panel.c's
   own comment on this). */
/* Fully opaque (255, not data_editor.c's 245) - drawn over the dimming scrim
   instead of the bright circuit canvas, so the same slight translucency that
   reads as "solid" there would still show a hint of the darkened scrim
   through it here, looking like the popup itself was see-through. */
static const SDL_Color BG_COLOR = { 32, 32, 36, 255 };
static const SDL_Color BORDER_COLOR = { 20, 20, 22, 255 };
static const SDL_Color HEADER_COLOR = { 190, 190, 196, 255 };
static const SDL_Color TEXT_COLOR = { 225, 225, 230, 255 };
static const SDL_Color DIM_TEXT_COLOR = { 140, 140, 146, 255 };
static const SDL_Color BUTTON_BG = { 50, 50, 56, 255 };
static const SDL_Color BUTTON_BG_HOVER = { 64, 64, 72, 255 };
static const SDL_Color EDIT_HIGHLIGHT_COLOR = { 90, 170, 255, 255 };
/* The real "selected/active" blue used everywhere else (taskbar.c's active
   tool button, data_editor.c's BUTTON_COLOR_ACTIVE) - NOT the same as
   EDIT_HIGHLIGHT_COLOR above, which is a brighter blue meaning "this text
   field is focused," a different thing. The Corner toggle's selected option
   and Save's primary-action look both use this one. */
static const SDL_Color ACTIVE_COLOR = { 70, 130, 200, 255 };
static const SDL_Color ACTIVE_COLOR_HOVER = { 82, 145, 215, 255 };
/* Warm red/orange for a keybind box actively listening for a keypress -
   visually distinct from EDIT_HIGHLIGHT_COLOR's "focused/editing" blue, so
   "I'm waiting for you to press a key" doesn't read as just another text
   field. */
static const SDL_Color CAPTURING_COLOR = { 230, 90, 60, 255 };
#define CLOSE_X_THICKNESS 1.8f /* matches layer_panel.c/data_editor.c's own close-X stroke weight exactly */

#define MODAL_W 460
#define HEADER_H 28  /* == data_editor.c/layer_panel.c's own HEADER_H exactly */
#define ROW_H 30
#define ROW_PAD_X 8  /* == data_editor.c/layer_panel.c's own ROW_PAD_X exactly */
#define SECTION_GAP 12
#define FOOTER_GAP 16
#define FOOTER_H 30
#define BTN_H 22
#define CLOSE_BOX 20 /* == data_editor.c's close_size (its own HEADER_H(28) - 8) */
#define FIELD_W 56
#define TOGGLE_W 56
#define KEY_BOX_W 130
#define FOOTER_BTN_MIN_W 70
#define FOOTER_BTN_PAD_X 16

static int point_in(const SDL_Rect *r, int x, int y) {
    return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Sized to fit the label rather than a fixed width shared by every footer
   button - "Reset Default" is a lot longer than "Save", and a shared fixed
   width either wastes space or crowds the longer one. */
static int button_w_for_label(TTF_Font *font, const char *label) {
    int tw = 0, th = 0;
    if (font != NULL) text_util_measure(font, label, &tw, &th);
    int w = tw + FOOTER_BTN_PAD_X * 2;
    return w < FOOTER_BTN_MIN_W ? FOOTER_BTN_MIN_W : w;
}

/* Same technique as layer_panel.c's own fill_thick_line/draw_delete_icon -
   duplicated rather than shared, same one-small-helper reasoning. */
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

/* Fixed 5px inset, same as data_editor.c's own close-X (not a proportional
   pad) - together with CLOSE_BOX matching its close_size, this makes the two
   buttons pixel-identical. */
static void draw_close_icon(SDL_Renderer *renderer, const SDL_Rect *box, SDL_Color col) {
    fill_thick_line(renderer, box->x + 5, box->y + 5, box->x + box->w - 5, box->y + box->h - 5, CLOSE_X_THICKNESS, col);
    fill_thick_line(renderer, box->x + box->w - 5, box->y + 5, box->x + 5, box->y + box->h - 5, CLOSE_X_THICKNESS, col);
}

/* Same blinking-caret technique as layer_panel.c's draw_text_cursor - fixed
   14px height (not the font's full line-metric height), same reasoning. */
static void draw_text_cursor(SDL_Renderer *renderer, TTF_Font *font, const char *text, int text_x, int text_y) {
    if ((SDL_GetTicks() / 500) % 2 != 0) return;
    int tw = 0, th = 0;
    if (font != NULL) text_util_measure(font, text, &tw, &th);
    (void)th;
    SDL_SetRenderDrawColor(renderer, TEXT_COLOR.r, TEXT_COLOR.g, TEXT_COLOR.b, 255);
    SDL_RenderDrawLine(renderer, text_x + tw + 1, text_y, text_x + tw + 1, text_y + 14);
}

static void commit_autosave_field(SettingsPanel *sp) {
    sp->working.autosave_minutes = atoi(sp->autosave_edit_buf);
    if (sp->working.autosave_minutes < 0) sp->working.autosave_minutes = 0;
    sp->autosave_editing = 0;
    SDL_StopTextInput();
}

void settings_panel_init(SettingsPanel *sp, Settings *live) {
    memset(sp, 0, sizeof(*sp));
    sp->live = live;
    sp->working = *live;
    sp->capturing_action = -1;
}

void settings_panel_open(SettingsPanel *sp) {
    sp->open = 1;
    sp->working = *sp->live; /* reseed from whatever's actually live - discards any never-saved edits from last time */
    sp->autosave_editing = 0;
    sp->capturing_action = -1;
}

static void draw_button(SDL_Renderer *renderer, TTF_Font *font, const SDL_Rect *r, const char *label,
                         int active, int hovered) {
    SDL_Color bg;
    if (active) bg = hovered ? ACTIVE_COLOR_HOVER : ACTIVE_COLOR;
    else bg = hovered ? BUTTON_BG_HOVER : BUTTON_BG;
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
    SDL_RenderFillRect(renderer, r);
    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, r);
    if (font != NULL && label != NULL) {
        int tw = 0, th = 0;
        text_util_measure(font, label, &tw, &th);
        text_util_draw(renderer, font, label, r->x + (r->w - tw) / 2, r->y + (r->h - 14) / 2, TEXT_COLOR);
    }
}

void settings_panel_render(SDL_Renderer *renderer, TTF_Font *font, SettingsPanel *sp, int window_w, int window_h,
                            int hover_x, int hover_y) {
    if (!sp->open) return;

    /* Content height is computed the same way panel_h is in
       layer_panel_render - built up from the rows actually drawn, rather
       than a hardcoded guess that could drift out of sync with the layout
       below. */
    int content_h = HEADER_H + ROW_H * 4 + SECTION_GAP + ROW_H + ROW_H * KEYBIND_ACTION_COUNT + FOOTER_GAP + FOOTER_H + ROW_PAD_X;
    int modal_w = MODAL_W;
    int modal_x = (window_w - modal_w) / 2;
    if (modal_x < 8) modal_x = 8;
    int modal_y = (window_h - content_h) / 2;
    if (modal_y < TASKBAR_HEIGHT + 8) modal_y = TASKBAR_HEIGHT + 8;
    sp->modal_rect = (SDL_Rect){ modal_x, modal_y, modal_w, content_h };

    /* Dimming scrim over the whole window - the one thing that visually
       marks this as a true modal rather than another docked panel like
       Layers/Manage Data. */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
    SDL_Rect scrim = { 0, 0, window_w, window_h };
    SDL_RenderFillRect(renderer, &scrim);

    SDL_SetRenderDrawColor(renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, BG_COLOR.a);
    SDL_RenderFillRect(renderer, &sp->modal_rect);

    if (font != NULL) {
        int hh = 0, hw = 0;
        text_util_measure(font, "Settings", &hw, &hh);
        text_util_draw(renderer, font, "Settings", sp->modal_rect.x + ROW_PAD_X,
                        sp->modal_rect.y + (HEADER_H - hh) / 2, HEADER_COLOR);
    }
    /* same 6px right-margin/4px top-margin data_editor.c's own close button uses */
    sp->close_rect = (SDL_Rect){ sp->modal_rect.x + modal_w - CLOSE_BOX - 6, sp->modal_rect.y + 4, CLOSE_BOX, CLOSE_BOX };
    SDL_Color close_bg = point_in(&sp->close_rect, hover_x, hover_y) ? BUTTON_BG_HOVER : BUTTON_BG;
    SDL_SetRenderDrawColor(renderer, close_bg.r, close_bg.g, close_bg.b, 255);
    SDL_RenderFillRect(renderer, &sp->close_rect);
    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, &sp->close_rect);
    draw_close_icon(renderer, &sp->close_rect, TEXT_COLOR);

    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawLine(renderer, sp->modal_rect.x, sp->modal_rect.y + HEADER_H,
                        sp->modal_rect.x + modal_w, sp->modal_rect.y + HEADER_H);

    int right_x = sp->modal_rect.x + modal_w - ROW_PAD_X;
    int y = sp->modal_rect.y + HEADER_H;

    /* Autosave frequency */
    {
        SDL_Rect row = { sp->modal_rect.x, y, modal_w, ROW_H };
        if (font != NULL) {
            text_util_draw(renderer, font, "Autosave (minutes, 0 = disabled)", row.x + ROW_PAD_X, row.y + (ROW_H - 14) / 2, TEXT_COLOR);
        }
        sp->autosave_field_rect = (SDL_Rect){ right_x - FIELD_W, row.y + (ROW_H - BTN_H) / 2, FIELD_W, BTN_H };
        int editing = sp->autosave_editing;
        SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
        SDL_RenderFillRect(renderer, &sp->autosave_field_rect);
        SDL_Color field_border = editing ? EDIT_HIGHLIGHT_COLOR : BORDER_COLOR;
        SDL_SetRenderDrawColor(renderer, field_border.r, field_border.g, field_border.b, 255);
        SDL_RenderDrawRect(renderer, &sp->autosave_field_rect);
        if (font != NULL) {
            char buf[16];
            const char *text;
            if (editing) {
                text = sp->autosave_edit_buf;
            } else {
                snprintf(buf, sizeof(buf), "%d", sp->working.autosave_minutes);
                text = buf;
            }
            text_util_draw(renderer, font, text, sp->autosave_field_rect.x + 6, sp->autosave_field_rect.y + 4, TEXT_COLOR);
            if (editing) draw_text_cursor(renderer, font, text, sp->autosave_field_rect.x + 6, sp->autosave_field_rect.y + 4);
        }
        y += ROW_H;
    }

    /* Layer-Panel Corner */
    {
        SDL_Rect row = { sp->modal_rect.x, y, modal_w, ROW_H };
        if (font != NULL) {
            text_util_draw(renderer, font, "Layers Panel Position", row.x + ROW_PAD_X, row.y + (ROW_H - 14) / 2, TEXT_COLOR);
        }
        sp->corner_right_rect = (SDL_Rect){ right_x - TOGGLE_W, row.y + (ROW_H - BTN_H) / 2, TOGGLE_W, BTN_H };
        sp->corner_left_rect = (SDL_Rect){ sp->corner_right_rect.x - 4 - TOGGLE_W, row.y + (ROW_H - BTN_H) / 2, TOGGLE_W, BTN_H };
        draw_button(renderer, font, &sp->corner_left_rect, "Left", sp->working.layer_panel_anchor_left, point_in(&sp->corner_left_rect, hover_x, hover_y));
        draw_button(renderer, font, &sp->corner_right_rect, "Right", !sp->working.layer_panel_anchor_left, point_in(&sp->corner_right_rect, hover_x, hover_y));
        y += ROW_H;
    }

    /* Diagnostics chip stack (bottom-left of the canvas) */
    {
        SDL_Rect row = { sp->modal_rect.x, y, modal_w, ROW_H };
        if (font != NULL) {
            text_util_draw(renderer, font, "Error/Warning Chips", row.x + ROW_PAD_X, row.y + (ROW_H - 14) / 2, TEXT_COLOR);
        }
        sp->diag_chips_off_rect = (SDL_Rect){ right_x - TOGGLE_W, row.y + (ROW_H - BTN_H) / 2, TOGGLE_W, BTN_H };
        sp->diag_chips_on_rect = (SDL_Rect){ sp->diag_chips_off_rect.x - 4 - TOGGLE_W, row.y + (ROW_H - BTN_H) / 2, TOGGLE_W, BTN_H };
        draw_button(renderer, font, &sp->diag_chips_on_rect, "On", sp->working.diag_chips_enabled, point_in(&sp->diag_chips_on_rect, hover_x, hover_y));
        draw_button(renderer, font, &sp->diag_chips_off_rect, "Off", !sp->working.diag_chips_enabled, point_in(&sp->diag_chips_off_rect, hover_x, hover_y));
        y += ROW_H;
    }

    /* Hover description tooltip for a chip or a flagged wire/pin on canvas -
       independent of the chip stack itself above, since the highlighted
       wires/pins on canvas can still be hovered for a description even with
       the chip stack turned off. */
    {
        SDL_Rect row = { sp->modal_rect.x, y, modal_w, ROW_H };
        if (font != NULL) {
            text_util_draw(renderer, font, "Error/Warning Hover Descriptions", row.x + ROW_PAD_X, row.y + (ROW_H - 14) / 2, TEXT_COLOR);
        }
        sp->diag_hover_off_rect = (SDL_Rect){ right_x - TOGGLE_W, row.y + (ROW_H - BTN_H) / 2, TOGGLE_W, BTN_H };
        sp->diag_hover_on_rect = (SDL_Rect){ sp->diag_hover_off_rect.x - 4 - TOGGLE_W, row.y + (ROW_H - BTN_H) / 2, TOGGLE_W, BTN_H };
        draw_button(renderer, font, &sp->diag_hover_on_rect, "On", sp->working.diag_hover_enabled, point_in(&sp->diag_hover_on_rect, hover_x, hover_y));
        draw_button(renderer, font, &sp->diag_hover_off_rect, "Off", !sp->working.diag_hover_enabled, point_in(&sp->diag_hover_off_rect, hover_x, hover_y));
        y += ROW_H;
    }

    y += SECTION_GAP;
    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawLine(renderer, sp->modal_rect.x, y, sp->modal_rect.x + modal_w, y);
    if (font != NULL) {
        text_util_draw(renderer, font, "Keybind Settings", sp->modal_rect.x + ROW_PAD_X, y + (ROW_H - 14) / 2, HEADER_COLOR);
    }
    y += ROW_H;

    for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
        SDL_Rect row = { sp->modal_rect.x, y, modal_w, ROW_H };
        if (font != NULL) {
            text_util_draw(renderer, font, keybind_action_label((KeybindAction)i), row.x + ROW_PAD_X, row.y + (ROW_H - 14) / 2, TEXT_COLOR);
        }
        SDL_Rect box = { right_x - KEY_BOX_W, row.y + (ROW_H - BTN_H) / 2, KEY_BOX_W, BTN_H };
        sp->keybind_box_rects[i] = box;
        int capturing = (sp->capturing_action == i);
        SDL_Color box_bg = capturing ? CAPTURING_COLOR : (point_in(&box, hover_x, hover_y) ? BUTTON_BG_HOVER : BUTTON_BG);
        SDL_SetRenderDrawColor(renderer, box_bg.r, box_bg.g, box_bg.b, 255);
        SDL_RenderFillRect(renderer, &box);
        SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
        SDL_RenderDrawRect(renderer, &box);
        if (font != NULL) {
            const char *label = capturing ? "Press a key..." : SDL_GetScancodeName(sp->working.keybind[i]);
            int tw = 0, th = 0;
            text_util_measure(font, label, &tw, &th);
            SDL_Color label_col = capturing ? TEXT_COLOR : DIM_TEXT_COLOR;
            text_util_draw(renderer, font, label, box.x + (box.w - tw) / 2, box.y + (box.h - 14) / 2, label_col);
        }
        y += ROW_H;
    }

    y += FOOTER_GAP;
    int save_w = button_w_for_label(font, "Save");
    int reset_w = button_w_for_label(font, "Reset Default");
    sp->save_button_rect = (SDL_Rect){ right_x - save_w, y, save_w, FOOTER_H };
    sp->reset_button_rect = (SDL_Rect){ sp->save_button_rect.x - 10 - reset_w, y, reset_w, FOOTER_H };
    draw_button(renderer, font, &sp->reset_button_rect, "Reset Default", 0, point_in(&sp->reset_button_rect, hover_x, hover_y));
    /* Save reads as the primary action - the accent fill it'd get from
       draw_button's own "active" styling (the real selected/active blue,
       ACTIVE_COLOR) is a deliberate reuse, not a real toggle state. */
    draw_button(renderer, font, &sp->save_button_rect, "Save", 1, point_in(&sp->save_button_rect, hover_x, hover_y));

    SDL_SetRenderDrawColor(renderer, BORDER_COLOR.r, BORDER_COLOR.g, BORDER_COLOR.b, 255);
    SDL_RenderDrawRect(renderer, &sp->modal_rect);
}

int settings_panel_covers_point(const SettingsPanel *sp, int x, int y) {
    (void)x; (void)y;
    return sp->open; /* fully modal - covers the whole window while open */
}

SettingsPanelClickResult settings_panel_handle_click(SettingsPanel *sp, int x, int y) {
    if (!sp->open) return SETTINGS_PANEL_CLICK_MISS;

    if (sp->autosave_editing && !point_in(&sp->autosave_field_rect, x, y)) {
        commit_autosave_field(sp);
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (sp->capturing_action != -1) {
        sp->capturing_action = -1; /* a mouse click isn't a valid keybind - cancel and swallow this click */
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }

    if (point_in(&sp->close_rect, x, y)) {
        sp->open = 0;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (!point_in(&sp->modal_rect, x, y)) {
        /* click-away does NOT close it - only the X (or Escape) does. Still
           fully swallowed though, same as everywhere else on the modal -
           the whole point of a modal is that nothing behind it reacts to
           the cursor at all while it's up. */
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }

    if (point_in(&sp->autosave_field_rect, x, y)) {
        sp->autosave_editing = 1;
        snprintf(sp->autosave_edit_buf, sizeof(sp->autosave_edit_buf), "%d", sp->working.autosave_minutes);
        sp->autosave_edit_len = (int)strlen(sp->autosave_edit_buf);
        SDL_StartTextInput();
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->corner_left_rect, x, y)) {
        sp->working.layer_panel_anchor_left = 1;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->corner_right_rect, x, y)) {
        sp->working.layer_panel_anchor_left = 0;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->diag_chips_on_rect, x, y)) {
        sp->working.diag_chips_enabled = 1;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->diag_chips_off_rect, x, y)) {
        sp->working.diag_chips_enabled = 0;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->diag_hover_on_rect, x, y)) {
        sp->working.diag_hover_enabled = 1;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->diag_hover_off_rect, x, y)) {
        sp->working.diag_hover_enabled = 0;
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
        if (point_in(&sp->keybind_box_rects[i], x, y)) {
            sp->capturing_action = i;
            return SETTINGS_PANEL_CLICK_CONSUMED;
        }
    }
    if (point_in(&sp->save_button_rect, x, y)) {
        *sp->live = sp->working;
        settings_save(sp->live);
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }
    if (point_in(&sp->reset_button_rect, x, y)) {
        settings_defaults(&sp->working);
        return SETTINGS_PANEL_CLICK_CONSUMED;
    }

    return SETTINGS_PANEL_CLICK_CONSUMED; /* modal background/header - swallow, it's modal */
}

int settings_panel_is_capturing_key(const SettingsPanel *sp) {
    return sp->open && (sp->capturing_action != -1 || sp->autosave_editing);
}

void settings_panel_text_input(SettingsPanel *sp, const char *text) {
    if (!sp->autosave_editing) return;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') continue; /* digits only */
        if (sp->autosave_edit_len < (int)sizeof(sp->autosave_edit_buf) - 1) {
            sp->autosave_edit_buf[sp->autosave_edit_len++] = *p;
            sp->autosave_edit_buf[sp->autosave_edit_len] = '\0';
        }
    }
}

void settings_panel_handle_key(SettingsPanel *sp, SDL_Scancode sc) {
    if (sp->capturing_action != -1) {
        if (sc == SDL_SCANCODE_ESCAPE) {
            sp->capturing_action = -1;
            return;
        }
        /* bare modifier presses aren't usable single-key bindings on their
           own - ignore them so reaching for e.g. a Ctrl+key chord doesn't
           "capture" as just Ctrl the instant the box opens. */
        if (sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL ||
            sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT ||
            sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT) {
            return;
        }
        int action = sp->capturing_action;
        for (int i = 0; i < KEYBIND_ACTION_COUNT; i++) {
            if (i != action && sp->working.keybind[i] == sc) {
                sp->working.keybind[i] = sp->working.keybind[action]; /* swap */
                break;
            }
        }
        sp->working.keybind[action] = sc;
        sp->capturing_action = -1;
        return;
    }

    if (sp->autosave_editing) {
        if (sc == SDL_SCANCODE_BACKSPACE) {
            if (sp->autosave_edit_len > 0) {
                sp->autosave_edit_len--;
                sp->autosave_edit_buf[sp->autosave_edit_len] = '\0';
            }
        } else if (sc == SDL_SCANCODE_ESCAPE) {
            sp->autosave_editing = 0;
            SDL_StopTextInput();
        } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
            commit_autosave_field(sp);
        }
        /* every other key (digits arrive via settings_panel_text_input) is swallowed */
    }
}
