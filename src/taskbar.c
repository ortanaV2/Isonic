#include <stdio.h>
#include <string.h>
#include "taskbar.h"

static const char *k_labels[TOOL_COUNT] = {
    "Select",
    "Wire",
    "Input",
    "Output",
    "Components",
};

static const char *k_category_labels[MENU_CAT_COUNT] = {
    "Logic Gates",
    "Multiplexers",
    "Demultiplexers",
    "Buffers",
    "Latches",
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
    { MENU_CAT_LOGIC_GATES, "NOT (CD74HC04E)",  "CD74HC04E" },
    { MENU_CAT_LOGIC_GATES, "NAND (SN74HC00N)", "SN74HC00N" },
    { MENU_CAT_LOGIC_GATES, "NOR (SN74HC02N)",  "SN74HC02N" },
    { MENU_CAT_LOGIC_GATES, "XOR (SN74HC86N)",  "SN74HC86N" },
    { MENU_CAT_MULTIPLEXERS, "8:1 MUX (SN74HC151N)",       "SN74HC151N" },
    { MENU_CAT_MULTIPLEXERS, "Dual 4:1 MUX (SN74HC153N)",  "SN74HC153N" },
    { MENU_CAT_DEMULTIPLEXERS, "1:8 DEMUX (CD74HCT238E)",     "CD74HCT238E" },
    { MENU_CAT_DEMULTIPLEXERS, "Dual 1:4 DEMUX (CD4555BE)",   "CD4555BE" },
    { MENU_CAT_BUFFERS, "Tri-State Buffer (SN74HC244N)", "SN74HC244N" },
    { MENU_CAT_LATCHES, "D-Latch (TC74HC373APF)",        "TC74HC373APF" },
};
#define MENU_ITEM_COUNT ((int)(sizeof(k_menu_items) / sizeof(k_menu_items[0])))

#define BUTTON_PADDING_X 14
#define BUTTON_MARGIN 6
#define MENU_ROW_H 26
#define MENU_ROW_INDENT 16
#define MENU_PANEL_W 300

void taskbar_init(Taskbar *tb) {
    for (int i = 0; i < TOOL_COUNT; i++) tb->label_textures[i] = NULL;
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

void taskbar_layout(Taskbar *tb, int window_w) {
    (void)window_w;
    int x = BUTTON_MARGIN;
    for (int i = 0; i < TOOL_COUNT; i++) {
        int text_w = (int)(8 * strlen(k_labels[i]));
        int w = text_w + BUTTON_PADDING_X * 2;
        tb->button_rects[i].x = x;
        tb->button_rects[i].y = BUTTON_MARGIN;
        tb->button_rects[i].w = w;
        tb->button_rects[i].h = TASKBAR_HEIGHT - BUTTON_MARGIN * 2;
        x += w + BUTTON_MARGIN;
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

void taskbar_render(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, Tool active_tool, const char *active_ic_name,
                     int hover_x, int hover_y) {
    SDL_Rect bar = { 0, 0, 4096, TASKBAR_HEIGHT };
    SDL_SetRenderDrawColor(renderer, 40, 40, 44, 255);
    SDL_RenderFillRect(renderer, &bar);

    for (int i = 0; i < TOOL_COUNT; i++) {
        const SDL_Rect *r = &tb->button_rects[i];
        if (i == (int)active_tool) {
            SDL_SetRenderDrawColor(renderer, 70, 130, 200, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 60, 60, 66, 255);
        }
        SDL_RenderFillRect(renderer, r);
        SDL_SetRenderDrawColor(renderer, 20, 20, 22, 255);
        SDL_RenderDrawRect(renderer, r);

        SDL_Texture *label = get_label_texture(renderer, tb, font, i);
        if (label != NULL) {
            int tw = 0, th = 0;
            SDL_QueryTexture(label, NULL, NULL, &tw, &th);
            SDL_Rect dst = { r->x + (r->w - tw) / 2, r->y + (r->h - th) / 2, tw, th };
            SDL_RenderCopy(renderer, label, NULL, &dst);
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

TaskbarClickKind taskbar_handle_click(Taskbar *tb, int x, int y, Tool *out_tool, const char **out_ic_name) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        const SDL_Rect *r = &tb->button_rects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
            if (i == TOOL_PLACE_IC) {
                tb->menu_open = !tb->menu_open;
                return TASKBAR_CLICK_CONSUMED;
            }
            tb->menu_open = 0;
            *out_tool = (Tool)i;
            return TASKBAR_CLICK_TOOL;
        }
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
