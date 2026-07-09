#include <stdio.h>
#include <math.h>
#include "text_util.h"

static const char *k_candidate_fonts[] = {
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    NULL
};

TTF_Font *text_util_load_font(int point_size) {
    for (int i = 0; k_candidate_fonts[i] != NULL; i++) {
        TTF_Font *font = TTF_OpenFont(k_candidate_fonts[i], point_size);
        if (font != NULL) {
            return font;
        }
    }
    fprintf(stderr, "Isonic: could not load any system font, text rendering disabled.\n");
    return NULL;
}

void text_util_draw(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    text_util_draw_scaled(renderer, font, text, x, y, color, 1.0f);
}

void text_util_draw_scaled(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color, float scale) {
    if (font == NULL || text == NULL || text[0] == '\0') return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface == NULL) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture != NULL) {
        SDL_Rect dst = { x, y, (int)lroundf(surface->w * scale), (int)lroundf(surface->h * scale) };
        SDL_RenderCopy(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void text_util_measure(TTF_Font *font, const char *text, int *out_w, int *out_h) {
    if (font == NULL || text == NULL) {
        *out_w = 0;
        *out_h = 0;
        return;
    }
    TTF_SizeUTF8(font, text, out_w, out_h);
}
