#ifndef ISONIC_APP_H
#define ISONIC_APP_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "circuit.h"
#include "camera.h"
#include "taskbar.h"
#include "diagnostics.h"
#include "data_editor.h"
#include "layer_panel.h"
#include "settings.h"
#include "settings_panel.h"

/* Bounds both the anchor list begin_selection_drag builds (one slot per
   selected component pin, two per selected wire) AND the attached-item
   lists snapshot_drag_attachments/begin_wire_node_drag fill from it
   (drag_attach_wire_id/drag_attach_via_id/drag_node_wire_id/
   drag_node_via_id) - all sized off this one constant, see app.h below.
   Was 64, which silently truncated the ANCHOR list (component pins are
   gathered before wire endpoints in begin_selection_drag, so a selection
   with enough pins alone - a handful of ICs is plenty - could already fill
   every slot and leave zero room for any wire endpoint to become an
   anchor at all). A via has no way to move except by sitting on one of
   those anchors (see circuit.h's Via - it has no .selected of its own,
   unlike a component/wire/section/text label, which move directly instead
   of relying on this list) - so a selection large enough to hit that cap
   left every via in it silently stranded while everything else moved,
   easy to trigger by pasting then dragging a whole multi-component
   circuit (see input_handler.c's commit_paste) even though nothing was
   wrong with the paste itself. Bumped well past any realistic circuit's
   scale - the memory cost of six more int arrays this size is trivial.
   Bumped again (1024 -> 8192) alongside circuit.h's own MAX_COMPONENTS/
   MAX_WIRES increase - a "select all, then drag" on a schematic actually
   using that new headroom (hundreds of ICs) can need thousands of anchors
   at once, not just "a handful of ICs" worth. */
#define MAX_DRAG_ATTACHMENTS 8192
/* Plain constant rather than pulling <windows.h>'s MAX_PATH into this
   shared header - only platform_win32.c ever needs the real Windows
   definition, everything else just needs "big enough for a path". */
#define ISONIC_PATH_MAX 260
/* Screen-pixel hit-testing tolerance shared by input_handler.c (click/select) and
   app.c (the temporary "you'd connect here" highlight while dragging a wire). */
#define WIRE_HIT_TOLERANCE_PX 6.0f

/* What a Select-mode left-drag is currently moving. */
typedef enum {
    DRAG_NONE,
    DRAG_COMPONENT,  /* moving a whole IC body, see selected_component_id */
    DRAG_WIRE_BODY,  /* moving a whole wire (both endpoints), see drag_wire_id */
    DRAG_WIRE_NODE,  /* moving every wire endpoint coincident at one point */
    DRAG_SELECTION,  /* moving every currently-.selected component/wire/section/text label together, e.g. after a marquee select */
    DRAG_SECTION_BODY,   /* moving a whole Section rectangle, see selected_section_id */
    DRAG_SECTION_HANDLE, /* resizing one corner of a Section, see selected_section_id/drag_section_corner */
    DRAG_TEXT_LABEL      /* moving a Text Label, see selected_text_label_id */
} DragKind;

/* Which canvas-level text field (if any) currently owns the keyboard - see
   canvas_edit_kind below. Section-Labeling and Text Label share this one
   mechanism instead of each getting their own copy of layer_panel.c's
   rename-field machinery, since both are just "type a single line of text
   directly onto the canvas" with identical commit/cancel rules. The _NEW_
   kinds are for something just drawn/placed and not committed to the
   circuit at all yet (see pending_section_x0.. / pending_text_label_x/y
   below) - committing there calls circuit_add_section/circuit_add_text_label
   for the first time; an empty commit (or Escape) simply discards the
   pending geometry instead. The plain kinds are retyping an EXISTING one's
   label/text (canvas_edit_id is which). */
typedef enum {
    CANVAS_EDIT_NONE,
    CANVAS_EDIT_NEW_SECTION,
    CANVAS_EDIT_SECTION_LABEL,
    CANVAS_EDIT_NEW_TEXT_LABEL,
    CANVAS_EDIT_TEXT_LABEL
} CanvasEditKind;
/* Big enough for either buffer - TEXT_LABEL_MAX_LEN (96) is the larger of
   the two (see circuit.h). */
#define CANVAS_EDIT_BUF_LEN (TEXT_LABEL_MAX_LEN + 1)

/* Ctrl+C's clipboard: an arbitrary mix of components (ICs)/wires/sections/
   text labels/vias, each stored as a (dx,dy) offset from the copied
   selection's own bounding-box min corner - see copy_selection in
   input_handler.c. Pasting (see App.pasting below) just means adding
   whatever grid point the ghost/click is at to every stored offset -
   render_paste_ghost in app.c and commit_paste in input_handler.c are the
   only two places that ever need to do that math.

   A via is never independently selectable (circuit.h's Via has no
   .selected field) and never copied standalone - copy_selection only ever
   includes one that sits exactly on an endpoint of some OTHER, already-
   copied wire, same "moves along with what's actually being manipulated"
   rule drag_attach_via_id elsewhere already follows for dragging. Both a
   copied wire's and a copied via's layer_slot(s) are preserved as-is from
   the original (not remapped to whatever layer happens to be active at
   paste time) - falling back to the active layer (wire) or being dropped
   entirely (via, if either side's layer no longer exists) only in the rare
   case that layer was since removed - see commit_paste. */
/* Matches the underlying Circuit's own capacity exactly (same pattern
   CLIPBOARD_MAX_SECTIONS/CLIPBOARD_MAX_TEXT_LABELS already used) rather than
   some smaller "realistic selection" guess - a plain Ctrl+C only ever holds
   as much as is currently selected, but File > Import Schematic (see
   import_schematic in input_handler.c) stages an ENTIRE other file's
   component/wire/via lists in here at once, which can be as large as the
   circuit format itself allows. A smaller cap silently truncated whichever
   items didn't fit, with no warning - components/wires/vias past the cap
   just vanished, worst on a big multi-layer import (used to be 64/128/128
   here vs. the circuit's real 256/512/512). */
#define CLIPBOARD_MAX_COMPONENTS MAX_COMPONENTS
#define CLIPBOARD_MAX_WIRES MAX_WIRES
#define CLIPBOARD_MAX_VIAS MAX_VIAS
#define CLIPBOARD_MAX_SECTIONS MAX_SECTIONS
#define CLIPBOARD_MAX_TEXT_LABELS MAX_TEXT_LABELS

typedef struct {
    const IC_Def *ic_def;
    int dx, dy;
    int rotation;
} ClipboardComponent;

typedef struct {
    int from_dx, from_dy, to_dx, to_dy;
    WireKind kind;
    int input_value; /* only meaningful for WIRE_KIND_INPUT, see Wire.input_value */
    int layer_slot;
} ClipboardWire;

typedef struct {
    int dx, dy;
    int layer_slot_a, layer_slot_b;
} ClipboardVia;

typedef struct {
    int dx0, dy0, dx1, dy1;
    char label[SECTION_LABEL_MAX_LEN + 1];
} ClipboardSection;

typedef struct {
    int dx, dy;
    char text[TEXT_LABEL_MAX_LEN + 1];
} ClipboardTextLabel;

typedef struct {
    Circuit circuit;
    Camera camera;
    Taskbar taskbar;
    TTF_Font *font;       /* taskbar only, loaded at display size */
    TTF_Font *font_large; /* all component/wire labels, loaded oversized and scaled down for crisp zoom */

    Tool active_tool;
    /* which IC TOOL_PLACE_IC would place, chosen from the taskbar's
       Components dropdown - NULL until something's been picked. Points at a
       static string literal owned by taskbar.c's menu catalog, so no
       lifetime/ownership concerns. */
    const char *place_ic_name;

    int window_w, window_h;
    int running;

    /* selection (mutually exclusive) */
    int selected_component_id; /* -1 = none */
    int selected_wire_id;      /* -1 = none */
    int selected_section_id;     /* -1 = none - see DRAG_SECTION_BODY/HANDLE */
    int selected_text_label_id;  /* -1 = none - see DRAG_TEXT_LABEL */

    /* dragging with the left mouse button in Select mode - a component body,
       a whole wire body, or a wire node (every endpoint coincident at one
       point); see DragKind above */
    DragKind drag_kind;
    int drag_last_gx, drag_last_gy; /* previous frame's cursor grid cell, for computing per-frame deltas */
    int drag_wire_id;               /* which wire, only for DRAG_WIRE_BODY */

    /* DRAG_SELECTION: which single component/wire/section/text label was
       actually clicked to start the drag (-1 for whichever don't apply),
       and whether the drag ever moved anything. A plain click-and-release on
       an item that happens to be part of a bigger existing selection should
       still collapse the selection down to just that one item, same as
       clicking any other unselected item always has - only an actual drag
       should preserve and move the whole group. See finish_drag. */
    int drag_click_component_id, drag_click_wire_id, drag_click_section_id, drag_click_text_label_id;
    int drag_moved;

    /* DRAG_SECTION_HANDLE: which of the section's 4 corners is being
       dragged - 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right, see
       section_corner_screen_pos in render.h. */
    int drag_section_corner;

    /* DRAG_COMPONENT/DRAG_WIRE_BODY: wires whose endpoint coincided with one
       of the dragged thing's anchor points (pins, or the wire's own two ends)
       at drag-start; shifted by the same per-frame delta so moving something
       doesn't silently tear its connections (see plan Revision 1) */
    int drag_attach_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_attach_wire_end[MAX_DRAG_ATTACHMENTS]; /* 0 = from, 1 = to */
    int drag_attach_count;

    /* Vias behave like a tiny component pinned to a wire node - any via
       whose point coincides with one of the dragged thing's anchor points
       at drag-start moves along with it too, same idea as
       drag_attach_wire_id above but for a via (which has no "end", just
       one point) - see snapshot_drag_attachments/apply_drag_attachments. */
    int drag_attach_via_id[MAX_DRAG_ATTACHMENTS];
    int drag_attach_via_count;

    /* DRAG_WIRE_NODE: every wire endpoint exactly at the grabbed point, which
       all move together as "the node" - also temporarily marked .selected so
       they're visibly highlighted for the duration of the drag */
    int drag_node_wire_id[MAX_DRAG_ATTACHMENTS];
    int drag_node_wire_end[MAX_DRAG_ATTACHMENTS];
    int drag_node_count;

    /* any via sitting exactly at the grabbed node moves along with it too -
       see begin_wire_node_drag/DRAG_WIRE_NODE in input_handler.c */
    int drag_node_via_id[MAX_DRAG_ATTACHMENTS];
    int drag_node_via_count;

    /* panning the camera with the middle mouse button */
    int panning;

    /* rubber-band multi-select: a Select-mode left-drag started over empty
       space (no node/component/wire under the cursor) draws a box, and on
       release every component/wire fully enclosed by it - not merely touched
       - gets marked .selected, same as Windows Explorer. Screen pixels, not
       grid coords: the box is a pure screen-space overlay, so it's simplest
       to keep it in the same space it's drawn in and only convert to grid
       space once, at release, for the containment test. */
    int marquee_active;
    int marquee_start_mx, marquee_start_my;
    int marquee_cur_mx, marquee_cur_my;

    /* TOOL_SECTION drag-to-draw, started on a left-button-down while
       TOOL_SECTION is active anywhere on the canvas (no node/component/wire
       hit-test to fall through first - Section-Labeling always draws,
       there's nothing else a click could mean in this tool). Unlike the
       marquee box above, both corners are converted to grid coordinates
       immediately (start once at button-down, cur again on every
       following motion event) instead of staying in screen space until
       release - a section is a grid object, so its start corner has to stay
       pinned to the same grid point for the rest of the drag. Tracking it
       as a screen pixel instead used to mean any zoom mid-drag (which
       shifts pan_x/pan_y to keep the CURSOR anchored, not that unrelated
       pixel) silently reinterpreted that same pixel as a different grid
       point every time it was reconverted, making the rectangle's start
       corner visibly drift on the grid despite the mouse never having
       moved. Released -> handed to canvas_edit_kind (CANVAS_EDIT_NEW_SECTION)
       rather than added to the circuit directly - see finish_section_draw in
       input_handler.c. */
    int section_dragging;
    int section_drag_start_gx, section_drag_start_gy;
    int section_drag_cur_gx, section_drag_cur_gy;

    /* Canvas-level text editing shared by Section-Labeling and Text Label -
       see CanvasEditKind above. canvas_edit_id is which circuit->sections[]/
       text_labels[] index is being retyped, for the two "existing" kinds.
       pending_section_x0.. / pending_text_label_x/y hold the just-drawn/
       placed geometry for the two "_NEW_" kinds, which don't exist in the
       circuit at all until this commits with non-empty text. */
    CanvasEditKind canvas_edit_kind;
    int canvas_edit_id;
    char canvas_edit_buf[CANVAS_EDIT_BUF_LEN];
    int canvas_edit_len;
    /* insertion point within canvas_edit_buf, in [0, canvas_edit_len] - Left/
       Right move it, typed text inserts at it (not always at the end), and
       Backspace erases the character just before it. Reset to canvas_edit_len
       (end of whatever initial text there is, matching every other text-field
       convention in this app) each time an edit starts - see the 4 call sites
       that set canvas_edit_kind. */
    int canvas_edit_cursor;
    int pending_section_x0, pending_section_y0, pending_section_x1, pending_section_y1;
    int pending_text_label_x, pending_text_label_y;

    /* Ctrl+C copy: snapshots the current selection (or, if nothing's
       selected, whatever single thing is directly under the cursor) into
       the clipboard_* arrays above - see copy_selection in input_handler.c.
       `pasting` is true while a copied selection is following the cursor as
       a ghost, waiting to be committed - Ctrl+C and Ctrl+V both just (re-)
       arm it the same way. A lone copied IC (clipboard_is_single_ic) keeps
       behaving exactly like a Components-menu pick always has: every click
       places another copy, looping until Escape/a tool switch cancels it
       (see app_pending_place_ic, and handle_left_click's place_def branch).
       Anything else - a lone wire, a lone Section/Text Label, or more than
       one item of any kind/mix - is a one-shot paste instead: a single
       click commits the whole thing at once and ends `pasting` immediately,
       leaving the just-placed copy selected (see commit_paste). */
    int clipboard_component_count;
    ClipboardComponent clipboard_components[CLIPBOARD_MAX_COMPONENTS];
    int clipboard_wire_count;
    ClipboardWire clipboard_wires[CLIPBOARD_MAX_WIRES];
    int clipboard_via_count;
    ClipboardVia clipboard_vias[CLIPBOARD_MAX_VIAS];
    int clipboard_section_count;
    ClipboardSection clipboard_sections[CLIPBOARD_MAX_SECTIONS];
    int clipboard_text_label_count;
    ClipboardTextLabel clipboard_text_labels[CLIPBOARD_MAX_TEXT_LABELS];
    int pasting;

    /* Rotation (0-3 quarter turns, see Component.rotation) the NEXT IC
       placed will get - applies to both an IC chosen from the Components
       menu and an in-progress paste, same "whichever placement is pending"
       resolution as app_pending_place_ic itself. R (KEYBIND_ROTATE)
       increments it while a placement is pending; reset to 0 whenever a new
       placement session starts (a fresh menu pick or Ctrl+C) so rotation
       never silently carries over from an unrelated previous part. */
    int place_rotation;

    /* dragging out a new wire between two arbitrary grid points - kind tags it
       as a plain wire or an Input/Output terminal (see WireKind in circuit.h) */
    int wiring;
    WireKind wiring_kind;
    int wire_from_gx, wire_from_gy;
    int wire_cursor_gx, wire_cursor_gy;

    /* recomputed every frame in app_update, right after sim_step so it sees
       this frame's settled pin values - see diagnostics.h */
    DiagnosticSet diagnostics;

    /* "Manage Data" EEPROM content editor - see data_editor.h */
    DataEditor data_editor;

    /* Multi-layer routing - see layer_panel.h/circuit.h. active_layer_slot
       (a circuit->layers[] slot index, not a layer_order[] position) is
       which layer a newly-drawn wire is placed on; the 1-9 keys and the
       panel's row clicks both just set this one field. */
    LayerPanel layer_panel;
    int active_layer_slot;

    /* Shift-hold previews every wire in its own layer's color instead of
       the plain gray/green signal color (see render_circuit's
       layer_preview param); Ctrl+Shift toggles that preview permanently on
       (CapsLock-like) until Ctrl+Shift again or a lone Shift tap.
       shift_press_was_chord distinguishes "Shift held as part of a
       Ctrl+Shift chord" from "a plain Shift tap/hold" so release only
       un-locks in the latter case - see input_handler.c. */
    int shift_held;
    int layer_preview_locked;
    int shift_press_was_chord;

    /* File/Settings - see settings.h/settings_panel.h, schematic_io.h,
       platform_win32.h. window is set once by app_init (passed down from
       main.c) so file dialogs/messageboxes and SDL_SetWindowTitle have an
       owner - nothing here ever destroys it, main.c still owns its
       lifetime. current_file_path[0] == '\0' means "untitled, never saved
       or opened". dirty tracks unsaved structural edits (see push_undo in
       input_handler.c) for the New/Open/Close confirm-discard prompt and
       autosave; it deliberately does NOT cover Manage Data EEPROM byte
       edits, which live outside the undo model entirely (see undo.h). */
    SDL_Window *window;
    Settings settings;
    SettingsPanel settings_panel;
    int dirty;
    char current_file_path[ISONIC_PATH_MAX];
    Uint32 last_autosave_tick;
} App;

void app_init(App *app, int window_w, int window_h, SDL_Window *window);
void app_shutdown(App *app);

/* Defined in input_handler.c */
void app_handle_event(App *app, const SDL_Event *event);

void app_update(App *app);
/* Draws one frame onto whatever render target is currently bound. Does not
   call SDL_RenderPresent - the caller (main.c) owns that, since it may render
   into an offscreen texture first for supersampled anti-aliasing. */
void app_render(App *app, SDL_Renderer *renderer);

/* The IC that would be placed by a click right now - either the one chosen
   from the taskbar's Components dropdown (TOOL_PLACE_IC, see place_ic_name
   above) or an in-progress Ctrl+C/Ctrl+V paste of a single copied IC and
   nothing else (see clipboard_is_single_ic below) - or NULL if neither
   applies. Centralizes which source wins so the existing single-item
   preview/footprint/infinite-placement-loop code (handle_left_click's
   place_def branch, app_render's pending_ic ghost) never needs to care which
   one it is, and keeps behaving exactly as it always has for that one case. */
const IC_Def *app_pending_place_ic(const App *app);

/* True if the clipboard holds exactly one item total and it's a component -
   see App.pasting's own comment above for what this distinction is for. */
int clipboard_is_single_ic(const App *app);
/* True if nothing's been copied at all (Ctrl+V has nothing to re-arm). */
int clipboard_is_empty(const App *app);

/* WIRE_HIT_TOLERANCE_PX converted to grid units at the current zoom - the
   tolerance every wire/pin proximity hit-test in input_handler.c and app.c uses. */
float app_wire_hit_tolerance(const App *app);

/* File menu actions - see taskbar.h's FileMenuItem and input_handler.c's
   dispatch. Unconditional (no dirty-check of their own) - New Window is the
   only File action with genuinely no discard prompt to gate (it starts a
   whole separate process); every other action's caller is expected to call
   app_confirm_discard_if_dirty first, same as this file's own
   app_close_window does. */
void app_new_schematic(App *app);
void app_load_from_file(App *app, const char *path);

/* Save to current_file_path, falling through to a Save-As dialog if there
   isn't one yet (untitled document). Returns 1 on success, 0 if the save
   failed or the user cancelled a Save-As it fell through to. */
int app_save_current(App *app);
/* Always prompts a Save-As dialog, regardless of current_file_path. Returns
   1 on success, 0 if cancelled or the save failed. */
int app_save_as(App *app);

/* The shared Save/Don't Save/Cancel gate New Schematic/Open File/Close
   Window all start with - 1 if the caller should proceed (nothing was
   dirty, or it was saved successfully first), 0 if the caller should abort
   (Cancel, or a Save/Save-As that failed or was itself cancelled). */
int app_confirm_discard_if_dirty(App *app);
/* Confirms discard if needed, then sets app->running = 0 (reuses the main
   loop's ordinary exit flag - no separate shutdown path). */
void app_close_window(App *app);

#endif
