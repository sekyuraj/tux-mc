/* tux-mc — the Minecraft launcher, full screen, in the desktop theme. Prism does
 * the launching; this is the face.
 *
 *   Home      your player in 3D (drag to turn), capes, one big Start
 *   Skins     preview + upload skins, a library, steal one by player name
 *   Mods      installed mods (on/off, remove, drop files in), Modrinth search
 *   Configs   every file in config/, edited in place
 *   Settings  memory, speed (GPU threading, gamemode, ZGC), behaviour
 *
 * Background: the theme's wallpaper, slowly drifting (GPU, GtkSnapshot), under
 * a gradient of the theme colours and some floating dust. Pages slide.
 * Styling: ~/.config/tux-mc/style.css (.mcl-* rules), reloaded live.
 * Files: mc-net.c (account, HTTP), mc3d.c (the 3D player), mc-pages.c,
 * mc-configs.c; the client capes come from the mod's libtuxmc (capes.c).
 */
#include "mc.h"
#include "theme-black.h"
#include "tuxmc.h"

#include <curl/curl.h>
#ifdef _WIN32
#include <gdk/win32/gdkwin32.h>
#include <windows.h>
#endif
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define APP_ID "dev.tux.Mc"

/* ------------------------------------------------------------- helpers -- */
GtkWidget *mc_label(const char *text, const char *cls)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    if (cls)
        gtk_widget_add_css_class(l, cls);
    return l;
}

GtkWidget *mc_card(const char *title, const char *subtitle)
{
    GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(c, "mcl-glass");
    if (title)
        gtk_box_append(GTK_BOX(c), mc_label(title, "mcl-h2"));
    if (subtitle) {
        GtkWidget *s = mc_label(subtitle, "dim");
        gtk_label_set_wrap(GTK_LABEL(s), TRUE);
        gtk_box_append(GTK_BOX(c), s);
    }
    return c;
}

void mc_status(app_t *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(a->status), s);
    gtk_widget_remove_css_class(a->status, "show");
    gtk_widget_add_css_class(a->status, "show");
    g_free(s);
}

void mc_pal(app_t *a, int i, GdkRGBA *c) { gtk_widget_get_color(a->pal[i], c); }

static cairo_status_t png_read(void *closure, unsigned char *data, unsigned int len)
{
    GBytes **b = closure;
    gsize n;
    const guchar *p = g_bytes_get_data(*b, &n);
    if (len > n)
        return CAIRO_STATUS_READ_ERROR;
    memcpy(data, p, len);
    GBytes *rest = g_bytes_new_from_bytes(*b, len, n - len);
    g_bytes_unref(*b);
    *b = rest;
    return CAIRO_STATUS_SUCCESS;
}

cairo_surface_t *mc_png_bytes(GBytes *bytes)
{
    GBytes *b = g_bytes_ref(bytes);
    cairo_surface_t *s = cairo_image_surface_create_from_png_stream(png_read, &b);
    g_bytes_unref(b);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return NULL;
    }
    return s;
}

bool mc_prism_running(void) { return mc_sys_prism_running(); }

bool mc_game_running(app_t *a) { return mc_sys_game_running(a->instdir); }

void mc_open_path(app_t *a, const char *path)
{
    char *uri = g_filename_to_uri(path, NULL, NULL);
    GError *e = NULL;
    if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &e)) {
        mc_status(a, "Couldn't open %s: %s", path, e ? e->message : "bad path");
        g_clear_error(&e);
    }
    g_free(uri);
}

/* config/tuxmc.properties: java.util.Properties, the mod re-reads it every 2 s */
static char *props_path(app_t *a) { return g_build_filename(a->mcdir, "config", "tuxmc.properties", NULL); }

char *mc_tuxmc_get(app_t *a, const char *key)
{
    char *path = props_path(a), *txt = NULL, *res = NULL;
    if (g_file_get_contents(path, &txt, NULL, NULL)) {
        char **l = g_strsplit(txt, "\n", -1);
        size_t kl = strlen(key);
        for (char **p = l; *p; p++)
            if (!strncmp(*p, key, kl) && (*p)[kl] == '=') {
                g_free(res);
                res = g_strcompress(g_strstrip((*p) + kl + 1));   /* Properties escapes "\:" etc. */
            }
        g_strfreev(l);
        g_free(txt);
    }
    g_free(path);
    return res;
}

void mc_tuxmc_set(app_t *a, const char *key, const char *value)
{
    char *path = props_path(a), *txt = NULL;
    GString *esc = g_string_new(NULL);                       /* Properties: escape \ = : */
    for (const char *c = value; *c; c++) {
        if (*c == '\\' || *c == '=' || *c == ':')
            g_string_append_c(esc, '\\');
        g_string_append_c(esc, *c);
    }
    GString *out = g_string_new(NULL);
    bool done = false;
    size_t kl = strlen(key);
    if (g_file_get_contents(path, &txt, NULL, NULL)) {
        char **l = g_strsplit(txt, "\n", -1);
        for (char **p = l; *p; p++) {
            if (!**p && !p[1])
                break;
            if (!strncmp(*p, key, kl) && (*p)[kl] == '=') {
                g_string_append_printf(out, "%s=%s\n", key, esc->str);
                done = true;
            } else {
                g_string_append_printf(out, "%s\n", *p);
            }
        }
        g_strfreev(l);
        g_free(txt);
    }
    if (!done)
        g_string_append_printf(out, "%s=%s\n", key, esc->str);
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_file_set_contents(path, out->str, out->len, NULL);
    g_free(dir);
    g_string_free(out, TRUE);
    g_string_free(esc, TRUE);
    g_free(path);
}

/* ---------------------------------------------------------- background -- */
/* a widget that draws the wallpaper with the GPU, slowly zooming and panning */
#define TUX_TYPE_BG (tux_bg_get_type())
G_DECLARE_FINAL_TYPE(TuxBg, tux_bg, TUX, BG, GtkWidget)

#define NDUST 70

struct _TuxBg {
    GtkWidget parent;
    GdkTexture *tex;
    app_t *a;
    double t;
    gint64 last;
    float dx[NDUST], dy[NDUST], ds[NDUST], dv[NDUST];
};

G_DEFINE_TYPE(TuxBg, tux_bg, GTK_TYPE_WIDGET)

static void tux_bg_snapshot(GtkWidget *w, GtkSnapshot *s)
{
    TuxBg *b = TUX_BG(w);
    float W = gtk_widget_get_width(w), H = gtk_widget_get_height(w);
    GdkRGBA bg, acc, surf;
    mc_pal(b->a, PAL_BG, &bg);
    mc_pal(b->a, PAL_ACCENT, &acc);
    mc_pal(b->a, PAL_SURFACE, &surf);
    gtk_snapshot_append_color(s, &bg, &GRAPHENE_RECT_INIT(0, 0, W, H));
    if (b->tex) {
        float tw = gdk_texture_get_width(b->tex), th = gdk_texture_get_height(b->tex);
        float cover = fmaxf(W / tw, H / th);
        float m = b->a->bg_motion ? 1 : 0;
        float zoom = 1.08f + 0.05f * sinf(b->t * 0.045f) * m;
        float sc = cover * zoom;
        float ox = (W - tw * sc) / 2 + sinf(b->t * 0.031f) * W * 0.025f * m;
        float oy = (H - th * sc) / 2 + cosf(b->t * 0.027f) * H * 0.02f * m;
        gtk_snapshot_save(s);
        gtk_snapshot_push_blur(s, b->a->bg_blur);
        gtk_snapshot_append_scaled_texture(s, b->tex, GSK_SCALING_FILTER_TRILINEAR,
                                           &GRAPHENE_RECT_INIT(ox, oy, tw * sc, th * sc));
        gtk_snapshot_pop(s);
        gtk_snapshot_restore(s);
    }
    /* theme gradient: dark at the sides, a little accent light low in the middle */
    GdkRGBA a0 = bg, a1 = bg;
    a0.alpha = 0.88;
    a1.alpha = 0.45;
    GskColorStop stops[] = {{0, a0}, {0.55, a1}, {1, a0}};
    gtk_snapshot_append_linear_gradient(s, &GRAPHENE_RECT_INIT(0, 0, W, H), &GRAPHENE_POINT_INIT(0, 0),
                                        &GRAPHENE_POINT_INIT(W, 0), stops, 3);
    GdkRGBA g0 = acc, g1 = acc;
    g0.alpha = 0.13 + 0.04 * sin(b->t * 0.6);
    g1.alpha = 0;
    GskColorStop rs[] = {{0, g0}, {1, g1}};
    gtk_snapshot_append_radial_gradient(s, &GRAPHENE_RECT_INIT(0, 0, W, H), &GRAPHENE_POINT_INIT(W * 0.36f, H * 0.62f),
                                        W * 0.35f, H * 0.45f, 0, 1, rs, 2);
    /* dust */
    for (int i = 0; b->a->bg_dust && i < NDUST; i++) {
        float x = fmodf(b->dx[i] + b->t * b->dv[i] * 6, 1.2f) - 0.1f;
        float y = fmodf(b->dy[i] - b->t * b->dv[i] * 10 + 10, 1.2f) - 0.1f;
        GdkRGBA c = i % 3 ? acc : surf;
        c.alpha = 0.25f + 0.25f * sinf(b->t * (0.5f + b->dv[i]) + i);
        float sz = b->ds[i];
        GskRoundedRect rr;
        gsk_rounded_rect_init_from_rect(&rr, &GRAPHENE_RECT_INIT(x * W, y * H, sz, sz), sz / 2);
        gtk_snapshot_push_rounded_clip(s, &rr);
        gtk_snapshot_append_color(s, &c, &GRAPHENE_RECT_INIT(x * W, y * H, sz, sz));
        gtk_snapshot_pop(s);
    }
}

static gboolean tux_bg_tick(GtkWidget *w, GdkFrameClock *fc, gpointer d)
{
    (void)d;
    TuxBg *b = TUX_BG(w);
    gint64 now = gdk_frame_clock_get_frame_time(fc);
    if (b->last)
        b->t += MIN((now - b->last) / 1e6, 0.1);
    b->last = now;
    gtk_widget_queue_draw(w);
    return G_SOURCE_CONTINUE;
}

static void tux_bg_dispose(GObject *o)
{
    g_clear_object(&TUX_BG(o)->tex);
    G_OBJECT_CLASS(tux_bg_parent_class)->dispose(o);
}

static void tux_bg_class_init(TuxBgClass *k)
{
    GTK_WIDGET_CLASS(k)->snapshot = tux_bg_snapshot;
    G_OBJECT_CLASS(k)->dispose = tux_bg_dispose;
}

static void tux_bg_init(TuxBg *b)
{
    for (int i = 0; i < NDUST; i++) {
        b->dx[i] = g_random_double();
        b->dy[i] = g_random_double();
        b->ds[i] = 1.5 + g_random_double() * 2.5;
        b->dv[i] = 0.004 + g_random_double() * 0.01;
    }
    gtk_widget_add_tick_callback(GTK_WIDGET(b), tux_bg_tick, NULL, NULL);
}

static char *theme_wallpaper(void)
{
    char *dir = g_build_filename(g_get_user_config_dir(), "themes", "current", NULL);
    char *sh = g_build_filename(dir, "theme.sh", NULL), *txt = NULL, *res = NULL;
    if (g_file_get_contents(sh, &txt, NULL, NULL)) {
        char **l = g_strsplit(txt, "\n", -1);
        for (char **p = l; *p && !res; p++)
            if (g_str_has_prefix(*p, "wallpaper=")) {
                char *v = g_strstrip(g_strdup(*p + 10));
                char *hash = strchr(v, '#');
                if (hash)
                    *hash = 0;
                g_strstrip(v);
                char *q = v;
                if (*q == '"' || *q == '\'') {
                    q++;
                    q[strlen(q) - 1] = 0;
                }
                res = g_build_filename(dir, q, NULL);
                g_free(v);
            }
        g_strfreev(l);
        g_free(txt);
    }
    g_free(sh);
    g_free(dir);
    return res;
}

static void bg_load(TuxBg *b)
{
    char *path = theme_wallpaper();
    g_clear_object(&b->tex);
    if (path)
        b->tex = gdk_texture_new_from_filename(path, NULL);
    g_free(path);
}

/* ---------------------------------------------------------------- home -- */
static gboolean home_tick(GtkWidget *w, GdkFrameClock *fc, gpointer data)
{
    app_t *a = data;
    gint64 now = gdk_frame_clock_get_frame_time(fc);
    double dt = a->last && now - a->last < 200000 ? (now - a->last) / 1e6 : 0;   /* page was hidden: no jump */
    a->last = now;
    mc3d_tick(&a->view, dt);
    a->view.wave = fmax(0, a->view.wave - dt * 0.35);
    a->glow += ((a->launching ? 1 : 0) - a->glow) * (1 - exp(-3 * dt));
    a->progress += (a->progress_target - a->progress) * (1 - exp(-2.5 * dt));
    gtk_widget_queue_draw(w);
    if (a->launching)
        gtk_widget_queue_draw(a->start);
    return G_SOURCE_CONTINUE;
}

/* the Skins page's player: its own clock, ticks only while that page shows */
static gboolean skin_tick(GtkWidget *w, GdkFrameClock *fc, gpointer data)
{
    app_t *a = data;
    static gint64 last;
    gint64 now = gdk_frame_clock_get_frame_time(fc);
    double dt = last && now - last < 200000 ? (now - last) / 1e6 : 0;
    last = now;
    mc3d_tick(&a->skin_view, dt);
    a->skin_view.wave = fmax(0, a->skin_view.wave - dt * 0.35);
    gtk_widget_queue_draw(w);
    return G_SOURCE_CONTINUE;
}

static void draw_stage(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    app_t *a = data;
    GdkRGBA bg;
    mc_pal(a, PAL_BG, &bg);
    double sh[3] = {bg.red * 0.5, bg.green * 0.5, bg.blue * 0.5};
    mc3d_draw(&a->view, cr, w, h, sh);
}

/* drag to turn; the pointer position steers the head */
static void on_drag_begin(GtkGestureDrag *g, double x, double y, gpointer data)
{
    (void)g;
    app_t *a = data;
    a->drag_x = x;
    a->drag_y = y;
    a->drag_t = g_get_monotonic_time();
    a->drag_vx = 0;
}

static mc3d_t *view_of(GtkGesture *g, app_t *a)
{
    return gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g)) == a->stage ? &a->view : &a->skin_view;
}

static void on_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    app_t *a = data;
    gint64 now = g_get_monotonic_time();
    double dx = ox - a->drag_x, dy = oy - a->drag_y;
    double dt = MAX((now - a->drag_t) / 1e6, 1e-3);
    a->drag_vx = a->drag_vx * 0.6 + dx / dt * 0.4;
    a->drag_x = ox;
    a->drag_y = oy;
    a->drag_t = now;
    mc3d_drag(view_of(GTK_GESTURE(g), a), dx, dy);
}

static void on_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)ox;
    (void)oy;
    app_t *a = data;
    mc3d_t *v = view_of(GTK_GESTURE(g), a);
    if (fabs(a->drag_vx) < 1 && fabs(ox) + fabs(oy) < 3)
        v->wave = 1;                     /* a click: wave */
    mc3d_release(v, a->drag_vx);
}

static void on_stage_motion(GtkEventControllerMotion *m, double x, double y, gpointer data)
{
    app_t *a = data;
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(m));
    mc3d_t *v = w == a->stage ? &a->view : &a->skin_view;
    v->look_x = CLAMP((x / gtk_widget_get_width(w) - 0.5) * 2, -1, 1);
    v->look_y = CLAMP((y / gtk_widget_get_height(w) - 0.3) * 1.5, -1, 1);
}

GtkWidget *mc_stage(app_t *a, mc3d_t *v, GtkDrawingAreaDrawFunc draw)
{
    GtkWidget *st = gtk_drawing_area_new();
    gtk_widget_set_hexpand(st, TRUE);
    gtk_widget_set_vexpand(st, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(st), draw, a, NULL);
    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), a);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), a);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), a);
    gtk_widget_add_controller(st, GTK_EVENT_CONTROLLER(drag));
    GtkEventController *mo = gtk_event_controller_motion_new();
    g_signal_connect(mo, "motion", G_CALLBACK(on_stage_motion), a);
    gtk_widget_add_controller(st, mo);
    gtk_widget_set_cursor_from_name(st, "grab");
    if (v == &a->skin_view)
        gtk_widget_add_tick_callback(st, skin_tick, a, NULL);
    return st;
}

/* ---- capes on the home page ---- */
static const struct { const char *id, *name; } TUX_CAPES[] = {
    {"tux", "Tux"},         {"gentoo", "Gentoo"}, {"steamos", "SteamOS"},  {"arch", "Arch"},
    {"debian", "Debian"},   {"mint", "Mint"},     {"freebsd", "FreeBSD"},  {"openbsd", "OpenBSD"},
    {"lfs", "LFS"},         {"crux", "CRUX"},     {"windows11", "Windows 11"}, {"stars", "Starfield"},  {"synth", "Synthwave"},
    {"image", "New picture…"},  {"none", "Off"},
};
#define NTUX (int)G_N_ELEMENTS(TUX_CAPES)
_Static_assert(G_N_ELEMENTS(TUX_CAPES) <= G_N_ELEMENTS(((app_t *)0)->tux_chips), "more capes than chip slots");
static cairo_surface_t *tux_preview[NTUX];

static cairo_surface_t *active_cape(app_t *a)
{
    for (int i = 0; i < a->ncapes; i++)
        if (a->active_cape && !strcmp(a->capes[i].id, a->active_cape))
            return a->capes[i].tex;
    return NULL;
}

/* "#rrggbb" from theme.sh, as opaque ARGB */
static guint32 theme_color(const char *key, guint32 def)
{
    char *sh = g_build_filename(g_get_user_config_dir(), "themes", "current", "theme.sh", NULL), *txt = NULL;
    guint32 res = def;
    if (g_file_get_contents(sh, &txt, NULL, NULL)) {
        char *pre = g_strconcat("\n", key, "=", NULL), *at = strstr(txt, pre);
        if (at) {
            char *h = strchr(at + strlen(pre), '#');
            if (h)
                res = 0xFF000000u | (guint32)strtoul(h + 1, NULL, 16);
        }
        g_free(pre);
        g_free(txt);
    }
    g_free(sh);
    return res;
}

/* any image as straight-alpha ARGB for libtuxmc */
static guint32 *load_argb(const char *path, int *w, int *h)
{
    GdkTexture *t = path && *path ? gdk_texture_new_from_filename(path, NULL) : NULL;
    if (!t)
        return NULL;
    *w = gdk_texture_get_width(t);
    *h = gdk_texture_get_height(t);
    guint32 *px = g_new(guint32, (gsize)*w * *h);
    gdk_texture_download(t, (guchar *)px, *w * 4);   /* premultiplied ARGB32 */
    g_object_unref(t);
    for (int i = 0; i < *w * *h; i++) {
        guint32 c = px[i], al = c >> 24;
        if (al && al < 255) {
            guint32 r = MIN(255u, ((c >> 16) & 255) * 255 / al), g = MIN(255u, ((c >> 8) & 255) * 255 / al),
                    b = MIN(255u, (c & 255) * 255 / al);
            px[i] = al << 24 | r << 16 | g << 8 | b;
        }
    }
    return px;
}

static char *expand_home(const char *p)
{
    return p && p[0] == '~' ? g_strconcat(g_get_home_dir(), p + 1, NULL) : g_strdup(p ? p : "");
}

/* paint a client cape the way the mod does; NULL for none / official */
static cairo_surface_t *paint_tux_cape(app_t *a, const char *id, int s)
{
    bool known = false;
    for (int i = 0; tuxmc_cape_ids[i]; i++)
        known |= !strcmp(tuxmc_cape_ids[i], id);
    if (!known)
        return NULL;
    tuxmc_pal_t pal = {theme_color("accent", 0xFFFFFFFF), theme_color("selection", 0xFF333333),
                       theme_color("bg", 0xFF000000), theme_color("fg_bright", 0xFFFFFFFF),
                       theme_color("warn", 0xFFD9D9D9)};
    tuxmc_img_t img = {0};
    guint32 *px = NULL;
    if (tuxmc_cape_logo(id)) {
        char *f = g_strconcat(id, "-cape.png", NULL);
        char *p = g_build_filename(g_get_user_data_dir(), "tux-mc", "data", f, NULL);
#ifdef _WIN32
        if (!g_file_test(p, G_FILE_TEST_EXISTS)) {      /* next to the .exe: <root>\share\tux-mc\data */
            char *root = g_win32_get_package_installation_directory_of_module(NULL);
            g_free(p);
            p = g_build_filename(root, "share", "tux-mc", "data", f, NULL);
            g_free(root);
        }
#endif
        if (!g_file_test(p, G_FILE_TEST_EXISTS)) {
            g_free(p);
            p = g_build_filename(g_get_user_data_dir(), "tux-control-center", "data", f, NULL);
        }
        g_free(f);
        px = load_argb(p, &img.w, &img.h);
        g_free(p);
    } else if (!strcmp(id, "image")) {
        char *v = mc_tuxmc_get(a, "capeImage"), *p = expand_home(v);
        px = load_argb(p, &img.w, &img.h);
        g_free(p);
        g_free(v);
    }
    img.px = px;
    int w = 64 * s, h = 32 * s;
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    guint32 *out = (guint32 *)cairo_image_surface_get_data(sf);
    tuxmc_cape(id, s, &pal, px ? &img : NULL, out);
    for (int i = 0; i < w * h; i++) {    /* straight -> premultiplied for Cairo */
        guint32 c = out[i], al = c >> 24;
        if (al < 255)
            out[i] = al << 24 | (((c >> 16) & 255) * al / 255) << 16 | (((c >> 8) & 255) * al / 255) << 8 |
                     ((c & 255) * al / 255);
    }
    cairo_surface_mark_dirty(sf);
    g_free(px);
    return sf;
}

static void repaint_tux_capes(app_t *a)
{
    if (a->tux_cape_tex)
        cairo_surface_destroy(a->tux_cape_tex);
    /* logo capes at 32x (2048x1024), like in game: thin lines stay sharp on the model */
    a->tux_cape_tex = a->tux_cape ? paint_tux_cape(a, a->tux_cape, tuxmc_cape_logo(a->tux_cape) ? 32 : 8) : NULL;
    for (int i = 0; i < NTUX; i++) {
        if (tux_preview[i])
            cairo_surface_destroy(tux_preview[i]);
        tux_preview[i] = paint_tux_cape(a, TUX_CAPES[i].id, 2);
        if (a->tux_chips[i])
            gtk_widget_queue_draw(a->tux_chips[i]);
    }
}

void mc_home_refresh_capes(app_t *a)
{
    for (int i = 0; i < a->ncapes; i++) {
        bool on = a->active_cape && !strcmp(a->capes[i].id, a->active_cape);
        if (on)
            gtk_widget_add_css_class(a->capes[i].tile, "active");
        else
            gtk_widget_remove_css_class(a->capes[i].tile, "active");
        if (a->capes[i].area)
            gtk_widget_queue_draw(a->capes[i].area);
    }
    if (a->active_cape)
        gtk_widget_remove_css_class(a->none_tile, "active");
    else
        gtk_widget_add_css_class(a->none_tile, "active");
    char *img = mc_tuxmc_get(a, "capeImage");
    bool pic = !g_strcmp0(a->tux_cape, "image"), matched = false;
    for (guint i = 0; a->pic_chips && i < a->pic_chips->len; i++) {
        GtkWidget *c = a->pic_chips->pdata[i];
        bool on = pic && !g_strcmp0(img, g_object_get_data(G_OBJECT(c), "png"));
        matched |= on;
        if (on)
            gtk_widget_add_css_class(c, "active");
        else
            gtk_widget_remove_css_class(c, "active");
    }
    g_free(img);
    for (int i = 0; i < NTUX; i++) {
        if (!a->tux_chips[i])
            continue;
        bool on = !g_strcmp0(a->tux_cape, TUX_CAPES[i].id) && !(pic && matched);
        if (on)
            gtk_widget_add_css_class(a->tux_chips[i], "active");
        else
            gtk_widget_remove_css_class(a->tux_chips[i], "active");
    }
    /* in game the client cape wins over the Mojang one, so show that */
    a->view.cape = a->skin_view.cape = a->tux_cape_tex ? a->tux_cape_tex : active_cape(a);
}

static void blit(cairo_t *cr, cairo_surface_t *tex, double u, double v, double w, double h, double x, double y,
                 double s)
{
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w * s, h * s);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, tex, -u, -v);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void draw_cape_tile(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    cape_t *k = data;
    double s = MIN(w / 10.0, h / 16.0);
    double x = (w - 10 * s) / 2, y = (h - 16 * s) / 2;
    if (k->tex) {
        double cw = cairo_image_surface_get_width(k->tex) / 64.0;
        blit(cr, k->tex, cw, cw, 10 * cw, 16 * cw, x, y, s / cw);
    }
}

/* change the Mojang cape (thread) */
typedef struct { app_t *a; char *id; long code; char *err; } cape_req_t;

static void cape_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    cape_req_t *r = data;
    char *tok = mc_auth_token(r->a, &r->err);
    if (tok) {
        char *body = r->id ? g_strdup_printf("{\"capeId\":\"%s\"}", r->id) : NULL;
        r->code = mc_http(r->id ? "PUT" : "DELETE", "https://api.minecraftservices.com/minecraft/profile/capes/active",
                          tok, body ? "application/json" : NULL, body, NULL);
        g_free(body);
        g_free(tok);
    }
    g_task_return_boolean(t, TRUE);
}

static void cape_done(GObject *s, GAsyncResult *res, gpointer data)
{
    (void)s;
    (void)res;
    cape_req_t *r = data;
    app_t *a = r->a;
    a->busy = false;
    if (r->code == 200) {
        g_free(a->active_cape);
        a->active_cape = g_strdup(r->id);
        const char *name = "no cape";
        for (int i = 0; i < a->ncapes; i++)
            if (r->id && !strcmp(a->capes[i].id, r->id))
                name = a->capes[i].alias;
        if (a->tux_cape_tex)
            mc_status(a, "Cape: %s. Everyone else sees it; you see your client cape (set it to Off to see this one).",
                      name);
        else
            mc_status(a, "Cape: %s. Everyone sees it now.", name);
        mc3d_show_back(&a->view);
    } else {
        mc_status(a, "Couldn't change the cape: %s", r->err ? r->err : r->code ? "Mojang refused" : "no connection");
    }
    mc_home_refresh_capes(a);
    g_free(r->id);
    g_free(r->err);
    g_free(r);
}

static void set_cape(app_t *a, const char *id)
{
    if (a->busy || !g_strcmp0(id, a->active_cape))
        return;
    a->busy = true;
    mc_status(a, "Asking Mojang…");
    cape_req_t *r = g_new0(cape_req_t, 1);
    r->a = a;
    r->id = g_strdup(id);
    GTask *t = g_task_new(NULL, NULL, cape_done, r);
    g_task_set_task_data(t, r, NULL);
    g_task_run_in_thread(t, cape_thread);
    g_object_unref(t);
}

static void on_cape(GtkButton *b, gpointer d) { (void)b; cape_t *k = d; set_cape(k->a, k->id); }
static void on_no_cape(GtkButton *b, gpointer d) { (void)b; set_cape(d, NULL); }

static void set_tux_cape(app_t *a, const char *id)
{
    mc_tuxmc_set(a, "cape", id);
    g_free(a->tux_cape);
    a->tux_cape = g_strdup(id);
    repaint_tux_capes(a);
    mc_home_refresh_capes(a);
    if (strcmp(id, "none"))
        mc3d_show_back(&a->view);
}

static void fill_cape_chips(app_t *a);

/* the editor saved the cape-shaped picture (a new preset, or one edited again) */
static void picture_saved(app_t *a, const char *path)
{
    mc_tuxmc_set(a, "capeImage", path);
    fill_cape_chips(a);                  /* its chip: new, or with the new picture/name */
    set_tux_cape(a, "image");
    mc_status(a, "Picture cape saved: right-click it to edit or delete. Other tux-mc players see it too.");
}

static void on_picture_done(GObject *src, GAsyncResult *res, gpointer data)
{
    app_t *a = data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f)
        return;
    char *p = g_file_get_path(f);
    mc_cape_editor(a, p, NULL, picture_saved);   /* crop / stretch it, name it */
    g_free(p);
    g_object_unref(f);
}

static void choose_picture(app_t *a)
{
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Picture for the cape");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Pictures");
    gtk_file_filter_add_mime_type(ff, "image/png");
    gtk_file_filter_add_mime_type(ff, "image/jpeg");
    gtk_file_filter_add_mime_type(ff, "image/webp");
    gtk_file_filter_add_mime_type(ff, "image/gif");
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(fl));
    const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    if (pics) {
        GFile *pf = g_file_new_for_path(pics);
        gtk_file_dialog_set_initial_folder(d, pf);
        g_object_unref(pf);
    }
    gtk_file_dialog_open(d, a->win, NULL, on_picture_done, a);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(d);
}

static void on_tux_chip(GtkButton *b, gpointer data)
{
    app_t *a = data;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "idx"));
    if (!strcmp(TUX_CAPES[i].id, "image")) {
        choose_picture(a);
        return;
    }
    set_tux_cape(a, TUX_CAPES[i].id);
    mc_status(a, !strcmp(TUX_CAPES[i].id, "none") ? "Client cape off: you see your Mojang cape"
                                                  : "Client cape %s: other tux-mc players see it too", TUX_CAPES[i].name);
}

static void load_tux_cape(app_t *a)
{
    a->tux_cape = mc_tuxmc_get(a, "cape");
    /* no cape yet, or one that was removed in 1.2: Tux */
    if (!a->tux_cape || !strcmp(a->tux_cape, "theme") || !strcmp(a->tux_cape, "aurora") ||
        !strcmp(a->tux_cape, "ember") || !strcmp(a->tux_cape, "ocean")) {
        g_free(a->tux_cape);
        a->tux_cape = g_strdup("tux");
    }
}

/* a chip's swatch: the cape itself, its outside face */
static void draw_chip(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    int i = GPOINTER_TO_INT(data);
    cairo_surface_t *s = tux_preview[i];
    if (!s) {                            /* Off */
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.35);
        cairo_rectangle(cr, 0.5, 0.5, w - 1, h - 1);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        return;
    }
    double k = MIN(w / 10.0, h / 16.0);
    cairo_translate(cr, (w - 10 * k) / 2, (h - 16 * k) / 2);
    cairo_scale(cr, k / 2, k / 2);       /* previews are painted at 2x */
    cairo_rectangle(cr, 0, 0, 20, 32);
    cairo_clip(cr);
    cairo_set_source_surface(cr, s, -2, -2);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
}

/* ---- saved picture capes: <data>/tux-mc/capes/<id>.png + <id>.ini ---- */

static void draw_pic_chip(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    cairo_surface_t *s = data;
    if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS)
        return;
    double sw = cairo_image_surface_get_width(s), sh = cairo_image_surface_get_height(s);
    double k = MIN(w / sw, h / sh);
    cairo_translate(cr, (w - sw * k) / 2, (h - sh * k) / 2);
    cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
}

static void on_pic_chip(GtkButton *b, gpointer data)
{
    app_t *a = data;
    mc_tuxmc_set(a, "capeImage", g_object_get_data(G_OBJECT(b), "png"));
    set_tux_cape(a, "image");
    mc_status(a, "Cape “%s”: other tux-mc players see it too", (char *)g_object_get_data(G_OBJECT(b), "name"));
}

static void on_pic_edit(GtkButton *b, gpointer data)
{
    app_t *a = data;
    GtkWidget *chip = g_object_get_data(G_OBJECT(b), "chip");
    gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(chip), "menu")));
    mc_cape_editor(a, g_object_get_data(G_OBJECT(chip), "png"), g_object_get_data(G_OBJECT(chip), "id"),
                   picture_saved);
}

static void on_pic_delete(GtkButton *b, gpointer data)
{
    app_t *a = data;
    GtkWidget *chip = g_object_get_data(G_OBJECT(b), "chip");
    gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(chip), "menu")));
    const char *png = g_object_get_data(G_OBJECT(chip), "png");
    char *name = g_strdup(g_object_get_data(G_OBJECT(chip), "name"));
    char *ini = g_strdup(png);
    strcpy(ini + strlen(ini) - 4, ".ini");
    char *cur = mc_tuxmc_get(a, "capeImage");
    bool wearing = !g_strcmp0(a->tux_cape, "image") && !g_strcmp0(cur, png);
    GFile *fp = g_file_new_for_path(png), *fi = g_file_new_for_path(ini);
    bool ok = g_file_trash(fp, NULL, NULL);       /* the trash, so a slip can be undone */
    g_file_trash(fi, NULL, NULL);
    g_object_unref(fp);
    g_object_unref(fi);
    if (ok) {
        if (wearing)
            set_tux_cape(a, "none");
        fill_cape_chips(a);
        mc_home_refresh_capes(a);
        mc_status(a, "Deleted “%s” (it's in the trash)", name);
    } else {
        mc_status(a, "Couldn't delete “%s”", name);
    }
    g_free(cur);
    g_free(ini);
    g_free(name);
}

static void on_pic_menu(GtkGestureClick *g, int n, double x, double y, gpointer data)
{
    (void)n;
    (void)x;
    (void)y;
    (void)data;
    GtkWidget *chip = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_popover_popup(GTK_POPOVER(g_object_get_data(G_OBJECT(chip), "menu")));
}

static GtkWidget *menu_item(GtkWidget *box, const char *label, GCallback cb, GtkWidget *chip, app_t *a)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(b))), 0);
    g_object_set_data(G_OBJECT(b), "chip", chip);
    g_signal_connect(b, "clicked", cb, a);
    gtk_box_append(GTK_BOX(box), b);
    return b;
}

static int pic_cmp(gconstpointer x, gconstpointer y) { return g_strcmp0(*(char *const *)x, *(char *const *)y); }

static GtkWidget *pic_chip(app_t *a, const char *dir, const char *id)
{
    char *png = g_strdup_printf("%s%s%s.png", dir, G_DIR_SEPARATOR_S, id);
    char *inip = g_strdup_printf("%s%s%s.ini", dir, G_DIR_SEPARATOR_S, id);
    GKeyFile *k = g_key_file_new();
    char *name = g_key_file_load_from_file(k, inip, G_KEY_FILE_NONE, NULL)
                     ? g_key_file_get_string(k, "cape", "name", NULL) : NULL;
    g_key_file_unref(k);
    g_free(inip);
    if (!name)
        name = g_strdup("Picture");
    cairo_surface_t *sf = cairo_image_surface_create_from_png(png);
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "mcl-chip");
    gtk_widget_set_tooltip_text(b, "Right-click to edit or delete");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *sw = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(sw), 10);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(sw), 16);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(sw), draw_pic_chip, sf, (GDestroyNotify)cairo_surface_destroy);
    gtk_box_append(GTK_BOX(box), sw);
    GtkWidget *l = mc_label(name, NULL);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 9);
    gtk_box_append(GTK_BOX(box), l);
    gtk_button_set_child(GTK_BUTTON(b), box);
    g_object_set_data_full(G_OBJECT(b), "png", png, g_free);
    g_object_set_data_full(G_OBJECT(b), "id", g_strdup(id), g_free);
    g_object_set_data_full(G_OBJECT(b), "name", name, g_free);
    g_signal_connect(b, "clicked", G_CALLBACK(on_pic_chip), a);

    GtkWidget *pop = gtk_popover_new();
    gtk_widget_set_parent(pop, b);
    g_signal_connect_swapped(b, "destroy", G_CALLBACK(gtk_widget_unparent), pop);
    GtkWidget *mb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    menu_item(mb, "Edit", G_CALLBACK(on_pic_edit), b, a);
    GtkWidget *del = menu_item(mb, "Delete", G_CALLBACK(on_pic_delete), b, a);
    gtk_widget_add_css_class(del, "mcl-danger");
    gtk_popover_set_child(GTK_POPOVER(pop), mb);
    g_object_set_data(G_OBJECT(b), "menu", pop);
    GtkGesture *rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(on_pic_menu), a);
    gtk_widget_add_controller(b, GTK_EVENT_CONTROLLER(rc));
    return b;
}

/* built-in capes, then your saved pictures, then "New picture…" and Off */
static void fill_cape_chips(app_t *a)
{
    if (!a->cape_flow)
        return;
    gtk_flow_box_remove_all(GTK_FLOW_BOX(a->cape_flow));
    for (int i = 0; i < NTUX; i++)
        a->tux_chips[i] = NULL;
    if (a->pic_chips)
        g_ptr_array_set_size(a->pic_chips, 0);
    else
        a->pic_chips = g_ptr_array_new();
    char *dir = mc_capes_dir();
    GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
    GDir *d = g_dir_open(dir, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));)
        if (g_str_has_suffix(e, ".png"))
            g_ptr_array_add(ids, g_strndup(e, strlen(e) - 4));
    if (d)
        g_dir_close(d);
    g_ptr_array_sort(ids, pic_cmp);                 /* picture-<time>: oldest first */
    for (int i = 0; i < NTUX; i++) {
        if (!strcmp(TUX_CAPES[i].id, "image"))     /* the saved ones go before "New picture…" */
            for (guint j = 0; j < ids->len; j++) {
                GtkWidget *c = pic_chip(a, dir, ids->pdata[j]);
                g_ptr_array_add(a->pic_chips, c);
                gtk_flow_box_append(GTK_FLOW_BOX(a->cape_flow), c);
            }
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "mcl-chip");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *sw = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(sw), 10);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(sw), 16);
        g_object_set_data(G_OBJECT(sw), "app", a);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(sw), draw_chip, GINT_TO_POINTER(i), NULL);
        gtk_box_append(GTK_BOX(box), sw);
        gtk_box_append(GTK_BOX(box), mc_label(TUX_CAPES[i].name, NULL));
        gtk_button_set_child(GTK_BUTTON(b), box);
        g_object_set_data(G_OBJECT(b), "idx", GINT_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(on_tux_chip), a);
        a->tux_chips[i] = b;
        gtk_flow_box_append(GTK_FLOW_BOX(a->cape_flow), b);
    }
    g_ptr_array_unref(ids);
    g_free(dir);
    mc_home_refresh_capes(a);
}

/* cape textures: ~/.cache/tux-mc/capes/<id>.png, fetched once */
static void fetch_capes_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    app_t *a = data;
    char *dir = g_build_filename(g_get_user_cache_dir(), "tux-mc", "capes", NULL);
    g_mkdir_with_parents(dir, 0755);
    for (int i = 0; i < a->ncapes; i++) {
        char *f = g_strdup_printf("%s/%s.png", dir, a->capes[i].id);
        if (!g_file_test(f, G_FILE_TEST_EXISTS) && *a->capes[i].url) {
            GByteArray *b = g_byte_array_new();
            if (mc_http(NULL, a->capes[i].url, NULL, NULL, NULL, b) == 200)
                g_file_set_contents(f, (char *)b->data, b->len, NULL);
            g_byte_array_unref(b);
        }
        g_free(f);
    }
    g_free(dir);
    g_task_return_boolean(t, TRUE);
}

static void fetch_capes_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)r;
    app_t *a = data;
    char *dir = g_build_filename(g_get_user_cache_dir(), "tux-mc", "capes", NULL);
    for (int i = 0; i < a->ncapes; i++) {
        char *f = g_strdup_printf("%s/%s.png", dir, a->capes[i].id);
        cairo_surface_t *sf = cairo_image_surface_create_from_png(f);
        if (cairo_surface_status(sf) == CAIRO_STATUS_SUCCESS)
            a->capes[i].tex = sf;
        else
            cairo_surface_destroy(sf);
        g_free(f);
    }
    g_free(dir);
    mc_home_refresh_capes(a);
}

/* ---- start ---- */
static gboolean close_later(gpointer data)
{
    app_t *a = data;
    if (a->win)
        gtk_window_close(a->win);
    return G_SOURCE_REMOVE;
}

static void set_start_label(app_t *a, const char *t) { gtk_label_set_text(GTK_LABEL(a->start_label), t); }

static void launch_failed(app_t *a, const char *msg)
{
    mc_status(a, "%s", msg);
    a->launching = false;
    a->progress_target = 0;
    set_start_label(a, "START");
    gtk_widget_remove_css_class(a->start, "busy");
}

static gboolean reset_start(gpointer data)
{
    app_t *a = data;
    launch_failed(a, "Minecraft is running.");
    return G_SOURCE_REMOVE;
}

static gboolean watch_log(gpointer data)
{
    app_t *a = data;
    struct stat st;
    if (stat(a->log_path, &st) != 0)
        return G_SOURCE_CONTINUE;
    if (st.st_size < a->log_seen)
        a->log_seen = 0;
    char *txt = NULL;
    gsize n = 0;
    if (!g_file_get_contents(a->log_path, &txt, &n, NULL))
        return G_SOURCE_CONTINUE;
    const char *from = (gsize)a->log_seen <= n ? txt + a->log_seen : txt;
    struct { const char *needle, *msg; double p; } steps[] = {
        {"Loading ", "Loading mods", 0.3},
        {"Using graphics backend", "Starting the game", 0.55},
        {"Reloading ResourceManager", "Loading textures", 0.75},
        {"Created: ", "Almost there", 0.88},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(steps); i++)
        if (strstr(from, steps[i].needle) && a->progress_target < steps[i].p) {
            a->progress_target = steps[i].p;
            set_start_label(a, steps[i].msg);
        }
    bool done = strstr(from, "Sound engine started") != NULL;
    bool dead = strstr(from, "Stopping!") || strstr(from, "#@!@# Game crashed");
    g_free(txt);
    if (dead) {
        a->watch_src = 0;
        launch_failed(a, "The game stopped while starting. Prism has the log (Settings → Open Prism).");
        return G_SOURCE_REMOVE;
    }
    if (done) {
        a->progress_target = 1;
        set_start_label(a, "Have fun");
        a->watch_src = 0;
        /* the launcher stays open: the game comes to the front (or we step aside) */
        if (!mc_sys_focus_game(a->instdir) && !mc_cfg_bool("close_on_start", false))
            gtk_window_minimize(a->win);
        if (mc_cfg_bool("close_on_start", false))
            g_timeout_add(1200, close_later, a);
        else
            g_timeout_add(2500, reset_start, a);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void on_start(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    if (a->launching)
        return;
    if (mc_game_running(a)) {
        mc_status(a, "Minecraft is already running.");
        return;
    }
    /* settings go into instance.cfg; Prism must not hold it open */
    if (mc_prism_running())
        mc_sys_stop_prism();
    mc_settings_apply(a);

    struct stat st;
    a->log_seen = stat(a->log_path, &st) == 0 ? st.st_size : 0;
    const char *args[] = {"--launch", a->instance, NULL};
    GError *err = NULL;
    if (!mc_sys_run_prism(args, &err)) {
        launch_failed(a, err->message);
        g_error_free(err);
        return;
    }
    a->launching = true;
    a->progress = 0;
    a->progress_target = 0.1;
    gtk_widget_add_css_class(a->start, "busy");
    set_start_label(a, "Logging in");
    mc_status(a, "Starting Minecraft %s…", a->instance);
    a->view.wave = 1;
    a->watch_src = g_timeout_add(300, watch_log, a);
}

/* the start button's own progress + shimmer */
static void draw_progress(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    app_t *a = data;
    if (a->progress < 0.005 && !a->launching)
        return;
    GdkRGBA br;
    mc_pal(a, PAL_BRIGHT, &br);
    cairo_set_source_rgba(cr, br.red, br.green, br.blue, 0.28);
    cairo_rectangle(cr, 0, 0, w * a->progress, h);
    cairo_fill(cr);
    double x = fmod(a->view.t * 0.5, 1.6) - 0.3;
    cairo_pattern_t *g = cairo_pattern_create_linear((x - 0.25) * w, 0, (x + 0.25) * w, 0);
    cairo_pattern_add_color_stop_rgba(g, 0, br.red, br.green, br.blue, 0);
    cairo_pattern_add_color_stop_rgba(g, 0.5, br.red, br.green, br.blue, 0.25);
    cairo_pattern_add_color_stop_rgba(g, 1, br.red, br.green, br.blue, 0);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
}

static int count_mods(app_t *a)
{
    char *dir = g_build_filename(a->mcdir, "mods", NULL);
    GDir *d = g_dir_open(dir, 0, NULL);
    int n = 0;
    for (const char *e; d && (e = g_dir_read_name(d));)
        n += g_str_has_suffix(e, ".jar");
    if (d)
        g_dir_close(d);
    g_free(dir);
    return n;
}

static char *instance_value(app_t *a, const char *key)
{
    char *p = g_build_filename(a->instdir, "instance.cfg", NULL), *txt = NULL, *res = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **l = g_strsplit(txt, "\n", -1);
        char *pre = g_strconcat(key, "=", NULL);
        for (char **q = l; *q && !res; q++)
            if (g_str_has_prefix(*q, pre))
                res = g_strdup(*q + strlen(pre));
        g_free(pre);
        g_strfreev(l);
        g_free(txt);
    }
    g_free(p);
    return res;
}

GtkWidget *mc_page_home(app_t *a)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(page, "mcl-page");

    a->stage = mc_stage(a, &a->view, draw_stage);
    gtk_widget_set_size_request(a->stage, 420, 520);
    gtk_box_append(GTK_BOX(page), a->stage);

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_size_request(right, 520, -1);
    gtk_widget_set_valign(right, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(right), mc_label("MINECRAFT", "mcl-hero"));
    char *played = instance_value(a, "totalTimePlayed");
    long secs = played ? atol(played) : 0;
    g_free(played);
    char *name = instance_value(a, "name");
    char *sub = g_strdup_printf("%s  ·  %s %s  ·  %d mods  ·  %ldh %02ldm played", name && *name ? name : a->instance,
                                a->mc_version ? a->mc_version : "", mc_loader_name(a->loader), count_mods(a),
                                secs / 3600, secs / 60 % 60);
    g_free(name);
    a->sub = mc_label(sub, "mcl-sub");
    g_free(sub);
    gtk_box_append(GTK_BOX(right), a->sub);

    GtkWidget *cc = mc_card("Cape", "Mojang capes: everyone sees them.");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (int i = 0; i < a->ncapes; i++) {
        cape_t *k = &a->capes[i];
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "mcl-cape");
        gtk_widget_set_tooltip_text(b, k->alias);
        k->area = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(k->area), 36);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(k->area), 56);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(k->area), draw_cape_tile, k, NULL);
        gtk_button_set_child(GTK_BUTTON(b), k->area);
        g_signal_connect(b, "clicked", G_CALLBACK(on_cape), k);
        k->tile = b;
        gtk_box_append(GTK_BOX(row), b);
    }
    a->none_tile = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(a->none_tile, "mcl-cape");
    gtk_widget_add_css_class(a->none_tile, "mcl-none");
    gtk_widget_set_tooltip_text(a->none_tile, "No cape");
    g_signal_connect(a->none_tile, "clicked", G_CALLBACK(on_no_cape), a);
    gtk_box_append(GTK_BOX(row), a->none_tile);
    gtk_box_append(GTK_BOX(cc), row);
    GtkWidget *cl = mc_label("tux client cape: other tux-mc players see it too.", "dim");
    gtk_widget_set_margin_top(cl, 6);
    gtk_box_append(GTK_BOX(cc), cl);
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 5);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 5);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 6);
    a->cape_flow = flow;
    fill_cape_chips(a);
    gtk_box_append(GTK_BOX(cc), flow);
    gtk_box_append(GTK_BOX(right), cc);

    a->start = gtk_button_new();
    gtk_widget_add_css_class(a->start, "mcl-start");
    GtkWidget *ov = gtk_overlay_new();
    GtkWidget *bar = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(bar), draw_progress, a, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(ov), bar);
    a->start_label = gtk_label_new("START");
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), a->start_label);
    gtk_widget_set_can_target(bar, FALSE);
    gtk_button_set_child(GTK_BUTTON(a->start), ov);
    g_signal_connect(a->start, "clicked", G_CALLBACK(on_start), a);
    gtk_widget_set_margin_top(a->start, 8);
    gtk_box_append(GTK_BOX(right), a->start);
    gtk_box_append(GTK_BOX(page), right);

    GTask *t = g_task_new(NULL, NULL, fetch_capes_done, a);
    g_task_set_task_data(t, a, NULL);
    g_task_run_in_thread(t, fetch_capes_thread);
    g_object_unref(t);
    gtk_widget_add_tick_callback(a->stage, home_tick, a, NULL);
    return page;
}

/* --------------------------------------------------------------- window -- */
static GtkWidget *palette(app_t *a)
{
    static const char *const cls[PAL_N] = {"pal-fg", "pal-dim",     "pal-accent",  "pal-accent2",
                                           "pal-bg", "pal-warn",    "pal-surface", "pal-bright"};
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "palette");
    for (int i = 0; i < PAL_N; i++) {
        a->pal[i] = gtk_drawing_area_new();
        gtk_widget_add_css_class(a->pal[i], cls[i]);
        gtk_box_append(GTK_BOX(box), a->pal[i]);
    }
    return box;
}

static const struct { const char *id, *title, *icon; } PAGES[] = {
    {"home", "Home", "go-home-symbolic"},
    {"instances", "Instances", "view-list-symbolic"},
    {"import", "Import", "folder-download-symbolic"},
    {"skins", "Skins", "avatar-default-symbolic"},
    {"mods", "Mods", "application-x-addon-symbolic"},
    {"configs", "Configs", "document-edit-symbolic"},
    {"settings", "Settings", "preferences-system-symbolic"},
};

static void show_page(app_t *a, int i)
{
    gtk_stack_set_visible_child_name(GTK_STACK(a->stack), PAGES[i].id);
    for (int k = 0; k < (int)G_N_ELEMENTS(PAGES); k++) {
        if (k == i)
            gtk_widget_add_css_class(a->nav[k], "active");
        else
            gtk_widget_remove_css_class(a->nav[k], "active");
    }
}

static void on_nav(GtkButton *b, gpointer data)
{
    show_page(data, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "page")));
}

static gboolean on_key(GtkEventControllerKey *k, guint key, guint code, GdkModifierType st, gpointer data)
{
    (void)k;
    (void)code;
    app_t *a = data;
    if (key == GDK_KEY_Escape) {
        gtk_window_close(a->win);
        return TRUE;
    }
    if (key == GDK_KEY_F11) {
        if (gtk_window_is_fullscreen(a->win))
            gtk_window_unfullscreen(a->win);
        else
            gtk_window_fullscreen(a->win);
        return TRUE;
    }
    /* Ctrl, not Alt: Alt+number is niri's workspace switch here */
    if ((st & GDK_CONTROL_MASK) && key >= GDK_KEY_1 && key < GDK_KEY_1 + (int)G_N_ELEMENTS(PAGES)) {
        show_page(a, key - GDK_KEY_1);
        return TRUE;
    }
    if (key == GDK_KEY_Return && !strcmp(gtk_stack_get_visible_child_name(GTK_STACK(a->stack)), "home")) {
        on_start(NULL, a);
        return TRUE;
    }
    return FALSE;
}

static void on_close(GtkButton *b, gpointer d) { (void)b; gtk_window_close(((app_t *)d)->win); }

static void on_destroy(GtkWidget *w, gpointer data)
{
    app_t *a = data;
    if (GTK_WINDOW(w) != a->win)         /* an old window, replaced by mc_reload_window */
        return;
    if (a->watch_src)
        g_source_remove(a->watch_src);
    a->watch_src = 0;
    a->win = NULL;
}

#ifdef _WIN32
/* a press on the title bar (not on its buttons): let Windows move the window,
 * so dragging to the top maximizes and to the sides tiles, like any app. Only
 * when the window has the native resize frame snapping needs; otherwise GTK's
 * own drag stays. Double clicks are left to GTK (maximize). */
static void on_title_press(GtkGestureClick *g, int n, double x, double y, gpointer data)
{
    app_t *a = data;
    if (n != 1 || gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g)) != GDK_BUTTON_PRIMARY)
        return;
    GtkWidget *w = gtk_widget_pick(GTK_WIDGET(a->win), x, y, GTK_PICK_DEFAULT);
    bool title = false;
    for (; w && w != GTK_WIDGET(a->win); w = gtk_widget_get_parent(w)) {
        if (GTK_IS_BUTTON(w))
            return;
        if (GTK_IS_HEADER_BAR(w) || GTK_IS_WINDOW_HANDLE(w))
            title = true;
    }
    if (!title)
        return;
    GdkSurface *sf = gtk_native_get_surface(GTK_NATIVE(a->win));
    HWND h = sf ? (HWND)gdk_win32_surface_get_handle(sf) : NULL;
    if (!h || !(GetWindowLongPtrW(h, GWL_STYLE) & WS_THICKFRAME))
        return;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    POINT pt;
    GetCursorPos(&pt);
    ReleaseCapture();
    PostMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
}
#endif

static void build_window(app_t *a)
{
    a->win = GTK_WINDOW(gtk_application_window_new(a->gapp));
    gtk_window_set_title(a->win, "Minecraft");
    gtk_window_set_default_size(a->win, 1280, 800);
    gtk_widget_add_css_class(GTK_WIDGET(a->win), "mcl-window");
    g_signal_connect(a->win, "destroy", G_CALLBACK(on_destroy), a);
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), a);
    gtk_widget_add_controller(GTK_WIDGET(a->win), keys);
#ifdef _WIN32
    GtkGesture *tp = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tp), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(tp), GTK_PHASE_CAPTURE);
    g_signal_connect(tp, "pressed", G_CALLBACK(on_title_press), a);
    gtk_widget_add_controller(GTK_WIDGET(a->win), GTK_EVENT_CONTROLLER(tp));
#endif

    GtkWidget *over = gtk_overlay_new();
    gtk_window_set_child(a->win, over);
    a->bg = g_object_new(TUX_TYPE_BG, NULL);
    TUX_BG(a->bg)->a = a;
    bg_load(TUX_BG(a->bg));
    gtk_overlay_set_child(GTK_OVERLAY(over), a->bg);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(root, "mcl-root");
    gtk_overlay_add_overlay(GTK_OVERLAY(over), root);

    /* sidebar */
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(side, "mcl-side");
    GtkWidget *logo = mc_label("tux", "mcl-logo");
    gtk_box_append(GTK_BOX(side), logo);
    GtkWidget *logo2 = mc_label("launcher", "mcl-logo-sub");
    gtk_box_append(GTK_BOX(side), logo2);
    for (int i = 0; i < (int)G_N_ELEMENTS(PAGES); i++) {
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "mcl-nav");
        GtkWidget *bx = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_append(GTK_BOX(bx), gtk_image_new_from_icon_name(PAGES[i].icon));
        gtk_box_append(GTK_BOX(bx), mc_label(PAGES[i].title, NULL));
        gtk_button_set_child(GTK_BUTTON(b), bx);
        g_object_set_data(G_OBJECT(b), "page", GINT_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(on_nav), a);
        a->nav[i] = b;
        gtk_box_append(GTK_BOX(side), b);
    }
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(side), spacer);
    gtk_box_append(GTK_BOX(side), mc_account_button(a));
    GtkWidget *quit = gtk_button_new_with_label("Close  (Esc)");
    gtk_widget_add_css_class(quit, "mcl-quit");
    g_signal_connect(quit, "clicked", G_CALLBACK(on_close), a);
    gtk_box_append(GTK_BOX(side), quit);
    gtk_box_append(GTK_BOX(root), side);

    /* pages */
    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main, TRUE);
    a->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
    gtk_stack_set_transition_duration(GTK_STACK(a->stack), 380);
    gtk_widget_set_vexpand(a->stack, TRUE);
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_home(a), "home");
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_instances(a), "instances");
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_import(a), "import");
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_skins(a), "skins");
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_mods(a), "mods");
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_configs(a), "configs");
    gtk_stack_add_named(GTK_STACK(a->stack), mc_page_settings(a), "settings");
    gtk_box_append(GTK_BOX(main), a->stack);
    a->status = mc_label("", "mcl-status");
    gtk_label_set_xalign(GTK_LABEL(a->status), 0.5);
    gtk_box_append(GTK_BOX(main), a->status);
    gtk_box_append(GTK_BOX(main), palette(a));
    gtk_box_append(GTK_BOX(root), main);

    show_page(a, 0);
    const char *pg = a->open_page ? a->open_page : g_getenv("TUX_MC_PAGE");   /* reload, screenshots */
    for (int i = 0; pg && i < (int)G_N_ELEMENTS(PAGES); i++)
        if (!strcmp(pg, PAGES[i].id)) {
            gtk_stack_set_transition_type(GTK_STACK(a->stack), GTK_STACK_TRANSITION_TYPE_NONE);
            show_page(a, i);
            gtk_stack_set_transition_type(GTK_STACK(a->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);
        }

    repaint_tux_capes(a);
    mc_home_refresh_capes(a);
    mc_apply_looks(a);
    if (g_getenv("TUX_MC_CAPE_EDIT"))                       /* screenshots */
        mc_cape_editor(a, g_getenv("TUX_MC_CAPE_EDIT"), g_getenv("TUX_MC_CAPE_ID"), picture_saved);
    if (!a->name)
        mc_status(a, "No account in Prism yet: log in there once (Settings → Open Prism).");
    else
        mc_status(a, "Welcome back, %s. Enter starts the game.", a->name);
}

void mc_apply_looks(app_t *a)
{
    a->bg_blur = mc_cfg_int("bg_blur", 6);
    a->bg_motion = mc_cfg_bool("bg_motion", true);
    a->bg_dust = mc_cfg_bool("bg_dust", true);
    a->view.idle_turn = a->skin_view.idle_turn = mc_cfg_bool("model_turn", true);
    a->view.still = a->skin_view.still = !mc_cfg_bool("model_idle", true);
    gtk_stack_set_transition_type(GTK_STACK(a->stack), mc_cfg_bool("page_anim", true)
                                                           ? GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN
                                                           : GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_visible(a->sub, mc_cfg_bool("show_stats", true));
    if (a->bg)
        gtk_widget_queue_draw(a->bg);
}

/* ------------------------------------------------------------------ app -- */
/* ~/.config/tux-mc/style.css (the theme switcher writes it); older setups kept
 * the rules in the Control Center's style.css */
static char *style_path(void)
{
    char *p = g_build_filename(g_get_user_config_dir(), "tux-mc", "style.css", NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS))
        return p;
    char *old = g_build_filename(g_get_user_config_dir(), "tux-control-center", "style.css", NULL);
    if (g_file_test(old, G_FILE_TEST_EXISTS)) {
        g_free(p);
        return old;
    }
    g_free(old);
    return p;
}

static void load_css(app_t *a)
{
    char *path = style_path();
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        gtk_css_provider_load_from_path(a->css, path);
    g_free(path);
}

static gboolean reload_css(gpointer data)
{
    app_t *a = data;
    load_css(a);
    if (a->bg)
        bg_load(TUX_BG(a->bg));                /* a theme switch brings a new wallpaper */
    return G_SOURCE_REMOVE;
}

static void on_css_changed(GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e, gpointer data)
{
    (void)m;
    (void)f;
    (void)o;
    if (e == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || e == G_FILE_MONITOR_EVENT_RENAMED ||
        e == G_FILE_MONITOR_EVENT_MOVED_IN || e == G_FILE_MONITOR_EVENT_CREATED)
        g_timeout_add(150, reload_css, data);
}

static void on_startup(GApplication *app, gpointer data)
{
    (void)app;
    app_t *a = data;
    /* the black theme, built in: what the launcher looks like without a style.css */
    GtkCssProvider *base = gtk_css_provider_new();
    gtk_css_provider_load_from_string(base, TUX_STYLE_CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(base),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    a->css = gtk_css_provider_new();
    load_css(a);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(a->css),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    char *path = style_path();
    GFile *f = g_file_new_for_path(path);
    a->css_monitor = g_file_monitor_file(f, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    if (a->css_monitor)
        g_signal_connect(a->css_monitor, "changed", G_CALLBACK(on_css_changed), a);
    g_object_unref(f);
    g_free(path);

    /* TUX_MC_INSTANCE, else the one picked on the Instances page, else 26.3 */
    const char *inst = g_getenv("TUX_MC_INSTANCE");
    char *picked = mc_cfg_str("instance", "26.3");
    a->instance = g_strdup(inst ? inst : picked);
    g_free(picked);
    mc_paths(a);
    if (!inst && !g_file_test(a->instdir, G_FILE_TEST_IS_DIR) && strcmp(a->instance, "26.3")) {
        g_free(a->instance);                     /* the picked one is gone */
        a->instance = g_strdup("26.3");
        mc_paths(a);
    }
    mc_load_account(a);
    load_tux_cape(a);
    mc3d_init(&a->view);
    mc3d_init(&a->skin_view);
    a->view.skin = a->skin;
    a->view.slim = a->slim;
}

/* Prism's folders; again after setup, when Prism may have just been installed */
void mc_paths(app_t *a)
{
    g_free(a->prism);
    g_free(a->instdir);
    g_free(a->mcdir);
    g_free(a->log_path);
    a->prism = mc_sys_prism_dir();
    a->instdir = g_build_filename(a->prism, "instances", a->instance, NULL);
    a->mcdir = g_build_filename(a->instdir, "minecraft", NULL);
    a->log_path = g_build_filename(a->mcdir, "logs", "latest.log", NULL);
    g_free(a->mc_version);
    g_free(a->loader);
    mc_inst_read_pack(a->instdir, &a->mc_version, &a->loader, NULL);
}

/* another instance was picked: everything (mods, configs, stats) is per
 * instance, so build the window again and drop the old one */
void mc_reload_window(app_t *a, const char *page)
{
    GtkWindow *old = a->win;
    mc_paths(a);
    if (a->mods) {
        g_ptr_array_unref(a->mods);
        a->mods = NULL;
    }
    a->open_page = page;
    build_window(a);
    a->open_page = NULL;
    gtk_window_present(a->win);
    if (old)
        gtk_window_destroy(old);
}

/* the setup screen is done: load the account it signed in and show the launcher */
void mc_open_main(app_t *a)
{
    mc_paths(a);
    mc_load_account(a);
    load_tux_cape(a);
    a->view.skin = a->skin;
    a->view.slim = a->slim;
    build_window(a);
    gtk_window_present(a->win);
}

static void on_activate(GApplication *app, gpointer data)
{
    (void)app;
    app_t *a = data;
    if (a->win) {
        gtk_window_present(a->win);
        return;
    }
    if (mc_setup_needed(a)) {            /* no Prism, no account or no instance yet */
        mc_setup_show(a);
        return;
    }
    build_window(a);
    gtk_window_present(a->win);
    mc_update_startup(a);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--version")) {   /* the updater runs a new build with this */
        printf("tux-mc %s\n", TUX_MC_VERSION);
        return 0;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    static app_t a;
    const char *id = g_getenv("TUX_MC_APP_ID");          /* a second copy for testing */
    a.gapp = gtk_application_new(id && *id ? id : APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a.gapp, "startup", G_CALLBACK(on_startup), &a);
    g_signal_connect(a.gapp, "activate", G_CALLBACK(on_activate), &a);
    int rc = g_application_run(G_APPLICATION(a.gapp), argc, argv);
    g_object_unref(a.gapp);
    curl_global_cleanup();
    if (a.restart)                       /* "Restart" after an update: the new build */
        mc_update_restart(argv[0]);
    return rc;
}
