#include <math.h>
#include "camera.h"

void camera_init(Camera *cam) {
    cam->zoom = 1.0f;
    cam->pan_x = 60.0f;
    cam->pan_y = 60.0f;
}

float camera_cell_px(const Camera *cam) {
    return BASE_CELL_PX * cam->zoom;
}

void camera_grid_to_screen(const Camera *cam, int gx, int gy, int *sx, int *sy) {
    float cell = camera_cell_px(cam);
    *sx = (int)lroundf(gx * cell + cam->pan_x);
    *sy = (int)lroundf(gy * cell + cam->pan_y);
}

void camera_screen_to_grid(const Camera *cam, int sx, int sy, int *gx, int *gy) {
    float cell = camera_cell_px(cam);
    *gx = (int)lroundf((sx - cam->pan_x) / cell);
    *gy = (int)lroundf((sy - cam->pan_y) / cell);
}

void camera_screen_to_grid_f(const Camera *cam, int sx, int sy, float *gx, float *gy) {
    float cell = camera_cell_px(cam);
    *gx = (sx - cam->pan_x) / cell;
    *gy = (sy - cam->pan_y) / cell;
}

void camera_screen_to_grid_floor(const Camera *cam, int sx, int sy, int *gx, int *gy) {
    float cell = camera_cell_px(cam);
    *gx = (int)floorf((sx - cam->pan_x) / cell);
    *gy = (int)floorf((sy - cam->pan_y) / cell);
}

void camera_pan(Camera *cam, int dx_screen, int dy_screen) {
    cam->pan_x += dx_screen;
    cam->pan_y += dy_screen;
}

void camera_zoom_at(Camera *cam, int cursor_sx, int cursor_sy, int delta) {
    float old_cell = camera_cell_px(cam);
    float world_x = (cursor_sx - cam->pan_x) / old_cell;
    float world_y = (cursor_sy - cam->pan_y) / old_cell;

    float factor = powf(1.15f, (float)delta);
    cam->zoom *= factor;
    if (cam->zoom < CAMERA_MIN_ZOOM) cam->zoom = CAMERA_MIN_ZOOM;
    if (cam->zoom > CAMERA_MAX_ZOOM) cam->zoom = CAMERA_MAX_ZOOM;

    float new_cell = camera_cell_px(cam);
    cam->pan_x = cursor_sx - world_x * new_cell;
    cam->pan_y = cursor_sy - world_y * new_cell;
}
