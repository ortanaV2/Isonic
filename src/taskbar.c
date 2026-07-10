#include <string.h>
#include "taskbar.h"

static const char *k_labels[TOOL_COUNT] = {
    "Select",
    "Wire",
    "Input",
    "Output",
    "Place SN7408",
    "Place SN7432",
};

#define BUTTON_PADDING_X 14
#define BUTTON_MARGIN 6

void taskbar_init(Taskbar *tb) {
    for (int i = 0; i < TOOL_COUNT; i++) tb->label_textures[i] = NULL;
    taskbar_layout(tb, 800);
}

void taskbar_shutdown(Taskbar *tb) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (tb->label_textures[i] != NULL) {
            SDL_DestroyTexture(tb->label_textures[i]);
            tb->label_textures[i] = NULL;
        }
    }
}

/* Button labels are static strings that never change after the app starts, so
   the texture is created once on first use and just blitted every frame after -
   text_util_draw would otherwise rebuild it from scratch 60 times a second. */
static SDL_Texture *get_label_texture(SDL_Renderer *renderer, Taskbar *tb, TTF_Font *font, int i) {
    if (tb->label_textures[i] != NULL || font == NULL) return tb->label_textures[i];
    SDL_Color white = { 235, 235, 235, 255 };
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, k_labels[i], white);
    if (surface == NULL) return NULL;
    tb->label_textures[i] = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return tb->label_textures[i];
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

void taskbar_render(SDL_Renderer *renderer, TTF_Font *font, Taskbar *tb, Tool active_tool) {
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
}

int taskbar_hit_test(const Taskbar *tb, int x, int y) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        const SDL_Rect *r = &tb->button_rects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
            return i;
        }
    }
    return -1;
}
