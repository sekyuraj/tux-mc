/* mc3d.h — a Minecraft player drawn in 3D with Cairo (tux-mc).
 *
 * Boxes from the skin texture, rasterized in C with a depth buffer, light
 * per face, a soft line on box edges, mild perspective. Drag to turn (with
 * momentum); idle sway, the head follows the pointer, the cape swings.
 */
#pragma once
#include <cairo.h>
#include <stdbool.h>

typedef struct {
    cairo_surface_t *skin;               /* 64x64 (or legacy 64x32), not owned */
    cairo_surface_t *cape;               /* 64x32 or HD, may be NULL, not owned */
    bool slim;

    double yaw, pitch;                   /* view, radians */
    double yaw_vel;                      /* momentum after a drag */
    double t;                            /* animation clock, seconds */
    double look_x, look_y;               /* -1..1: where the head looks */
    double wave;                         /* 0..1: right arm waving */
    bool dragging;
    double idle;                         /* seconds since the last drag */
    double show_back;                    /* > 0: turned round to show the cape */
    bool idle_turn;                      /* slow showcase turn when left alone */
    bool still;                          /* no breathing / swaying */
} mc3d_t;

void mc3d_init(mc3d_t *v);
/* advance animation; returns true while something moves */
bool mc3d_tick(mc3d_t *v, double dt);
/* draw centred in w x h; light/shadow colours are 0..1 rgb */
void mc3d_draw(mc3d_t *v, cairo_t *cr, int w, int h, const double shadow_rgb[3]);
void mc3d_drag(mc3d_t *v, double dx_pixels, double dy_pixels);
void mc3d_release(mc3d_t *v, double vx_pixels_per_s);
/* turn round for a few seconds so the cape is in view */
void mc3d_show_back(mc3d_t *v);
/* a 64x64 skin drawn for 3-pixel arms: the 4th arm column is empty. Mojang
 * keeps the model separately and it can be wrong (a slim skin uploaded as
 * classic), so the picture decides. */
bool mc3d_skin_is_slim(cairo_surface_t *skin);
