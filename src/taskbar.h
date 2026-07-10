#ifndef ISONIC_TASKBAR_H
#define ISONIC_TASKBAR_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define TASKBAR_HEIGHT 44

typedef enum {
    TOOL_SELECT,
    TOOL_WIRE,
    TOOL_INPUT,
    TOOL_OUTPUT,
    TOOL_PLACE_SN7408,
    TOOL_PLACE_SN7432,
    TOOL_COUNT
} Tool;

typedef struct {
    SDL_Rect button_rects[TOOL_COUNT];
    SDL_Texture *label_textures[TOOL_COUNT]; /* lazily created on first render, then reused every frame */
} Taskbar;

void taskbar_init(Taskbar *tb);
/* Recomputes button rects; call whenever the window is resized. */
void taskbar_layout(Taskbar *tb, int window_w);
void taskbar_render(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, Tool active_tool);
/* Frees the cached label textures; call once at shutdown. */
void taskbar_shutdown(Taskbar *tb);

/* Returns the tool whose button contains (x,y), or -1 if none. Callers should
   only forward clicks here when y < TASKBAR_HEIGHT. */
int taskbar_hit_test(const Taskbar *tb, int x, int y);

#endif
