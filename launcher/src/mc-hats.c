/* mc-hats.c — the Hats page: build a 3D model that sits on your head in game.
 *
 * The model is a grid of cells (mc-hat.c). The view is its own small
 * rasterizer with a face-id buffer, so a click knows exactly which cell face
 * it hit: Add puts a cell on that face, Paint recolours, Erase removes. Your
 * head (from your skin) is drawn in the grid as something to build on. Drag
 * empty space or use the right button to turn the view, scroll to zoom.
 *
 * Saved hats: <data>/tux-mc/hats/<name>.tuxhat. Wearing one copies it to
 * <instance>/config/tuxmc/hat.tuxhat and sets hat=<file name> in
 * tuxmc.properties; the mod shows it and shares it with other tux-mc players.
 */
#include "mc.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define SHARE_MAX 45000                  /* bytes; the mod sends it as base64, 60k at most */
#define UNDO_MAX 60

enum { T_ADD, T_PAINT, T_ERASE, T_PICK, T_FILL, T_N };
static const struct { const char *label, *tip; char key; } TOOLS[T_N] = {
    {"Add", "Put a cell on the face you click (A)", 'a'},
    {"Paint", "Recolour cells (P)", 'p'},
    {"Erase", "Remove cells, also the right button (E)", 'e'},
    {"Pick", "Take a cell's colour (I)", 'i'},
    {"Fill", "Recolour all touching cells of the same colour (F)", 'f'},
};

static const guint32 SWATCHES[] = {
    0xf2f2f2, 0xb8b8b8, 0x7a7a7a, 0x3d3d3d, 0x1a1a1a, 0x5c3b24, 0x8a5a33, 0xc49565,
    0xd94848, 0xe8823a, 0xf2cf3a, 0x8fc24a, 0x4cae5a, 0x2f8f8f, 0x3f86d9, 0x2d4fa8,
    0x7a4fc9, 0xc24fa8, 0xf2a0c0, 0xf6e27a, 0xe2b13c, 0xa7d8f0, 0x9a6b16, 0x6b8a3a,
};

typedef struct { float x, y, z; } p3;

typedef struct {
    p3 p[4];                             /* view space TL TR BL BR */
    guint32 col;
    float light;
    int cell, dir;                       /* dir 0 +z, 1 -z, 2 -x, 3 +x, 4 +y, 5 -y */
    bool head;
} hface_t;

typedef struct {
    int w, h, cap;
    guint32 *px;
    float *depth;
    int *id;
} rbuf_t;

static const int DX[6] = {0, 0, -1, 1, 0, 0}, DY[6] = {0, 0, 0, 0, 1, -1}, DZ[6] = {1, -1, 0, 0, 0, 0};

static struct {
    app_t *a;
    mc_hat_t hat;
    char *path;                          /* file being edited, NULL = not saved yet */
    bool dirty;
    GPtrArray *undo, *redo;              /* mc_hat_t* */
    int tool;
    guint32 colour;
    bool mirror, show_head;
    double yaw, pitch, zoom;

    GArray *faces;                       /* hface_t, from the last frame */
    rbuf_t rb;
    int hover;                           /* face index or -1 */
    double sx, sy;                       /* drag start */
    int button;
    bool rotating, moved;
    guint8 *stroke;                      /* cells added in this stroke */

    GtkWidget *area, *mine, *starts, *name, *info, *msg, *tool_btn[T_N], *colour_btn, *used, *wear, *detail,
        *mirror_chk, *head_chk;
} H;

/* ----------------------------------------------------------- rendering -- */

#define EYE 70.0f

static p3 to_view(float x, float y, float z, double yaw, double pitch)
{
    y -= 30;                             /* the grid's middle: y 22..38 */
    float c = cosf(yaw), s = sinf(yaw), t;
    t = x * c + z * s; z = -x * s + z * c; x = t;
    c = cosf(pitch); s = sinf(pitch);
    t = y * c - z * s; z = y * s + z * c; y = t;
    return (p3){x, y, z};
}

static guint32 skin_at(int u, int v)
{
    cairo_surface_t *k = H.a->skin;
    if (!k || u < 0 || v < 0 || u >= 64 || v >= cairo_image_surface_get_height(k))
        return 0xffc89a78;
    cairo_surface_flush(k);
    const guint32 *px = (const guint32 *)cairo_image_surface_get_data(k);
    int st = cairo_image_surface_get_stride(k) / 4;
    guint32 o = u + 32 < 64 && cairo_image_surface_get_height(k) == 64 ? px[v * st + u + 32] : 0;
    return (o >> 24) >= 128 ? o : px[v * st + u];
}

/* the skin's head pixel seen on face dir of head cell (x, y, z) */
static guint32 head_colour(int n, int x, int y, int z, int dir)
{
    int s = n / 16;
    int i = (x - 4 * s) / s, j = 7 - (y - 2 * s) / s, k = (z - 4 * s) / s;
    switch (dir) {
    case 0: return skin_at(8 + i, 8 + j);
    case 1: return skin_at(24 + 7 - i, 8 + j);
    case 2: return skin_at(k, 8 + j);
    case 3: return skin_at(16 + 7 - k, 8 + j);
    case 4: return skin_at(8 + i, k);
    default: return skin_at(16 + i, 7 - k);
    }
}

static void add_quad(GArray *out, const mc_hat_t *h, int x, int y, int z, int dir, guint32 col, bool head,
                     double yaw, double pitch)
{
    float s = 16.0f / h->n;
    float x0 = -8 + x * s, x1 = x0 + s, y0 = 22 + y * s, y1 = y0 + s, z0 = -8 + z * s, z1 = z0 + s;
    float c[4][3];
#define SET(i, a, b, d) (c[i][0] = (a), c[i][1] = (b), c[i][2] = (d))
    switch (dir) {
    case 0: SET(0, x0, y1, z1); SET(1, x1, y1, z1); SET(2, x0, y0, z1); break;
    case 1: SET(0, x1, y1, z0); SET(1, x0, y1, z0); SET(2, x1, y0, z0); break;
    case 2: SET(0, x0, y1, z0); SET(1, x0, y1, z1); SET(2, x0, y0, z0); break;
    case 3: SET(0, x1, y1, z1); SET(1, x1, y1, z0); SET(2, x1, y0, z1); break;
    case 4: SET(0, x0, y1, z0); SET(1, x1, y1, z0); SET(2, x0, y1, z1); break;
    default: SET(0, x0, y0, z1); SET(1, x1, y0, z1); SET(2, x0, y0, z0); break;
    }
#undef SET
    hface_t f = {.col = col, .cell = (y * h->n + z) * h->n + x, .dir = dir, .head = head};
    for (int i = 0; i < 3; i++)
        f.p[i] = to_view(c[i][0], c[i][1], c[i][2], yaw, pitch);
    f.p[3] = (p3){f.p[1].x + f.p[2].x - f.p[0].x, f.p[1].y + f.p[2].y - f.p[0].y, f.p[1].z + f.p[2].z - f.p[0].z};
    /* normal from the corners; away from the eye = hidden */
    p3 u = {f.p[2].x - f.p[0].x, f.p[2].y - f.p[0].y, f.p[2].z - f.p[0].z};
    p3 v = {f.p[1].x - f.p[0].x, f.p[1].y - f.p[0].y, f.p[1].z - f.p[0].z};
    p3 nn = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    p3 e = {-f.p[0].x, -f.p[0].y, EYE - f.p[0].z};
    if (nn.x * e.x + nn.y * e.y + nn.z * e.z <= 0)
        return;
    float l = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
    float lam = (nn.x * -0.42f + nn.y * 0.55f + nn.z * 0.72f) / l;
    f.light = 0.55f + 0.45f * (lam > 0 ? lam : 0);
    g_array_append_val(out, f);
}

static bool filled(const mc_hat_t *h, bool head, int x, int y, int z)
{
    int n = h->n;
    if (x < 0 || y < 0 || z < 0 || x >= n || y >= n || z >= n)
        return false;
    return h->c[(y * n + z) * n + x] || (head && mc_hat_is_head(n, x, y, z));
}

static void build_faces(GArray *out, const mc_hat_t *h, bool head, double yaw, double pitch)
{
    g_array_set_size(out, 0);
    int n = h->n;
    for (int y = 0; y < n; y++)
        for (int z = 0; z < n; z++)
            for (int x = 0; x < n; x++) {
                guint32 c = h->c[(y * n + z) * n + x];
                bool hd = !c && head && mc_hat_is_head(n, x, y, z);
                if (!c && !hd)
                    continue;
                for (int d = 0; d < 6; d++)
                    if (!filled(h, head, x + DX[d], y + DY[d], z + DZ[d]))
                        add_quad(out, h, x, y, z, d, hd ? head_colour(n, x, y, z, d) : c, hd, yaw, pitch);
            }
}

static void rb_size(rbuf_t *b, int w, int h)
{
    if (w * h > b->cap) {
        g_free(b->px);
        g_free(b->depth);
        g_free(b->id);
        b->cap = w * h;
        b->px = g_new(guint32, b->cap);
        b->depth = g_new(float, b->cap);
        b->id = g_new(int, b->cap);
    }
    b->w = w;
    b->h = h;
    memset(b->px, 0, sizeof(guint32) * w * h);
    for (int i = 0; i < w * h; i++) {
        b->depth[i] = -1e9f;
        b->id[i] = -1;
    }
}

typedef struct { float x, y, q, sq, tq; } vt_t;

static void tri(rbuf_t *b, const hface_t *f, int id, vt_t A, vt_t B, vt_t C, float edge, bool hi)
{
    float area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    if (fabsf(area) < 1e-6f)
        return;
    int x0 = MAX(0, (int)floorf(fminf(A.x, fminf(B.x, C.x)))), x1 = MIN(b->w, (int)ceilf(fmaxf(A.x, fmaxf(B.x, C.x))));
    int y0 = MAX(0, (int)floorf(fminf(A.y, fminf(B.y, C.y)))), y1 = MIN(b->h, (int)ceilf(fmaxf(A.y, fmaxf(B.y, C.y))));
    float inv = 1 / area;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((B.x - px) * (C.y - py) - (B.y - py) * (C.x - px)) * inv;
            float w1 = ((C.x - px) * (A.y - py) - (C.y - py) * (A.x - px)) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            float q = w0 * A.q + w1 * B.q + w2 * C.q, d = EYE - 1 / q;
            int i = y * b->w + x;
            if (d <= b->depth[i])
                continue;
            float s = (w0 * A.sq + w1 * B.sq + w2 * C.sq) / q, t = (w0 * A.tq + w1 * B.tq + w2 * C.tq) / q;
            float k = f->light * (f->head ? 0.82f : 1);
            float e = fminf(fminf(s, 1 - s), fminf(t, 1 - t)) / edge;
            if (e < 1)
                k *= f->head ? 0.9f + 0.1f * e : 0.8f + 0.2f * e;
            if (hi)
                k = k * 0.7f + 0.45f;
            guint32 c = f->col;
            int r = MIN(255, (int)((c >> 16 & 255) * k)), g = MIN(255, (int)((c >> 8 & 255) * k)),
                bl = MIN(255, (int)((c & 255) * k));
            b->px[i] = 0xFF000000u | r << 16 | g << 8 | bl;
            b->depth[i] = d;
            b->id[i] = id;
        }
}

/* draw faces into b; scale = pixels per skin pixel */
static void raster(rbuf_t *b, GArray *faces, double scale, int hover)
{
    float cx = b->w / 2.0f, cy = b->h / 2.0f;
    for (guint i = 0; i < faces->len; i++) {
        const hface_t *f = &g_array_index(faces, hface_t, i);
        static const float S[4] = {0, 1, 0, 1}, T[4] = {0, 0, 1, 1};
        vt_t v[4];
        for (int k = 0; k < 4; k++) {
            float q = 1 / (EYE - f->p[k].z), m = EYE * q;
            v[k] = (vt_t){cx + scale * m * f->p[k].x, cy - scale * m * f->p[k].y, q, S[k] * q, T[k] * q};
        }
        float cell = 16.0f / H.hat.n * scale;
        float edge = fmaxf(0.04f, 1.0f / fmaxf(cell, 1));
        tri(b, f, i, v[0], v[1], v[2], edge, (int)i == hover);
        tri(b, f, i, v[1], v[3], v[2], edge, (int)i == hover);
    }
}

static cairo_surface_t *rb_surface(rbuf_t *b)
{
    return cairo_image_surface_create_for_data((unsigned char *)b->px, CAIRO_FORMAT_ARGB32, b->w, b->h, b->w * 4);
}

/* a small picture of a hat on a plain head, for the lists */
static cairo_surface_t *thumb(const mc_hat_t *h, int size)
{
    GArray *f = g_array_new(FALSE, FALSE, sizeof(hface_t));
    mc_hat_t keep = H.hat;               /* raster() reads H.hat.n for the edge width */
    H.hat = *h;
    build_faces(f, h, true, -0.6, 0.35);
    rbuf_t b = {0};
    rb_size(&b, size, size);
    raster(&b, f, size / 26.0, -1);
    H.hat = keep;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    memcpy(cairo_image_surface_get_data(s), b.px, sizeof(guint32) * size * size);
    cairo_surface_mark_dirty(s);
    g_free(b.px);
    g_free(b.depth);
    g_free(b.id);
    g_array_unref(f);
    return s;
}

static void draw(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    (void)data;
    if (!H.hat.n || w < 8 || h < 8)
        return;
    int sc = gtk_widget_get_scale_factor(H.area);
    int rw = w * sc, rh = h * sc;
    build_faces(H.faces, &H.hat, H.show_head, H.yaw, H.pitch);
    rb_size(&H.rb, rw, rh);
    raster(&H.rb, H.faces, fmin(rw, rh) / 21.0 * H.zoom, H.hover);
    /* the grid's floor, faint, so empty space has a shape */
    GdkRGBA dim;
    mc_pal(H.a, PAL_DIM, &dim);
    cairo_save(cr);
    cairo_scale(cr, 1.0 / sc, 1.0 / sc);
    cairo_set_source_rgba(cr, dim.red, dim.green, dim.blue, 0.35);
    cairo_set_line_width(cr, sc);
    double s = fmin(rw, rh) / 21.0 * H.zoom;
    for (int i = 0; i <= 16; i += 2) {
        for (int k = 0; k < 2; k++) {
            float ax = k ? -8 : -8 + i, az = k ? -8 + i : -8, bx = k ? 8 : -8 + i, bz = k ? -8 + i : 8;
            p3 p = to_view(ax, 22, az, H.yaw, H.pitch), q = to_view(bx, 22, bz, H.yaw, H.pitch);
            float mp = EYE / (EYE - p.z), mq = EYE / (EYE - q.z);
            cairo_move_to(cr, rw / 2.0 + s * mp * p.x, rh / 2.0 - s * mp * p.y);
            cairo_line_to(cr, rw / 2.0 + s * mq * q.x, rh / 2.0 - s * mq * q.y);
        }
    }
    cairo_stroke(cr);
    cairo_surface_t *img = rb_surface(&H.rb);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(img);
    cairo_restore(cr);
}

/* ------------------------------------------------------------- editing -- */

static void update_info(void);
static void fill_used(void);

static void push_undo(void)
{
    mc_hat_t *u = g_new0(mc_hat_t, 1);
    mc_hat_copy(u, &H.hat);
    g_ptr_array_add(H.undo, u);
    if (H.undo->len > UNDO_MAX)
        g_ptr_array_remove_index(H.undo, 0);
    g_ptr_array_set_size(H.redo, 0);
}

static void free_hat(gpointer p)
{
    mc_hat_free(p);
    g_free(p);
}

static void changed(void)
{
    H.dirty = true;
    update_info();
    fill_used();
    gtk_widget_queue_draw(H.area);
}

static void undo_redo(GPtrArray *from, GPtrArray *to)
{
    if (!from->len)
        return;
    mc_hat_t *cur = g_new0(mc_hat_t, 1);
    mc_hat_copy(cur, &H.hat);
    g_ptr_array_add(to, cur);
    mc_hat_t *u = g_ptr_array_steal_index(from, from->len - 1);
    char name[64];
    g_strlcpy(name, H.hat.name, sizeof name);
    mc_hat_copy(&H.hat, u);
    g_strlcpy(H.hat.name, name, sizeof H.hat.name);
    free_hat(u);
    changed();
}

static void set_cell(int cell, guint32 c)
{
    int n = H.hat.n, x = cell % n, z = cell / n % n, y = cell / (n * n);
    if (mc_hat_is_head(n, x, y, z))
        return;
    H.hat.c[cell] = c;
    if (H.mirror)
        H.hat.c[(y * n + z) * n + (n - 1 - x)] = c;
}

static void flood(int start, guint32 from, guint32 to)
{
    if (from == to)
        return;
    int n = H.hat.n;
    GArray *st = g_array_new(FALSE, FALSE, sizeof(int));
    g_array_append_val(st, start);
    while (st->len) {
        int c = g_array_index(st, int, st->len - 1);
        g_array_set_size(st, st->len - 1);
        if (H.hat.c[c] != from)
            continue;
        H.hat.c[c] = to;
        int x = c % n, z = c / n % n, y = c / (n * n);
        for (int d = 0; d < 6; d++) {
            int X = x + DX[d], Y = y + DY[d], Z = z + DZ[d];
            if (X >= 0 && Y >= 0 && Z >= 0 && X < n && Y < n && Z < n) {
                int k = (Y * n + Z) * n + X;
                g_array_append_val(st, k);
            }
        }
    }
    g_array_unref(st);
}

static void set_colour(guint32 rgb)
{
    H.colour = 0xFF000000u | rgb;
    GdkRGBA c = {(rgb >> 16 & 255) / 255.0, (rgb >> 8 & 255) / 255.0, (rgb & 255) / 255.0, 1};
    mc_colour_set(H.colour_btn, &c);
}

static void set_tool(int t)
{
    H.tool = t;
    for (int i = 0; i < T_N; i++)
        if (i == t)
            gtk_widget_add_css_class(H.tool_btn[i], "active");
        else
            gtk_widget_remove_css_class(H.tool_btn[i], "active");
}

/* the tool on face f; returns true when something changed */
static bool apply(int f, int tool, bool first)
{
    if (f < 0 || f >= (int)H.faces->len)
        return false;
    hface_t *hf = &g_array_index(H.faces, hface_t, f);
    int n = H.hat.n, x = hf->cell % n, z = hf->cell / n % n, y = hf->cell / (n * n);
    switch (tool) {
    case T_ADD: {
        if (H.stroke[hf->cell] && !first)
            return false;                /* don't stack a stroke onto itself */
        int X = x + DX[hf->dir], Y = y + DY[hf->dir], Z = z + DZ[hf->dir];
        if (X < 0 || Y < 0 || Z < 0 || X >= n || Y >= n || Z >= n || filled(&H.hat, true, X, Y, Z))
            return false;
        int k = (Y * n + Z) * n + X;
        set_cell(k, H.colour);
        H.stroke[k] = 1;
        if (H.mirror)
            H.stroke[(Y * n + Z) * n + n - 1 - X] = 1;
        return true;
    }
    case T_PAINT:
        if (hf->head || H.hat.c[hf->cell] == H.colour)
            return false;
        set_cell(hf->cell, H.colour);
        return true;
    case T_ERASE:
        if (hf->head)
            return false;
        set_cell(hf->cell, 0);
        return true;
    case T_PICK:
        set_colour(hf->col & 0xFFFFFF);
        set_tool(T_PAINT);
        return false;
    case T_FILL:
        if (hf->head)
            return false;
        flood(hf->cell, H.hat.c[hf->cell], H.colour);
        if (H.mirror)
            flood((y * n + z) * n + n - 1 - x, H.hat.c[(y * n + z) * n + n - 1 - x], H.colour);
        return true;
    }
    return false;
}

static int pick(double x, double y)
{
    int sc = gtk_widget_get_scale_factor(H.area);
    int px = (int)(x * sc), py = (int)(y * sc);
    if (!H.rb.id || px < 0 || py < 0 || px >= H.rb.w || py >= H.rb.h)
        return -1;
    return H.rb.id[py * H.rb.w + px];
}

static void on_press(GtkGestureDrag *g, double x, double y, gpointer data)
{
    (void)data;
    gtk_widget_grab_focus(H.area);
    H.button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    H.sx = x;
    H.sy = y;
    H.moved = false;
    int f = pick(x, y);
    H.rotating = H.button != 1 || f < 0;
    if (H.rotating)
        return;
    memset(H.stroke, 0, H.hat.n * H.hat.n * H.hat.n);
    push_undo();
    if (apply(f, H.tool, true))
        changed();
}

static void on_drag(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)g;
    (void)data;
    static double lx, ly;
    if (!H.moved) {
        lx = ly = 0;
        if (fabs(ox) + fabs(oy) < 4)
            return;
        H.moved = true;
    }
    if (H.rotating) {
        H.yaw += (ox - lx) * 0.01;
        H.pitch = CLAMP(H.pitch + (oy - ly) * 0.008, -1.3, 1.3);
        lx = ox;
        ly = oy;
        gtk_widget_queue_draw(H.area);
        return;
    }
    lx = ox;
    ly = oy;
    if (H.tool == T_PICK || H.tool == T_FILL)
        return;
    /* the faces under the pointer come from the frame just drawn */
    int f = pick(H.sx + ox, H.sy + oy);
    H.hover = f;
    if (apply(f, H.tool, false))
        changed();
}

static void on_release(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)g;
    (void)ox;
    (void)oy;
    (void)data;
    if (H.rotating && !H.moved && H.button == 3) {   /* a right click erases */
        int f = pick(H.sx, H.sy);
        if (f >= 0) {
            push_undo();
            if (apply(f, T_ERASE, true))
                changed();
            else
                g_ptr_array_set_size(H.undo, H.undo->len - 1);
        }
    }
    H.rotating = false;
}

static void on_motion(GtkEventControllerMotion *m, double x, double y, gpointer data)
{
    (void)m;
    (void)data;
    int f = pick(x, y);
    if (f != H.hover) {
        H.hover = f;
        gtk_widget_queue_draw(H.area);
    }
}

static void on_leave(GtkEventControllerMotion *m, gpointer data)
{
    (void)m;
    (void)data;
    H.hover = -1;
    gtk_widget_queue_draw(H.area);
}

static gboolean on_scroll(GtkEventControllerScroll *s, double dx, double dy, gpointer data)
{
    (void)s;
    (void)dx;
    (void)data;
    H.zoom = CLAMP(H.zoom * (dy > 0 ? 0.9 : 1.11), 0.5, 4);
    gtk_widget_queue_draw(H.area);
    return TRUE;
}

static gboolean on_key(GtkEventControllerKey *k, guint key, guint code, GdkModifierType st, gpointer data)
{
    (void)k;
    (void)code;
    (void)data;
    if (st & GDK_CONTROL_MASK) {
        if (key == GDK_KEY_z || key == GDK_KEY_Z)
            undo_redo((st & GDK_SHIFT_MASK) ? H.redo : H.undo, (st & GDK_SHIFT_MASK) ? H.undo : H.redo);
        else if (key == GDK_KEY_y)
            undo_redo(H.redo, H.undo);
        else if (key == GDK_KEY_s)
            gtk_widget_activate(H.name);
        else
            return FALSE;
        return TRUE;
    }
    if (GTK_IS_EDITABLE(gtk_root_get_focus(gtk_widget_get_root(H.area))))
        return FALSE;
    for (int i = 0; i < T_N; i++)
        if (key == (guint)TOOLS[i].key) {
            set_tool(i);
            return TRUE;
        }
    if (key == GDK_KEY_m) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(H.mirror_chk), !H.mirror);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------- the UI -- */

static void fill_lists(void);

static void update_info(void)
{
    GBytes *b = mc_hat_encode(&H.hat);
    gsize sz = g_bytes_get_size(b);
    g_bytes_unref(b);
    int cells = mc_hat_count(&H.hat);
    char *t = sz > SHARE_MAX
                  ? g_strdup_printf("%d cells, %.1f kB: too big to share, other players won't see it", cells, sz / 1024.0)
                  : g_strdup_printf("%d cells, %.1f kB to share%s", cells, sz / 1024.0, H.dirty ? ", not saved" : "");
    gtk_label_set_text(GTK_LABEL(H.info), t);
    g_free(t);
}

static void msg(const char *m) { gtk_label_set_text(GTK_LABEL(H.msg), m); }

static void on_colour(GtkWidget *b, gpointer data)
{
    (void)data;
    GdkRGBA cc;
    mc_colour_get(b, &cc);
    const GdkRGBA *c = &cc;
    H.colour = 0xFF000000u | (guint32)(c->red * 255 + 0.5) << 16 | (guint32)(c->green * 255 + 0.5) << 8 |
               (guint32)(c->blue * 255 + 0.5);
    if (H.tool == T_ERASE || H.tool == T_PICK)
        set_tool(T_ADD);
}

static void draw_swatch(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    guint32 c = GPOINTER_TO_UINT(data);
    cairo_set_source_rgb(cr, (c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0, (c & 255) / 255.0);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
}

static void on_swatch(GtkButton *b, gpointer data)
{
    (void)b;
    set_colour(GPOINTER_TO_UINT(data));
    if (H.tool == T_ERASE || H.tool == T_PICK)
        set_tool(T_ADD);
}

static GtkWidget *swatch(guint32 rgb)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "mcl-swatch");
    GtkWidget *d = gtk_drawing_area_new();
    gtk_widget_set_size_request(d, 18, 18);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(d), draw_swatch, GUINT_TO_POINTER(rgb), NULL);
    gtk_button_set_child(GTK_BUTTON(b), d);
    char *tip = g_strdup_printf("#%06x", rgb);
    gtk_widget_set_tooltip_text(b, tip);
    g_free(tip);
    g_signal_connect(b, "clicked", G_CALLBACK(on_swatch), GUINT_TO_POINTER(rgb));
    return b;
}

/* the colours this hat uses, most used first */
static void fill_used(void)
{
    gtk_flow_box_remove_all(GTK_FLOW_BOX(H.used));
    GHashTable *cnt = g_hash_table_new(NULL, NULL);
    for (int i = 0; i < H.hat.n * H.hat.n * H.hat.n; i++)
        if (H.hat.c[i]) {
            gpointer k = GUINT_TO_POINTER(H.hat.c[i] & 0xFFFFFF);
            g_hash_table_insert(cnt, k, GINT_TO_POINTER(GPOINTER_TO_INT(g_hash_table_lookup(cnt, k)) + 1));
        }
    for (int shown = 0; shown < 16 && g_hash_table_size(cnt); shown++) {
        GHashTableIter it;
        gpointer k, v, best = NULL;
        int bn = 0;
        g_hash_table_iter_init(&it, cnt);
        while (g_hash_table_iter_next(&it, &k, &v))
            if (GPOINTER_TO_INT(v) > bn) {
                bn = GPOINTER_TO_INT(v);
                best = k;
            }
        g_hash_table_remove(cnt, best);
        gtk_flow_box_append(GTK_FLOW_BOX(H.used), swatch(GPOINTER_TO_UINT(best)));
    }
    g_hash_table_unref(cnt);
}

static void reset_view(void)
{
    H.yaw = -0.6;
    H.pitch = 0.35;
    H.zoom = 1;
    H.hover = -1;
}

/* show hat h in the editor (takes it over) */
static void edit(mc_hat_t *h, const char *path)
{
    mc_hat_free(&H.hat);
    H.hat = *h;
    memset(h, 0, sizeof *h);
    g_free(H.path);
    H.path = g_strdup(path);
    H.dirty = false;
    g_ptr_array_set_size(H.undo, 0);
    g_ptr_array_set_size(H.redo, 0);
    g_free(H.stroke);
    H.stroke = g_malloc0(H.hat.n * H.hat.n * H.hat.n);
    gtk_editable_set_text(GTK_EDITABLE(H.name), H.hat.name);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(H.detail), H.hat.n == 32);
    H.hover = -1;
    update_info();
    fill_used();
    gtk_widget_queue_draw(H.area);
}

static char *worn_file(app_t *a) { return g_build_filename(a->mcdir, "config", "tuxmc", "hat.tuxhat", NULL); }

void mc_hat_load_worn(app_t *a)
{
    mc_hat_free(&a->hat);
    char *id = a->mcdir ? mc_tuxmc_get(a, "hat") : NULL;
    if (g_getenv("TUX_MC_HAT_FILE")) {                        /* screenshots */
        mc_hat_load(g_getenv("TUX_MC_HAT_FILE"), &a->hat);
    } else if (id && *id) {
        char *p = worn_file(a);
        if (!mc_hat_load(p, &a->hat))
            mc_hat_free(&a->hat);
        g_free(p);
    }
    g_free(id);
    a->view.hat = a->hat.n ? a->hat.c : NULL;
    a->view.hat_n = a->hat.n;
}

static char *save_current(void)
{
    const char *nm = gtk_editable_get_text(GTK_EDITABLE(H.name));
    g_strlcpy(H.hat.name, *nm ? nm : "My hat", sizeof H.hat.name);
    if (!H.path) {
        GString *f = g_string_new(NULL);
        for (const char *s = H.hat.name; *s; s++)
            g_string_append_c(f, g_ascii_isalnum(*s) ? g_ascii_tolower(*s) : '-');
        char *dir = mc_hats_dir();
        for (int k = 1; !H.path || g_file_test(H.path, G_FILE_TEST_EXISTS); k++) {
            g_free(H.path);
            char *base = k == 1 ? g_strdup_printf("%s.tuxhat", f->str) : g_strdup_printf("%s-%d.tuxhat", f->str, k);
            H.path = g_build_filename(dir, base, NULL);
            g_free(base);
        }
        g_free(dir);
        g_string_free(f, TRUE);
    }
    if (!mc_hat_save(H.path, &H.hat))
        return NULL;
    H.dirty = false;
    update_info();
    fill_lists();
    return g_path_get_basename(H.path);
}

static void on_save(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    char *id = save_current();
    msg(id ? "Saved." : "Couldn't save it.");
    g_free(id);
}

static void on_wear(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    if (!mc_hat_count(&H.hat)) {
        msg("It's empty: add some cells first.");
        return;
    }
    char *id = save_current();
    char *p = worn_file(a);
    if (!id || !mc_hat_save(p, &H.hat)) {
        msg("Couldn't save it.");
    } else {
        mc_tuxmc_set(a, "hat", id);
        mc_hat_load_worn(a);
        GBytes *enc = mc_hat_encode(&H.hat);
        bool big = g_bytes_get_size(enc) > SHARE_MAX;
        g_bytes_unref(enc);
        msg(big ? "You're wearing it. It's too big to share, so only you see it."
                : mc_game_running(a) ? "You're wearing it: the game picks it up in a few seconds."
                                     : "You're wearing it. Other tux-mc players see it too.");
    }
    g_free(p);
    g_free(id);
}

static void on_take_off(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    mc_tuxmc_set(a, "hat", "");
    mc_hat_load_worn(a);
    msg("Hat off.");
}

static void on_undo(GtkButton *b, gpointer d) { (void)b; (void)d; undo_redo(H.undo, H.redo); }
static void on_redo(GtkButton *b, gpointer d) { (void)b; (void)d; undo_redo(H.redo, H.undo); }

static void on_clear(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    push_undo();
    memset(H.hat.c, 0, sizeof(guint32) * H.hat.n * H.hat.n * H.hat.n);
    changed();
}

static void on_tool(GtkButton *b, gpointer d) { (void)b; set_tool(GPOINTER_TO_INT(d)); }
static void on_mirror(GtkCheckButton *c, gpointer d) { (void)d; H.mirror = gtk_check_button_get_active(c); }

static void on_head(GtkCheckButton *c, gpointer d)
{
    (void)d;
    H.show_head = gtk_check_button_get_active(c);
    gtk_widget_queue_draw(H.area);
}

static void on_detail(GObject *dd, GParamSpec *ps, gpointer d)
{
    (void)ps;
    (void)d;
    int n = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)) ? 32 : 16;
    if (!H.hat.n || n == H.hat.n)
        return;
    push_undo();
    mc_hat_resize(&H.hat, n);
    g_free(H.stroke);
    H.stroke = g_malloc0(n * n * n);
    changed();
}

/* move everything one cell */
static void on_move(GtkButton *b, gpointer d)
{
    (void)b;
    int dir = GPOINTER_TO_INT(d), n = H.hat.n;
    /* turn "left/right/front/back" into grid steps as you see it now */
    int dx = DX[dir], dy = DY[dir], dz = DZ[dir];
    if (dir < 4) {
        double ang = remainder(H.yaw, 2 * M_PI);
        int q = (int)lround(ang / (M_PI / 2)) & 3;     /* how far the view is turned */
        for (int i = 0; i < q; i++) {
            int t = dx;
            dx = -dz;
            dz = t;
        }
    }
    push_undo();
    guint32 *o = g_memdup2(H.hat.c, sizeof(guint32) * n * n * n);
    memset(H.hat.c, 0, sizeof(guint32) * n * n * n);
    for (int y = 0; y < n; y++)
        for (int z = 0; z < n; z++)
            for (int x = 0; x < n; x++) {
                int X = x + dx, Y = y + dy, Z = z + dz;
                guint32 c = o[(y * n + z) * n + x];
                if (c && X >= 0 && Y >= 0 && Z >= 0 && X < n && Y < n && Z < n && !mc_hat_is_head(n, X, Y, Z))
                    H.hat.c[(Y * n + Z) * n + X] = c;
            }
    g_free(o);
    changed();
}

static bool leave_ok(void)
{
    if (!H.dirty || !mc_hat_count(&H.hat))
        return true;
    char *id = save_current();             /* never lose work: keep it before switching */
    g_free(id);
    return true;
}

static void on_pick_saved(GtkButton *b, gpointer d)
{
    (void)d;
    leave_ok();
    const char *p = g_object_get_data(G_OBJECT(b), "path");
    mc_hat_t h = {0};
    if (mc_hat_load(p, &h)) {
        edit(&h, p);
        msg("Opened. Changes are kept when you switch.");
    } else {
        msg("That file couldn't be read.");
    }
}

static void on_preset(GtkButton *b, gpointer d)
{
    (void)b;
    leave_ok();
    int i = GPOINTER_TO_INT(d);
    mc_hat_t h = {0};
    if (i < 0) {
        mc_hat_new(&h, 16);
        g_strlcpy(h.name, "My hat", sizeof h.name);
    } else {
        mc_hat_preset(&h, i);
    }
    edit(&h, NULL);
    msg(i < 0 ? "Click your head to add cells. Drag empty space to turn it." : "A copy to change as you like.");
}

static void on_delete(GtkButton *b, gpointer d)
{
    (void)b;
    app_t *a = d;
    if (!H.path) {
        msg("Not saved yet, nothing to delete.");
        return;
    }
    if (!g_object_get_data(G_OBJECT(b), "armed")) {
        g_object_set_data(G_OBJECT(b), "armed", GINT_TO_POINTER(1));
        gtk_button_set_label(b, "Sure?");
        return;
    }
    g_object_set_data(G_OBJECT(b), "armed", NULL);
    gtk_button_set_label(b, "Delete");
    GFile *f = g_file_new_for_path(H.path);
    bool ok = g_file_trash(f, NULL, NULL) || g_file_delete(f, NULL, NULL);
    g_object_unref(f);
    char *id = g_path_get_basename(H.path), *worn = mc_tuxmc_get(a, "hat");
    if (ok && worn && !strcmp(worn, id))
        on_take_off(NULL, a);
    g_free(id);
    g_free(worn);
    g_free(H.path);
    H.path = NULL;
    H.dirty = true;
    fill_lists();
    msg(ok ? "Moved to the trash. It's still here until you switch." : "Couldn't delete it.");
}

static void on_vox_done(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f)
        return;
    char *p = g_file_get_path(f), *err = NULL;
    mc_hat_t h = {0};
    leave_ok();
    if (mc_hat_import_vox(p, &h, &err)) {
        edit(&h, NULL);
        H.dirty = true;
        update_info();
        msg("Imported. Use the arrows to move it onto your head, then Save.");
    } else {
        msg(err);
    }
    g_free(err);
    g_free(p);
    g_object_unref(f);
}

static void on_vox(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "MagicaVoxel model");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "MagicaVoxel (.vox)");
    gtk_file_filter_add_pattern(ff, "*.vox");
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(fl));
    gtk_file_dialog_open(d, a->win, NULL, on_vox_done, a);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(d);
}

static void draw_thumb(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    cairo_surface_t *s = data;
    int sw = cairo_image_surface_get_width(s);
    cairo_translate(cr, (w - sw) / 2.0, (h - sw) / 2.0);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
}

static GtkWidget *hat_tile(const mc_hat_t *h, const char *label)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "mcl-preset");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *d = gtk_drawing_area_new();
    gtk_widget_set_size_request(d, 72, 72);
    if (h) {
        cairo_surface_t *t = thumb(h, 72);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(d), draw_thumb, t, (GDestroyNotify)cairo_surface_destroy);
    }
    gtk_box_append(GTK_BOX(box), d);
    GtkWidget *l = mc_label(label, "dim");
    gtk_label_set_xalign(GTK_LABEL(l), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 10);
    gtk_box_append(GTK_BOX(box), l);
    gtk_button_set_child(GTK_BUTTON(b), box);
    return b;
}

static void fill_lists(void)
{
    gtk_flow_box_remove_all(GTK_FLOW_BOX(H.mine));
    char *dir = mc_hats_dir();
    GDir *dd = g_dir_open(dir, 0, NULL);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    for (const char *e; dd && (e = g_dir_read_name(dd));)
        if (g_str_has_suffix(e, ".tuxhat"))
            g_ptr_array_add(names, g_strdup(e));
    if (dd)
        g_dir_close(dd);
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
    for (guint i = 0; i < names->len; i++) {
        char *p = g_build_filename(dir, names->pdata[i], NULL);
        mc_hat_t h = {0};
        if (mc_hat_load(p, &h)) {
            GtkWidget *t = hat_tile(&h, h.name);
            g_object_set_data_full(G_OBJECT(t), "path", g_strdup(p), g_free);
            g_signal_connect(t, "clicked", G_CALLBACK(on_pick_saved), NULL);
            gtk_flow_box_append(GTK_FLOW_BOX(H.mine), t);
            mc_hat_free(&h);
        }
        g_free(p);
    }
    if (!names->len) {
        GtkWidget *l = mc_label("None yet. Start from one below.", "dim");
        gtk_flow_box_append(GTK_FLOW_BOX(H.mine), l);
    }
    g_ptr_array_unref(names);
    g_free(dir);
}

static GtkWidget *small_button(const char *label, const char *tip, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    if (tip)
        gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

static void on_page_destroy(GtkWidget *w, gpointer d)
{
    (void)w;
    (void)d;
    leave_ok();
    H.area = NULL;
}

GtkWidget *mc_page_hats(app_t *a)
{
    H.a = a;
    if (!H.undo) {
        H.undo = g_ptr_array_new_with_free_func(free_hat);
        H.redo = g_ptr_array_new_with_free_func(free_hat);
        H.faces = g_array_new(FALSE, FALSE, sizeof(hface_t));
        H.mirror = H.show_head = true;
        H.colour = 0xFF000000u | SWATCHES[8];
    }
    reset_view();

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(page, "mcl-page");
    g_signal_connect(page, "destroy", G_CALLBACK(on_page_destroy), NULL);
    gtk_box_append(GTK_BOX(page), mc_label("Hats", "mcl-title"));
    GtkWidget *sub = mc_label("Build something to wear on your head. Other tux-mc players see it in game.", "dim");
    gtk_box_append(GTK_BOX(page), sub);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_vexpand(row, TRUE);
    gtk_box_append(GTK_BOX(page), row);

    /* left: yours + starting points */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(left, 226, -1);
    GtkWidget *lsc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lsc), left);
    gtk_widget_set_vexpand(lsc, TRUE);
    GtkWidget *mine = mc_card("Your hats", NULL);
    H.mine = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(H.mine), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(H.mine), 3);
    gtk_box_append(GTK_BOX(mine), H.mine);
    gtk_box_append(GTK_BOX(left), mine);
    GtkWidget *st = mc_card("Start from", NULL);
    H.starts = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(H.starts), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(H.starts), 3);
    mc_hat_t empty = {0};
    mc_hat_new(&empty, 16);
    GtkWidget *nb = hat_tile(&empty, "Empty");
    mc_hat_free(&empty);
    g_signal_connect(nb, "clicked", G_CALLBACK(on_preset), GINT_TO_POINTER(-1));
    gtk_flow_box_append(GTK_FLOW_BOX(H.starts), nb);
    for (int i = 0; i < mc_hat_presets(); i++) {
        mc_hat_t h = {0};
        mc_hat_preset(&h, i);
        GtkWidget *t = hat_tile(&h, mc_hat_preset_name(i));
        mc_hat_free(&h);
        g_signal_connect(t, "clicked", G_CALLBACK(on_preset), GINT_TO_POINTER(i));
        gtk_flow_box_append(GTK_FLOW_BOX(H.starts), t);
    }
    gtk_box_append(GTK_BOX(st), H.starts);
    GtkWidget *vox = small_button("Open a .vox file…", "a MagicaVoxel model; big ones are scaled to fit",
                                  G_CALLBACK(on_vox), a);
    gtk_box_append(GTK_BOX(st), vox);
    gtk_box_append(GTK_BOX(left), st);
    gtk_box_append(GTK_BOX(row), lsc);

    /* middle: the view */
    GtkWidget *mid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(mid, TRUE);
    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    for (int i = 0; i < T_N; i++) {
        char *l = g_strdup_printf("%s", TOOLS[i].label);
        H.tool_btn[i] = small_button(l, TOOLS[i].tip, G_CALLBACK(on_tool), GINT_TO_POINTER(i));
        gtk_widget_add_css_class(H.tool_btn[i], "mcl-tool");
        g_free(l);
        gtk_box_append(GTK_BOX(tools), H.tool_btn[i]);
    }
    GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(sp, TRUE);
    gtk_box_append(GTK_BOX(tools), sp);
    gtk_box_append(GTK_BOX(tools), small_button("↶", "Undo (Ctrl+Z)", G_CALLBACK(on_undo), NULL));
    gtk_box_append(GTK_BOX(tools), small_button("↷", "Redo (Ctrl+Y)", G_CALLBACK(on_redo), NULL));
    gtk_box_append(GTK_BOX(mid), tools);

    GtkWidget *frame = gtk_overlay_new();
    gtk_widget_add_css_class(frame, "mcl-glass");
    gtk_widget_set_vexpand(frame, TRUE);
    H.area = gtk_drawing_area_new();
    gtk_widget_set_focusable(H.area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(H.area), draw, NULL, NULL);
    GtkGesture *g = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);
    g_signal_connect(g, "drag-begin", G_CALLBACK(on_press), NULL);
    g_signal_connect(g, "drag-update", G_CALLBACK(on_drag), NULL);
    g_signal_connect(g, "drag-end", G_CALLBACK(on_release), NULL);
    gtk_widget_add_controller(H.area, GTK_EVENT_CONTROLLER(g));
    GtkEventController *mo = gtk_event_controller_motion_new();
    g_signal_connect(mo, "motion", G_CALLBACK(on_motion), NULL);
    g_signal_connect(mo, "leave", G_CALLBACK(on_leave), NULL);
    gtk_widget_add_controller(H.area, mo);
    GtkEventController *sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(sc, "scroll", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_controller(H.area, sc);
    gtk_overlay_set_child(GTK_OVERLAY(frame), H.area);
    GtkWidget *hint = mc_label("Click: use the tool.  Right click: erase.  Drag empty space: turn.  Scroll: zoom.",
                               "dim");
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(hint, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(hint, 10);
    gtk_widget_set_can_target(hint, FALSE);
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_start(hint, 12);
    gtk_widget_set_margin_end(hint, 12);
    gtk_overlay_add_overlay(GTK_OVERLAY(frame), hint);
    gtk_box_append(GTK_BOX(mid), frame);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(bottom), mc_label("Name", NULL));
    H.name = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(H.name), 40);
    gtk_widget_set_hexpand(H.name, TRUE);
    g_signal_connect(H.name, "activate", G_CALLBACK(on_save), NULL);
    gtk_box_append(GTK_BOX(bottom), H.name);
    gtk_box_append(GTK_BOX(bottom), small_button("Save", "Ctrl+S", G_CALLBACK(on_save), NULL));
    gtk_box_append(GTK_BOX(mid), bottom);
    GtkWidget *bottom2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    H.wear = small_button("Wear it", "save it and put it on, in game too", G_CALLBACK(on_wear), a);
    gtk_widget_add_css_class(H.wear, "suggested-action");
    gtk_box_append(GTK_BOX(bottom2), H.wear);
    gtk_box_append(GTK_BOX(bottom2), small_button("Take off", NULL, G_CALLBACK(on_take_off), a));
    GtkWidget *del = small_button("Delete", "moves the saved file to the trash", G_CALLBACK(on_delete), a);
    gtk_widget_add_css_class(del, "mcl-danger");
    gtk_box_append(GTK_BOX(bottom2), del);
    gtk_box_append(GTK_BOX(mid), bottom2);
    GtkWidget *irow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    H.info = mc_label("", "dim");
    H.msg = mc_label("", NULL);
    gtk_widget_set_hexpand(H.msg, TRUE);
    gtk_label_set_xalign(GTK_LABEL(H.msg), 1);
    gtk_label_set_wrap(GTK_LABEL(H.msg), TRUE);
    gtk_label_set_wrap(GTK_LABEL(H.info), TRUE);
    gtk_box_append(GTK_BOX(irow), H.info);
    gtk_box_append(GTK_BOX(irow), H.msg);
    gtk_box_append(GTK_BOX(mid), irow);
    gtk_box_append(GTK_BOX(row), mid);

    /* right: colours and options */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *rsc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(rsc, 236, -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(rsc), right);
    GtkWidget *cc = mc_card("Colour", NULL);
    H.colour_btn = mc_colour_wheel_new(false, 190);
    mc_colour_on_change(H.colour_btn, on_colour, NULL);
    gtk_box_append(GTK_BOX(cc), H.colour_btn);
    GtkWidget *sw = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(sw), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(sw), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(sw), 5);
    for (size_t i = 0; i < G_N_ELEMENTS(SWATCHES); i++)
        gtk_flow_box_append(GTK_FLOW_BOX(sw), swatch(SWATCHES[i]));
    gtk_box_append(GTK_BOX(cc), sw);
    gtk_box_append(GTK_BOX(cc), mc_label("In this hat", "dim"));
    H.used = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(H.used), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(H.used), 8);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(H.used), 5);
    gtk_box_append(GTK_BOX(cc), H.used);
    gtk_box_append(GTK_BOX(right), cc);

    GtkWidget *oc = mc_card("Options", NULL);
    H.mirror_chk = gtk_check_button_new_with_label("Mirror left and right (M)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(H.mirror_chk), H.mirror);
    g_signal_connect(H.mirror_chk, "toggled", G_CALLBACK(on_mirror), NULL);
    gtk_box_append(GTK_BOX(oc), H.mirror_chk);
    H.head_chk = gtk_check_button_new_with_label("Show my head");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(H.head_chk), H.show_head);
    g_signal_connect(H.head_chk, "toggled", G_CALLBACK(on_head), NULL);
    gtk_box_append(GTK_BOX(oc), H.head_chk);
    GtkWidget *dr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(dr), mc_label("Cells", NULL));
    const char *const det[] = {"16 a side (chunky)", "32 a side (fine)", NULL};
    H.detail = gtk_drop_down_new_from_strings(det);
    gtk_widget_set_hexpand(H.detail, TRUE);
    g_signal_connect(H.detail, "notify::selected", G_CALLBACK(on_detail), NULL);
    gtk_box_append(GTK_BOX(dr), H.detail);
    gtk_box_append(GTK_BOX(oc), dr);
    gtk_box_append(GTK_BOX(right), oc);

    GtkWidget *mv = mc_card("Move it", "One cell at a time, as you see it now.");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    static const struct { const char *l; int dir, c, r; } MV[] = {
        {"Up", 4, 0, 0}, {"Back", 1, 1, 0}, {"Down", 5, 2, 0},
        {"Left", 2, 0, 1}, {"Front", 0, 1, 1}, {"Right", 3, 2, 1},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(MV); i++)
        gtk_grid_attach(GTK_GRID(grid), small_button(MV[i].l, NULL, G_CALLBACK(on_move), GINT_TO_POINTER(MV[i].dir)),
                        MV[i].c, MV[i].r, 1, 1);
    gtk_box_append(GTK_BOX(mv), grid);
    GtkWidget *clr = small_button("Clear everything", NULL, G_CALLBACK(on_clear), NULL);
    gtk_widget_add_css_class(clr, "mcl-danger");
    gtk_box_append(GTK_BOX(mv), clr);
    gtk_box_append(GTK_BOX(right), mv);
    gtk_box_append(GTK_BOX(row), rsc);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(page, keys);

    fill_lists();
    set_tool(T_ADD);
    set_colour(H.colour & 0xFFFFFF);

    /* start on the hat you wear, else the newest saved one, else an empty grid */
    mc_hat_t h = {0};
    char *worn = mc_tuxmc_get(a, "hat"), *dir = mc_hats_dir(), *p = NULL;
    if (worn && *worn)
        p = g_build_filename(dir, worn, NULL);
    const char *test = g_getenv("TUX_MC_HAT");                 /* screenshots: a preset by name */
    for (int i = 0; test && i < mc_hat_presets(); i++)
        if (!strcmp(test, mc_hat_preset_name(i)))
            mc_hat_preset(&h, i);
    if (h.n)
        edit(&h, NULL);
    else if (p && mc_hat_load(p, &h))
        edit(&h, p);
    else {
        mc_hat_new(&h, 16);
        g_strlcpy(h.name, "My hat", sizeof h.name);
        edit(&h, NULL);
    }
    g_free(p);
    g_free(dir);
    g_free(worn);
    return page;
}
