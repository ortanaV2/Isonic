#ifndef ISONIC_TASKBAR_H
#define ISONIC_TASKBAR_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define TASKBAR_HEIGHT 44

/* TOOL_PLACE_IC is generic - which specific IC it places lives in
   App.place_ic_name (app.h), chosen from the Components dropdown below, not
   encoded in the Tool value itself. That's what lets the menu grow without
   touching this enum.

   TOOL_SECTION/TOOL_TEXT_LABEL sit in their own visually separate group in
   the taskbar, to the right of Components (see GROUP_GAP in taskbar.c's
   taskbar_layout) - they're canvas annotation tools (see circuit.h's
   Section/TextLabel), not part of the main "place a circuit element" tool
   group, even though they share the same Tool/active_tool/set_active_tool
   machinery as everything else for simplicity. */
typedef enum {
    TOOL_SELECT,
    TOOL_WIRE,
    TOOL_VIA,   /* manually bridges two specific layers at a point - see circuit_add_via */
    TOOL_INPUT,
    TOOL_OUTPUT,
    TOOL_PLACE_IC,
    TOOL_SECTION,     /* drag out a Section-Labeling rectangle - see input_handler.c */
    TOOL_TEXT_LABEL,  /* click to place a plain Text Label */
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
    MENU_CAT_COUNTERS,
    MENU_CAT_TIMERS,
    MENU_CAT_MEMORY,
    MENU_CAT_ARITHMETIC,
    MENU_CAT_OBSOLETE, /* parts replaced by a newer catalog entry (see taskbar.c) - kept placeable for existing schematics, just out of the way of the current recommendation */
    MENU_CAT_COUNT
} MenuCategory;

#define MENU_MAX_ITEMS 32
#define MENU_MAX_ROWS (MENU_CAT_COUNT + MENU_MAX_ITEMS)

/* The File dropdown's fixed action list - unlike the Components dropdown
   this is flat (no categories) and never grows dynamically, so it's just an
   enum + a parallel label table (taskbar.c), not data-driven like
   k_menu_items. */
typedef enum {
    FILE_MENU_NEW_SCHEMATIC,
    FILE_MENU_NEW_WINDOW,
    FILE_MENU_OPEN,
    FILE_MENU_IMPORT,
    FILE_MENU_SAVE,
    FILE_MENU_SAVE_AS,
    FILE_MENU_CLOSE_WINDOW,
    FILE_MENU_COUNT
} FileMenuItem;

typedef struct {
    /* the 4 plain tool buttons plus the Components dropdown trigger, indexed
       exactly like the Tool enum - button_rects[TOOL_PLACE_IC] is the
       trigger button itself, not any specific IC */
    SDL_Rect button_rects[TOOL_COUNT];
    SDL_Texture *label_textures[TOOL_COUNT];

    /* File/Settings sit in their own group at the far left, separated from
       the tool buttons by a visible gap/divider - they're app-level actions,
       not canvas tools, so they live outside the Tool enum entirely. */
    SDL_Rect file_button_rect;
    SDL_Rect settings_button_rect;
    SDL_Texture *file_label_texture;
    SDL_Texture *settings_label_texture;

    /* File dropdown - structurally identical to the Components dropdown
       below (a small fixed list of clickable text rows anchored under its
       trigger button), just flat/uncategorized, so it stays here alongside
       that logic rather than becoming its own file. */
    int file_menu_open;
    SDL_Rect file_menu_row_rects[FILE_MENU_COUNT];
    SDL_Texture *file_menu_item_textures[FILE_MENU_COUNT];

    /* category header textures come in expanded/collapsed pairs (the arrow
       glyph is baked into the label text, see taskbar.c) */
    SDL_Texture *category_textures[MENU_CAT_COUNT][2];
    SDL_Texture *item_textures[MENU_MAX_ITEMS];

    int menu_open;
    int category_expanded[MENU_CAT_COUNT];
} Taskbar;

typedef enum {
    TASKBAR_CLICK_NONE,           /* missed the taskbar/menu - caller should treat this as an ordinary canvas click */
    TASKBAR_CLICK_CONSUMED,       /* landed on taskbar chrome (menu/dropdown toggle, category fold) - nothing further to do */
    TASKBAR_CLICK_TOOL,           /* a plain tool button was clicked - see *out_tool */
    TASKBAR_CLICK_IC,             /* a specific IC was chosen from the Components dropdown - see *out_ic_name */
    TASKBAR_CLICK_FILE_MENU_ITEM, /* a File dropdown row was chosen - see *out_file_item */
    TASKBAR_CLICK_SETTINGS        /* the Settings button was clicked - caller opens the Settings popup */
} TaskbarClickKind;

void taskbar_init(Taskbar *tb);
/* Recomputes button rects; call whenever the window is resized. */
void taskbar_layout(Taskbar *tb, int window_w);
/* Shifts button_rects[TOOL_SECTION..TOOL_TEXT_LABEL] to start manage_data_w
   (0 if Manage Data isn't showing this frame) past Components, so that group
   never overlaps the Manage Data button - call every frame, before
   taskbar_render, since Manage Data's width depends on the real font and on
   whether an AT28C64B happens to be selected right now, neither of which
   taskbar_layout (called only once, on init/resize) has access to. */
void taskbar_position_annotation_group(Taskbar *tb, int manage_data_w);
/* Just the bar itself (File/Settings/tool buttons, hover tooltips) - not
   either dropdown, see taskbar_render_dropdowns below for those. hover_x/
   hover_y (current mouse position) lights up whichever button the cursor is
   directly over. Both the "File" and "Settings" hover tooltips are
   suppressed together while the File dropdown is open (it visually sits
   right behind both buttons) - the separate, centered Settings popup does
   NOT suppress them, since it doesn't sit behind either button. */
void taskbar_render(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, Tool active_tool, int hover_x, int hover_y);
/* The File and (if open) Components dropdown panels, split out of
   taskbar_render above so the caller can draw them in a separate, later pass
   - dropdowns need to be the app's topmost layer (short of the Settings
   modal), but the taskbar bar itself renders first, before the Manage Data/
   Layers panels; call this AFTER those so an open dropdown never ends up
   tucked behind either one. active_ic_name (may be NULL) highlights the
   matching Components row, same idea as taskbar_render's active_tool
   highlighting its button; hover_x/hover_y light up whichever dropdown row -
   category or item - the cursor is directly over. */
void taskbar_render_dropdowns(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, const char *active_ic_name,
                               int hover_x, int hover_y);
/* Frees the cached label textures; call once at shutdown. */
void taskbar_shutdown(Taskbar *tb);

/* Handles a left click at (x,y): may select a plain tool, choose an IC from
   the Components dropdown, choose an action from the File dropdown, open
   Settings, fold/unfold a category, or toggle either dropdown itself -
   mutates *tb accordingly and reports what (if anything) the caller needs to
   act on. Also closes whichever dropdown is open as a side effect on any
   click that lands outside it (still returning TASKBAR_CLICK_NONE in that
   case), same as any dropdown menu dismissing on a click elsewhere. */
TaskbarClickKind taskbar_handle_click(Taskbar *tb, int x, int y, Tool *out_tool, const char **out_ic_name,
                                       FileMenuItem *out_file_item);

/* True if (x,y) falls anywhere within the taskbar strip or (if open) the
   dropdown panel - used to swallow right/middle clicks there instead of
   letting them act on the circuit underneath. */
int taskbar_covers_point(const Taskbar *tb, int x, int y);

#endif
