#ifndef ISONIC_TASKBAR_H
#define ISONIC_TASKBAR_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define TASKBAR_HEIGHT 44

/* TOOL_PLACE_IC is generic - which specific IC it places lives in
   App.place_ic_name (app.h), chosen from the Components dropdown below, not
   encoded in the Tool value itself. That's what lets the menu grow without
   touching this enum. */
typedef enum {
    TOOL_SELECT,
    TOOL_WIRE,
    TOOL_INPUT,
    TOOL_OUTPUT,
    TOOL_PLACE_IC,
    TOOL_COUNT
} Tool;

/* Categories shown in the Components dropdown, Falstad-style fold-up lists.
   Add a category here and label it in taskbar.c's k_category_labels, then
   tag entries in k_menu_items with it - the menu lays itself out from that
   data, nothing else needs to change. */
typedef enum {
    MENU_CAT_LOGIC_GATES,
    MENU_CAT_MULTIPLEXERS,
    MENU_CAT_DEMULTIPLEXERS,
    MENU_CAT_BUFFERS,
    MENU_CAT_LATCHES,
    MENU_CAT_COUNT
} MenuCategory;

#define MENU_MAX_ITEMS 32
#define MENU_MAX_ROWS (MENU_CAT_COUNT + MENU_MAX_ITEMS)

typedef struct {
    /* the 4 plain tool buttons plus the Components dropdown trigger, indexed
       exactly like the Tool enum - button_rects[TOOL_PLACE_IC] is the
       trigger button itself, not any specific IC */
    SDL_Rect button_rects[TOOL_COUNT];
    SDL_Texture *label_textures[TOOL_COUNT];

    /* category header textures come in expanded/collapsed pairs (the arrow
       glyph is baked into the label text, see taskbar.c) */
    SDL_Texture *category_textures[MENU_CAT_COUNT][2];
    SDL_Texture *item_textures[MENU_MAX_ITEMS];

    int menu_open;
    int category_expanded[MENU_CAT_COUNT];
} Taskbar;

typedef enum {
    TASKBAR_CLICK_NONE,     /* missed the taskbar/menu - caller should treat this as an ordinary canvas click */
    TASKBAR_CLICK_CONSUMED, /* landed on taskbar chrome (menu toggle, category fold) - nothing further to do */
    TASKBAR_CLICK_TOOL,     /* a plain tool button was clicked - see *out_tool */
    TASKBAR_CLICK_IC        /* a specific IC was chosen from the dropdown - see *out_ic_name */
} TaskbarClickKind;

void taskbar_init(Taskbar *tb);
/* Recomputes button rects; call whenever the window is resized. */
void taskbar_layout(Taskbar *tb, int window_w);
/* active_ic_name (may be NULL) highlights the matching dropdown row, same
   idea as active_tool highlighting its button. hover_x/hover_y (current
   mouse position) additionally lights up whichever dropdown row - category
   or item - the cursor is directly over, while the dropdown is open. */
void taskbar_render(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, Tool active_tool, const char *active_ic_name,
                     int hover_x, int hover_y);
/* Frees the cached label textures; call once at shutdown. */
void taskbar_shutdown(Taskbar *tb);

/* Handles a left click at (x,y): may select a plain tool, choose an IC from
   the dropdown, fold/unfold a category, or toggle the dropdown itself -
   mutates *tb accordingly and reports what (if anything) the caller needs to
   act on. Also closes the dropdown as a side effect on any click that lands
   outside it while it's open (still returning TASKBAR_CLICK_NONE in that
   case), same as any dropdown menu dismissing on a click elsewhere. */
TaskbarClickKind taskbar_handle_click(Taskbar *tb, int x, int y, Tool *out_tool, const char **out_ic_name);

/* True if (x,y) falls anywhere within the taskbar strip or (if open) the
   dropdown panel - used to swallow right/middle clicks there instead of
   letting them act on the circuit underneath. */
int taskbar_covers_point(const Taskbar *tb, int x, int y);

#endif
