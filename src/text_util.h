#ifndef ISONIC_TEXT_UTIL_H
#define ISONIC_TEXT_UTIL_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Tries a handful of common Windows system font paths. Returns NULL if none
   could be loaded (callers must tolerate a NULL font by simply skipping text). */
TTF_Font *text_util_load_font(int point_size);

/* No-op if font is NULL. Draws left-aligned text with (x,y) as the top-left corner. */
void text_util_draw(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color);

/* Same as text_util_draw, but blits the glyph texture scaled by `scale` (1.0 =
   native size) so labels visually track the current zoom level. */
void text_util_draw_scaled(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color, float scale);

/* Same as text_util_draw_scaled, but centered on (center_x, center_y) and
   rotated clockwise by angle_deg around that center - used for the IC body's
   large name label, which runs along the body's long axis. */
void text_util_draw_scaled_rotated(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                                    int center_x, int center_y, SDL_Color color, float scale, float angle_deg);

/* Returns the rendered pixel width/height of text, or 0/0 if font is NULL. */
void text_util_measure(TTF_Font *font, const char *text, int *out_w, int *out_h);

#endif
