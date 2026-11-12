/* mc3d.c — see mc3d.h. Model units are skin pixels: feet at y=0, the head's
 * top at y=32, the face looks down +z.
 *
 * A small software rasterizer: every box face is two triangles, filled with
 * perspective-correct texture lookups and a depth buffer (so arms, overlays
 * and the cape never sort wrong). Rendered at up to 2x and scaled down by
 * Cairo for smooth edges. Box edges get a soft dark line, which is what
 * keeps a mostly white skin readable. */
#include "mc3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double x, y, z; } v3;

typedef struct {
    double x0, y0, z0, x1, y1, z1;
    int u, v, w, h, d;                   /* texture box (skin pixels, before HD scaling) */
    cairo_surface_t *tex;
    double ts;                           /* texture scale: HD capes are 2x, 4x ... */
    double rx, ry, rz;                   /* part rotation (MC order: Z * Y * X) */
    v3 pivot;
    double bias;                         /* depth nudge: overlays win over their base */
} box_t;

typedef struct {
    v3 p[4];                             /* view space: TL, TR, BL, BR */
    double light, bias;
    double fu, fv, fw, fh;               /* texture rect, texels */
    double mw, mh;                       /* face size in model units (for the edge line) */
    const uint32_t *tex;
    int tw, th, tstride;                 /* stride in pixels */
} face_t;

typedef struct {
    double yaw, pitch, cx, cy, s;        /* s: pixels per model unit */
    face_t *f;
    int n, cap;
} scene_t;

static face_t *new_face(scene_t *sc)
{
    if (sc->n == sc->cap) {
        sc->cap = sc->cap ? sc->cap * 2 : 256;
        sc->f = realloc(sc->f, sizeof *sc->f * sc->cap);
    }
    return &sc->f[sc->n++];
}

static v3 rot_part(v3 p, const box_t *b)
{
    double x = p.x - b->pivot.x, y = p.y - b->pivot.y, z = p.z - b->pivot.z, t;
    double c = cos(b->rx), s = sin(b->rx);
    t = y * c - z * s; z = y * s + z * c; y = t;
    c = cos(b->ry); s = sin(b->ry);
    t = x * c + z * s; z = -x * s + z * c; x = t;
    c = cos(b->rz); s = sin(b->rz);
    t = x * c - y * s; y = x * s + y * c; x = t;
    return (v3){x + b->pivot.x, y + b->pivot.y, z + b->pivot.z};
}

static v3 view(const scene_t *sc, v3 p)
{
    double x = p.x, y = p.y - 16, z = p.z, t;
    double c = cos(sc->yaw), s = sin(sc->yaw);
    t = x * c + z * s; z = -x * s + z * c; x = t;
    c = cos(sc->pitch); s = sin(sc->pitch);
    t = y * c - z * s; z = y * s + z * c; y = t;
    return (v3){x, y, z};
}

#define EYE 90.0                         /* camera distance: mild perspective */

static void project(const scene_t *sc, v3 p, double *sx, double *sy)
{
    double k = EYE / (EYE - p.z);
    *sx = sc->cx + sc->s * k * p.x;
    *sy = sc->cy - sc->s * k * p.y;
}

static v3 sub(v3 a, v3 b) { return (v3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static v3 cross(v3 a, v3 b) { return (v3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

static void add_face(scene_t *sc, const box_t *b, v3 tl, v3 tr, v3 bl, double fu, double fv, double fw, double fh)
{
    if (fw <= 0 || fh <= 0 || !b->tex)
        return;
    v3 a = view(sc, rot_part(tl, b)), c = view(sc, rot_part(tr, b)), d = view(sc, rot_part(bl, b));
    v3 n = cross(sub(d, a), sub(c, a));
    double len = sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    /* facing away from the eye (which sits at z = EYE, not infinity) */
    v3 to_eye = {-a.x, -a.y, EYE - a.z};
    if (len < 1e-9 || n.x * to_eye.x + n.y * to_eye.y + n.z * to_eye.z <= 0)
        return;
    n.x /= len; n.y /= len; n.z /= len;
    const double L[3] = {-0.42, 0.55, 0.72};
    double lam = n.x * L[0] + n.y * L[1] + n.z * L[2];
    face_t *f = new_face(sc);
    f->light = 0.52 + 0.48 * (lam > 0 ? lam : 0);
    f->p[0] = a;
    f->p[1] = c;
    f->p[2] = d;
    f->p[3] = (v3){c.x + d.x - a.x, c.y + d.y - a.y, c.z + d.z - a.z};
    f->bias = b->bias;
    f->fu = fu * b->ts; f->fv = fv * b->ts; f->fw = fw * b->ts; f->fh = fh * b->ts;
    f->mw = sqrt((tr.x - tl.x) * (tr.x - tl.x) + (tr.y - tl.y) * (tr.y - tl.y) + (tr.z - tl.z) * (tr.z - tl.z));
    f->mh = sqrt((bl.x - tl.x) * (bl.x - tl.x) + (bl.y - tl.y) * (bl.y - tl.y) + (bl.z - tl.z) * (bl.z - tl.z));
    f->tex = (const uint32_t *)cairo_image_surface_get_data(b->tex);
    f->tw = cairo_image_surface_get_width(b->tex);
    f->th = cairo_image_surface_get_height(b->tex);
    f->tstride = cairo_image_surface_get_stride(b->tex) / 4;
}

/* one cell face of the head model: a flat colour, no edge line */
static void add_cell_face(scene_t *sc, const box_t *b, v3 tl, v3 tr, v3 bl, const uint32_t *colour)
{
    int n0 = sc->n;
    add_face(sc, b, tl, tr, bl, 0, 0, 1, 1);
    if (sc->n == n0)
        return;
    face_t *f = &sc->f[sc->n - 1];
    f->tex = colour;
    f->fu = f->fv = 0;
    f->fw = f->fh = 1;
    f->tw = f->th = f->tstride = 1;
    f->mw = f->mh = 1e6;
}

static void add_hat(scene_t *sc, const mc3d_t *v, const box_t *head)
{
    int n = v->hat_n, hh = v->hat_h > 0 ? v->hat_h : n;
    double s = 16.0 / n;
    const uint32_t *c = v->hat, *sd = v->hat_sides;
#define CELL(x, y, z) ((x) >= 0 && (y) >= 0 && (z) >= 0 && (x) < n && (y) < hh && (z) < n && c[((y) * n + (z)) * n + (x)])
    for (int y = 0; y < hh; y++)
        for (int z = 0; z < n; z++)
            for (int x = 0; x < n; x++) {
                int i = (y * n + z) * n + x;
                const uint32_t *k = &c[i];
                if (!*k)
                    continue;
                /* side d's own colour if it has one */
#define COL(d) (sd && sd[i * 6 + (d)] ? &sd[i * 6 + (d)] : k)
                double x0 = -8 + x * s, x1 = x0 + s, y0 = 22 + y * s, y1 = y0 + s, z0 = -8 + z * s, z1 = z0 + s;
                if (!CELL(x, y, z + 1))
                    add_cell_face(sc, head, (v3){x0, y1, z1}, (v3){x1, y1, z1}, (v3){x0, y0, z1}, COL(0));
                if (!CELL(x, y, z - 1))
                    add_cell_face(sc, head, (v3){x1, y1, z0}, (v3){x0, y1, z0}, (v3){x1, y0, z0}, COL(1));
                if (!CELL(x - 1, y, z))
                    add_cell_face(sc, head, (v3){x0, y1, z0}, (v3){x0, y1, z1}, (v3){x0, y0, z0}, COL(2));
                if (!CELL(x + 1, y, z))
                    add_cell_face(sc, head, (v3){x1, y1, z1}, (v3){x1, y1, z0}, (v3){x1, y0, z1}, COL(3));
                if (!CELL(x, y + 1, z))
                    add_cell_face(sc, head, (v3){x0, y1, z0}, (v3){x1, y1, z0}, (v3){x0, y1, z1}, COL(4));
                if (!CELL(x, y - 1, z))
                    add_cell_face(sc, head, (v3){x0, y0, z1}, (v3){x1, y0, z1}, (v3){x0, y0, z0}, COL(5));
#undef COL
            }
#undef CELL
}

static void add_box(scene_t *sc, const box_t *b)
{
    double x0 = b->x0, y0 = b->y0, z0 = b->z0, x1 = b->x1, y1 = b->y1, z1 = b->z1;
    int u = b->u, v = b->v, w = b->w, h = b->h, d = b->d;
    /* front +z, back -z, right side -x, left side +x, top, bottom */
    add_face(sc, b, (v3){x0, y1, z1}, (v3){x1, y1, z1}, (v3){x0, y0, z1}, u + d, v + d, w, h);
    add_face(sc, b, (v3){x1, y1, z0}, (v3){x0, y1, z0}, (v3){x1, y0, z0}, u + 2 * d + w, v + d, w, h);
    add_face(sc, b, (v3){x0, y1, z0}, (v3){x0, y1, z1}, (v3){x0, y0, z0}, u, v + d, d, h);
    add_face(sc, b, (v3){x1, y1, z1}, (v3){x1, y1, z0}, (v3){x1, y0, z1}, u + d + w, v + d, d, h);
    add_face(sc, b, (v3){x0, y1, z0}, (v3){x1, y1, z0}, (v3){x0, y1, z1}, u + d, v, w, d);
    add_face(sc, b, (v3){x0, y0, z1}, (v3){x1, y0, z1}, (v3){x0, y0, z0}, u + d + w, v, w, d);
}

/* a body part, its overlay layer (inflated) on top */
static void part(scene_t *sc, cairo_surface_t *tex, double x0, double y0, double z0, int w, int h, int d, int u,
                 int v, int ou, int ov, double infl, double rx, double ry, double rz, v3 pivot)
{
    box_t b = {x0, y0, z0, x0 + w, y0 + h, z0 + d, u, v, w, h, d, tex, 1, rx, ry, rz, pivot, 0};
    add_box(sc, &b);
    if (ou >= 0) {
        box_t o = b;
        o.x0 -= infl; o.y0 -= infl; o.z0 -= infl;
        o.x1 += infl; o.y1 += infl; o.z1 += infl;
        o.u = ou; o.v = ov;
        o.bias = 0.05;
        add_box(sc, &o);
    }
}

/* ------------------------------------------------------------ raster -- */
typedef struct {
    uint32_t *px;
    float *depth;
    int w, h, cap;
    int x0, y0, x1, y1;                  /* dirty box */
} target_t;

static target_t T;

static void target_size(int w, int h)
{
    if (w * h > T.cap) {
        free(T.px);
        free(T.depth);
        T.cap = w * h;
        T.px = malloc(sizeof *T.px * T.cap);
        T.depth = malloc(sizeof *T.depth * T.cap);
        T.x0 = 0; T.y0 = 0; T.x1 = w; T.y1 = h;   /* clear it all once */
        T.w = w; T.h = h;
        memset(T.px, 0, sizeof *T.px * T.cap);
        for (int i = 0; i < T.cap; i++)
            T.depth[i] = -1e9f;
    }
    if (w != T.w || h != T.h) {
        memset(T.px, 0, sizeof *T.px * w * h);
        for (int i = 0; i < w * h; i++)
            T.depth[i] = -1e9f;
    } else {                             /* only what the last frame touched */
        for (int y = T.y0; y < T.y1; y++) {
            memset(T.px + y * w + T.x0, 0, sizeof *T.px * (T.x1 - T.x0));
            for (int x = T.x0; x < T.x1; x++)
                T.depth[y * w + x] = -1e9f;
        }
    }
    T.w = w;
    T.h = h;
    T.x0 = w; T.y0 = h; T.x1 = 0; T.y1 = 0;
}

typedef struct { double x, y, q, sq, tq; } vert_t;   /* q = 1/(EYE - z) */

static inline uint32_t shade(uint32_t c, double k)
{
    uint32_t a = c >> 24;
    uint32_t r = (uint32_t)(((c >> 16) & 255) * k), g = (uint32_t)(((c >> 8) & 255) * k), b = (uint32_t)((c & 255) * k);
    return a << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
}

static void tri(const face_t *f, vert_t a, vert_t b, vert_t c, double edge_px)
{
    double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (fabs(area) < 1e-9)
        return;
    int minx = (int)floor(fmin(a.x, fmin(b.x, c.x))), maxx = (int)ceil(fmax(a.x, fmax(b.x, c.x)));
    int miny = (int)floor(fmin(a.y, fmin(b.y, c.y))), maxy = (int)ceil(fmax(a.y, fmax(b.y, c.y)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > T.w) maxx = T.w;
    if (maxy > T.h) maxy = T.h;
    if (minx >= maxx || miny >= maxy)
        return;
    if (minx < T.x0) T.x0 = minx;
    if (miny < T.y0) T.y0 = miny;
    if (maxx > T.x1) T.x1 = maxx;
    if (maxy > T.y1) T.y1 = maxy;

    double inv = 1.0 / area;
    /* the edge line, in face units: ~1.1 output pixels wide */
    double es = edge_px / (f->mw * 1.0), et = edge_px / (f->mh * 1.0);
    for (int y = miny; y < maxy; y++) {
        double py = y + 0.5;
        for (int x = minx; x < maxx; x++) {
            double px = x + 0.5;
            double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * inv;
            double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * inv;
            double w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            double q = w0 * a.q + w1 * b.q + w2 * c.q;
            float depth = (float)(EYE - 1.0 / q + f->bias);
            int i = y * T.w + x;
            if (depth <= T.depth[i])
                continue;
            double s = (w0 * a.sq + w1 * b.sq + w2 * c.sq) / q, t = (w0 * a.tq + w1 * b.tq + w2 * c.tq) / q;
            int tx = (int)(f->fu + fmin(fmax(s, 0), 0.9999) * f->fw);
            int ty = (int)(f->fv + fmin(fmax(t, 0), 0.9999) * f->fh);
            if (tx < 0 || ty < 0 || tx >= f->tw || ty >= f->th)
                continue;
            uint32_t texel = f->tex[ty * f->tstride + tx];
            if ((texel >> 24) < 128)     /* cutout, like the game */
                continue;
            texel |= 0xFF000000u;
            double k = f->light;
            double d = fmin(fmin(s, 1 - s) / es, fmin(t, 1 - t) / et);
            if (d < 1)
                k *= 0.78 + 0.22 * d;
            T.px[i] = shade(texel, k);
            T.depth[i] = depth;
        }
    }
}

static void raster_face(const scene_t *sc, const face_t *f, double ss)
{
    static const double S[4] = {0, 1, 0, 1}, Tt[4] = {0, 0, 1, 1};
    vert_t v[4];
    for (int i = 0; i < 4; i++) {
        double sx, sy;
        project(sc, f->p[i], &sx, &sy);
        double q = 1.0 / (EYE - f->p[i].z);
        v[i] = (vert_t){sx, sy, q, S[i] * q, Tt[i] * q};
    }
    double edge_px = 1.1 * ss / sc->s;   /* model units per ~1.1 screen px */
    tri(f, v[0], v[1], v[2], edge_px);
    tri(f, v[1], v[3], v[2], edge_px);
}

/* ------------------------------------------------------------- public -- */
bool mc3d_skin_is_slim(cairo_surface_t *skin)
{
    if (!skin || cairo_image_surface_get_width(skin) != 64 || cairo_image_surface_get_height(skin) != 64)
        return false;
    cairo_surface_flush(skin);
    const uint32_t *px = (const uint32_t *)cairo_image_surface_get_data(skin);
    int st = cairo_image_surface_get_stride(skin) / 4;
    /* right arm front+back, columns 54-55, and the left arm's 46-47 (rows 52-63) */
    for (int y = 20; y < 32; y++)
        for (int x = 54; x < 56; x++)
            if (px[y * st + x] >> 24)
                return false;
    for (int y = 52; y < 64; y++)
        for (int x = 46; x < 48; x++)
            if (px[y * st + x] >> 24)
                return false;
    return true;
}

void mc3d_init(mc3d_t *v)
{
    memset(v, 0, sizeof *v);
    v->yaw = -0.5;
    v->pitch = 0.12;
    v->idle = 100;
}

void mc3d_show_back(mc3d_t *v)
{
    v->show_back = 3.2;
    v->idle = 0;
    v->yaw_vel = 0;
}

bool mc3d_tick(mc3d_t *v, double dt)
{
    v->t += dt;
    if (v->dragging)
        return true;
    v->idle += dt;
    if (v->show_back > 0) {              /* turn round to show the cape, then back */
        v->show_back -= dt;
        double target = M_PI - 0.45;
        double cur = remainder(v->yaw - target, 2 * M_PI);
        v->yaw -= cur * (1 - exp(-4.5 * dt));
        v->pitch += (0.18 - v->pitch) * (1 - exp(-3 * dt));
        if (v->show_back <= 0)
            v->idle = 3.5;
        return true;
    }
    v->yaw += v->yaw_vel * dt;
    v->yaw_vel *= exp(-3.2 * dt);
    if (v->idle > 3.5 && v->idle_turn) { /* drift back to a slow showcase turn */
        double target = -0.5 + sin(v->t * 0.22) * 0.55;
        double cur = remainder(v->yaw - target, 2 * M_PI);
        v->yaw -= cur * (1 - exp(-1.2 * dt));
        v->pitch += (0.12 - v->pitch) * (1 - exp(-2 * dt));
    }
    return true;
}

void mc3d_drag(mc3d_t *v, double dx, double dy)
{
    v->dragging = true;
    v->idle = 0;
    v->show_back = 0;
    v->yaw += dx * 0.012;
    v->pitch = fmax(-0.6, fmin(0.7, v->pitch + dy * 0.006));
}

void mc3d_release(mc3d_t *v, double vx)
{
    v->dragging = false;
    v->idle = 0;
    v->yaw_vel = fmax(-14, fmin(14, vx * 0.012));
}

void mc3d_draw(mc3d_t *v, cairo_t *cr, int w, int h, const double sh[3])
{
    if (!v->skin || w < 8 || h < 8)
        return;
    static scene_t sc;
    sc.n = 0;
    sc.yaw = v->yaw;
    sc.pitch = v->pitch;
    bool hat = v->hat && (v->hat_n == 16 || v->hat_n == 32);
    /* a hat reaches 6 pixels above the head, 16 more for each extra grid of height */
    double extra = hat && v->hat_h > v->hat_n ? 16.0 * (v->hat_h - v->hat_n) / v->hat_n : 0;
    double scale = fmin(h / (hat ? 54.0 + extra : 47.0), w / 26.0);
    double mid = hat ? 0.53 + 0.5 * extra / (54.0 + extra) : 0.47;

    /* floor shadow, in output pixels */
    sc.s = scale;
    sc.cx = w / 2.0;
    sc.cy = h * mid;
    double fx, fy;
    project(&sc, view(&sc, (v3){0, 0, 0}), &fx, &fy);
    cairo_save(cr);
    cairo_translate(cr, fx, fy);
    cairo_scale(cr, 9 * sc.s, 9 * sc.s * fmax(0.12, sin(sc.pitch + 0.25)));
    cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(g, 0, sh[0], sh[1], sh[2], 0.55);
    cairo_pattern_add_color_stop_rgba(g, 1, sh[0], sh[1], sh[2], 0);
    cairo_set_source(cr, g);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
    cairo_restore(cr);

    /* supersample: up to 2x, about 2.4 Mpx at most */
    double ss = fmin(2.0, fmax(1.0, sqrt(2.4e6 / ((double)w * h))));
    int rw = (int)ceil(w * ss), rh = (int)ceil(h * ss);
    sc.s = scale * ss;
    sc.cx = rw / 2.0;
    sc.cy = rh * mid;

    cairo_surface_flush(v->skin);
    cairo_surface_t *k = v->skin;
    bool legacy = cairo_image_surface_get_height(k) == 32;
    int aw = v->slim || mc3d_skin_is_slim(k) ? 3 : 4;
    double t = v->t;
    double breathe = v->still ? 0 : sin(t * 1.9) * 0.04;
    double swing = v->still ? 0 : sin(t * 1.1) * 0.06;
    v3 neck = {0, 24, 0};

    part(&sc, k, -4, 24, -4, 8, 8, 8, 0, 0, 32, 0, 0.5, v->look_y * 0.45, v->look_x * 0.7, 0, neck);
    if (hat) {
        box_t head = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, k, 1, v->look_y * 0.45, v->look_x * 0.7, 0, neck, 0.1};
        add_hat(&sc, v, &head);
    }
    part(&sc, k, -4, 12, -2, 8, 12, 4, 16, 16, legacy ? -1 : 16, 32, 0.25, 0, 0, 0, neck);
    double rwave = v->wave > 0 ? -v->wave * (2.5 + sin(t * 10) * 0.35) : 0;
    part(&sc, k, -4 - aw, 12, -2, aw, 12, 4, 40, 16, legacy ? -1 : 40, 32, 0.25, swing, 0,
         -0.06 - breathe + rwave, (v3){-4 - aw / 2.0, 22, 0});
    part(&sc, k, 4, 12, -2, aw, 12, 4, legacy ? 40 : 32, legacy ? 16 : 48, legacy ? -1 : 48, 48, 0.25, -swing, 0,
         0.06 + breathe, (v3){4 + aw / 2.0, 22, 0});
    part(&sc, k, -4, 0, -2, 4, 12, 4, 0, 16, legacy ? -1 : 0, 32, 0.25, -swing * 0.5, 0, 0, (v3){-2, 12, 0});
    part(&sc, k, 0, 0, -2, 4, 12, 4, legacy ? 0 : 16, legacy ? 16 : 48, legacy ? -1 : 0, 48, 0.25, swing * 0.5, 0, 0,
         (v3){2, 12, 0});

    /* cape: hangs off the shoulders, its outside facing away, swinging */
    if (v->cape) {
        cairo_surface_flush(v->cape);
        double ts = cairo_image_surface_get_width(v->cape) / 64.0;
        double sway = 0.12 + (v->still ? 0 : sin(t * 1.3) * 0.05);
        box_t c = {-5, 8, -3.5, 5, 24, -2.5, 0, 0, 10, 16, 1, v->cape, ts, -sway, M_PI, 0, {0, 24, -3}, 0};
        add_box(&sc, &c);
    }

    target_size(rw, rh);
    for (int i = 0; i < sc.n; i++)
        raster_face(&sc, &sc.f[i], ss);
    if (T.x1 <= T.x0)
        return;

    cairo_surface_t *img = cairo_image_surface_create_for_data((unsigned char *)T.px, CAIRO_FORMAT_ARGB32, rw, rh,
                                                               rw * 4);
    cairo_save(cr);
    cairo_scale(cr, 1 / ss, 1 / ss);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), ss > 1.01 ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, T.x0, T.y0, T.x1 - T.x0, T.y1 - T.y0);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_surface_destroy(img);
}
