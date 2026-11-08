/* mc-packs.c — the Packs page: make a resource pack without an image editor.
 *
 * Textures come from the game's own client jar. Whatever you change is
 * written straight into a folder pack, <instance>/resourcepacks/<name>/, so
 * the game can use it as it is (F3+T reloads it in game). Export makes a zip
 * to share.
 *
 * Pick a texture on the left, draw on it on the right: draw, erase, fill and
 * pick on a big pixel grid, any colour from the wheel or the texture's own
 * colours, and (folded away) hue / saturation / brightness / contrast for the
 * whole texture.
 */
#include "mc.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define SHOW_MAX 240

typedef struct {
    int w, h;
    guint8 *px;                          /* RGBA, not premultiplied */
} img_t;

enum { C_ALL, C_BLOCK, C_ITEM, C_ENTITY, C_GUI, C_PARTICLE, C_OTHER, C_CHANGED, C_N };
static const char *const CATS[] = {"Everything", "Blocks", "Items", "Mobs", "Menus", "Particles", "Other",
                                   "Changed by you", NULL};
static const int CHIP_ORDER[] = {C_BLOCK, C_ITEM, C_ENTITY, C_GUI, C_PARTICLE, C_OTHER, C_ALL, C_CHANGED};

enum { T_DRAW, T_ERASE, T_FILL, T_PICK, T_N };
static const char *const TOOL_NAMES[T_N] = {"Draw", "Erase", "Fill", "Pick"};

static struct {
    app_t *a;
    char *jar;                           /* the game's client jar */
    mc_zip_t *zip;
    GPtrArray *names;                    /* texture names in the jar, sorted */
    int format;                          /* resource pack format of this version */
    char *pack;                          /* pack folder name */

    /* the texture being edited */
    char *cur;
    img_t img, base;                     /* now / when adjustments started */
    int frame, frames;
    GPtrArray *undo;                     /* img_t* */
    int tool;
    guint8 colour[4];
    int last_x, last_y;
    bool stroke;

    GtkWidget *packs, *onoff, *grid, *search, *count, *title, *canvas, *colour_btn, *used, *tool_btn[T_N],
        *frame_lbl, *frame_box, *adj[4], *msg, *editor, *empty, *chips[C_N], *state;
    int cat;
    bool filling, loading;
} P;

/* ---------------------------------------------------------------- images -- */

static void img_free(img_t *i)
{
    g_free(i->px);
    i->px = NULL;
    i->w = i->h = 0;
}

static void img_copy(img_t *to, const img_t *from)
{
    g_free(to->px);
    to->w = from->w;
    to->h = from->h;
    to->px = g_memdup2(from->px, (gsize)from->w * from->h * 4);
}

static bool img_from_bytes(GBytes *b, img_t *out)
{
    if (!b)
        return false;
    GdkTexture *t = gdk_texture_new_from_bytes(b, NULL);
    if (!t)
        return false;
    out->w = gdk_texture_get_width(t);
    out->h = gdk_texture_get_height(t);
    out->px = g_malloc((gsize)out->w * out->h * 4);
    GdkTextureDownloader *d = gdk_texture_downloader_new(t);
    gdk_texture_downloader_set_format(d, GDK_MEMORY_R8G8B8A8);
    gdk_texture_downloader_download_into(d, out->px, out->w * 4);
    gdk_texture_downloader_free(d);
    g_object_unref(t);
    return true;
}

static GBytes *img_png(const img_t *i)
{
    GBytes *raw = g_bytes_new(i->px, (gsize)i->w * i->h * 4);
    GdkTexture *t = gdk_memory_texture_new(i->w, i->h, GDK_MEMORY_R8G8B8A8, raw, i->w * 4);
    GBytes *png = gdk_texture_save_to_png_bytes(t);
    g_object_unref(t);
    g_bytes_unref(raw);
    return png;
}

/* rows y0..y0+h as a cairo surface */
static cairo_surface_t *img_surface(const img_t *i, int y0, int h)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, i->w, h);
    guint32 *d = (guint32 *)cairo_image_surface_get_data(s);
    int st = cairo_image_surface_get_stride(s) / 4;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < i->w; x++) {
            const guint8 *p = i->px + ((gsize)(y0 + y) * i->w + x) * 4;
            guint32 a = p[3];
            d[y * st + x] = a << 24 | (p[0] * a / 255) << 16 | (p[1] * a / 255) << 8 | (p[2] * a / 255);
        }
    cairo_surface_mark_dirty(s);
    return s;
}

/* ------------------------------------------------------------ the pack -- */

static char *packs_root(void) { return g_build_filename(P.a->mcdir, "resourcepacks", NULL); }

static char *pack_dir(void)
{
    char *r = packs_root(), *d = g_build_filename(r, P.pack, NULL);
    g_free(r);
    return d;
}

static char *pack_file(const char *name)
{
    char *d = pack_dir(), *f = g_build_filename(d, name, NULL);
    g_free(d);
    return f;
}

static bool changed_in_pack(const char *name)
{
    char *f = pack_file(name);
    bool r = g_file_test(f, G_FILE_TEST_EXISTS);
    g_free(f);
    return r;
}

/* pack.mcmeta and an icon, the first time something is written */
static void ensure_pack(const img_t *icon_from)
{
    char *d = pack_dir(), *meta = g_build_filename(d, "pack.mcmeta", NULL);
    if (!g_file_test(meta, G_FILE_TEST_EXISTS)) {
        g_mkdir_with_parents(d, 0755);
        char *desc = g_strescape(P.pack, NULL);
        char *j = g_strdup_printf("{\n  \"pack\": {\n    \"description\": \"%s, made with tux-mc\",\n"
                                  "    \"pack_format\": %d,\n    \"min_format\": %d,\n    \"max_format\": %d\n  }\n}\n",
                                  desc, P.format, P.format, P.format);
        g_file_set_contents(meta, j, -1, NULL);
        g_free(j);
        g_free(desc);
    }
    char *icon = g_build_filename(d, "pack.png", NULL);
    bool need_icon = !g_file_test(icon, G_FILE_TEST_EXISTS);
    g_free(icon);
    {
        if (need_icon && icon_from && icon_from->w) {   /* the first changed texture, blown up */
            img_t ic = {64, 64, g_malloc(64 * 64 * 4)};
            int fh = MIN(icon_from->w, icon_from->h);
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++)
                    memcpy(ic.px + (y * 64 + x) * 4, icon_from->px + ((y * fh / 64) * icon_from->w + x * icon_from->w / 64) * 4, 4);
            GBytes *png = img_png(&ic);
            char *ip = g_build_filename(d, "pack.png", NULL);
            g_file_set_contents(ip, g_bytes_get_data(png, NULL), g_bytes_get_size(png), NULL);
            g_free(ip);
            g_bytes_unref(png);
            img_free(&ic);
        }
    }
    g_free(meta);
    g_free(d);
}

static bool write_texture(const char *name, const img_t *i)
{
    ensure_pack(i);
    char *f = pack_file(name), *dir = g_path_get_dirname(f);
    g_mkdir_with_parents(dir, 0755);
    GBytes *png = img_png(i);
    bool ok = g_file_set_contents(f, g_bytes_get_data(png, NULL), g_bytes_get_size(png), NULL);
    g_bytes_unref(png);
    g_free(dir);
    g_free(f);
    return ok;
}

/* the pack's version if it has one, else the game's */
static bool load_texture(mc_zip_t *z, const char *name, img_t *out)
{
    char *f = pack_file(name), *d = NULL;
    gsize l = 0;
    bool ok = false;
    if (g_file_get_contents(f, &d, &l, NULL)) {
        GBytes *b = g_bytes_new_take(d, l);
        ok = img_from_bytes(b, out);
        g_bytes_unref(b);
    }
    g_free(f);
    if (!ok) {
        GBytes *b = mc_zip_get(z, name);
        ok = img_from_bytes(b, out);
        if (b)
            g_bytes_unref(b);
    }
    return ok;
}

/* ------------------------------------------------------- colour helpers -- */

static void rgb_hsv(double r, double g, double b, double *h, double *s, double *v)
{
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0)
        *h = 0;
    else if (mx == r)
        *h = fmod((g - b) / d + 6, 6);
    else if (mx == g)
        *h = (b - r) / d + 2;
    else
        *h = (r - g) / d + 4;
    *h *= 60;
}

static void hsv_rgb(double h, double s, double v, double *r, double *g, double *b)
{
    h = fmod(fmod(h, 360) + 360, 360) / 60;
    double c = v * s, x = c * (1 - fabs(fmod(h, 2) - 1)), m = v - c;
    double rr = 0, gg = 0, bb = 0;
    if (h < 1) { rr = c; gg = x; }
    else if (h < 2) { rr = x; gg = c; }
    else if (h < 3) { gg = c; bb = x; }
    else if (h < 4) { gg = x; bb = c; }
    else if (h < 5) { rr = x; bb = c; }
    else { rr = c; bb = x; }
    *r = rr + m;
    *g = gg + m;
    *b = bb + m;
}

static guint8 clamp8(double v) { return (guint8)CLAMP(lround(v * 255), 0, 255); }

/* hue (degrees), saturation and value factors, brightness add, contrast factor */
static void adjust(img_t *i, double dh, double ks, double add, double kc)
{
    for (int k = 0; k < i->w * i->h; k++) {
        guint8 *p = i->px + k * 4;
        if (!p[3])
            continue;
        double h, s, v, r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
        rgb_hsv(r, g, b, &h, &s, &v);
        hsv_rgb(h + dh, fmin(1, s * ks), v, &r, &g, &b);
        r = (r - 0.5) * kc + 0.5 + add;
        g = (g - 0.5) * kc + 0.5 + add;
        b = (b - 0.5) * kc + 0.5 + add;
        p[0] = clamp8(r);
        p[1] = clamp8(g);
        p[2] = clamp8(b);
    }
}

/* ---------------------------------------------------------------- list -- */

static int category(const char *n)
{
    static const struct { const char *dir; int c; } D[] = {
        {"assets/minecraft/textures/block/", C_BLOCK},       {"assets/minecraft/textures/item/", C_ITEM},
        {"assets/minecraft/textures/entity/", C_ENTITY},     {"assets/minecraft/textures/gui/", C_GUI},
        {"assets/minecraft/textures/particle/", C_PARTICLE},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(D); i++)
        if (g_str_has_prefix(n, D[i].dir))
            return D[i].c;
    return C_OTHER;
}

/* "block/oak_planks.png" from the jar name */
static const char *short_name(const char *n) { return n + strlen("assets/minecraft/textures/"); }

/* "Oak planks" */
static char *nice_name(const char *n)
{
    const char *b = strrchr(n, '/');
    char *s = g_strdup(b ? b + 1 : n), *dot = strrchr(s, '.');
    if (dot)
        *dot = 0;
    for (char *c = s; *c; c++)
        if (*c == '_')
            *c = ' ';
    s[0] = g_ascii_toupper(s[0]);
    return s;
}

static void select_texture(const char *name);

static void draw_thumb(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    cairo_surface_t *s = data;
    int sw = cairo_image_surface_get_width(s), sh = cairo_image_surface_get_height(s);
    double k = fmin((double)w / sw, (double)h / sh);
    cairo_translate(cr, (w - sw * k) / 2, (h - sh * k) / 2);
    cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
}

static void on_tile(GtkButton *b, gpointer d)
{
    (void)d;
    for (GtkWidget *c = gtk_widget_get_first_child(P.grid); c; c = gtk_widget_get_next_sibling(c))
        gtk_widget_remove_css_class(gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(c)), "active");
    gtk_widget_add_css_class(GTK_WIDGET(b), "active");
    select_texture(g_object_get_data(G_OBJECT(b), "name"));
}

static void fill_grid(void)
{
    if (!P.grid || !P.names)
        return;
    gtk_flow_box_remove_all(GTK_FLOW_BOX(P.grid));
    const char *q = gtk_editable_get_text(GTK_EDITABLE(P.search));
    int cat = P.cat;
    char *ql = g_utf8_strdown(q, -1);
    int shown = 0, total = 0;
    for (guint i = 0; i < P.names->len; i++) {
        const char *n = P.names->pdata[i];
        bool ch = cat == C_CHANGED || shown < SHOW_MAX ? changed_in_pack(n) : false;
        if (cat == C_CHANGED ? !ch : cat != C_ALL && category(n) != cat)
            continue;
        char *nn = *ql ? g_utf8_strdown(short_name(n), -1) : NULL;
        for (char *c = nn; c && *c; c++)
            if (*c == '_')
                *c = ' ';
        bool miss = nn && !strstr(nn, ql);
        g_free(nn);
        if (miss)
            continue;
        total++;
        if (shown >= SHOW_MAX)
            continue;
        img_t im = {0};
        if (!load_texture(P.zip, n, &im))
            continue;
        cairo_surface_t *s = img_surface(&im, 0, MIN(im.w, im.h));
        img_free(&im);
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "mcl-tex");
        if (ch)
            gtk_widget_add_css_class(b, "changed");
        if (P.cur && !strcmp(P.cur, n))
            gtk_widget_add_css_class(b, "active");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        GtkWidget *da = gtk_drawing_area_new();
        gtk_widget_set_size_request(da, 56, 56);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_thumb, s, (GDestroyNotify)cairo_surface_destroy);
        gtk_box_append(GTK_BOX(box), da);
        char *nice = nice_name(n);
        GtkWidget *l = mc_label(nice, "dim");
        g_free(nice);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 9);
        gtk_label_set_width_chars(GTK_LABEL(l), 9);
        gtk_widget_add_css_class(l, "mcl-small");
        gtk_box_append(GTK_BOX(box), l);
        gtk_button_set_child(GTK_BUTTON(b), box);
        gtk_widget_set_tooltip_text(b, short_name(n));
        g_object_set_data_full(G_OBJECT(b), "name", g_strdup(n), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(on_tile), NULL);
        gtk_flow_box_append(GTK_FLOW_BOX(P.grid), b);
        shown++;
    }
    char *t = total > shown ? g_strdup_printf("Showing %d of %d. Type above to find the rest.", shown, total)
              : !total && cat == C_CHANGED ? g_strdup("Nothing changed yet. Pick any texture and draw on it.")
              : !total ? g_strdup("Nothing with that name here. Try Everything.")
                       : g_strdup_printf("%d %s", total, total == 1 ? "texture" : "textures");
    gtk_label_set_text(GTK_LABEL(P.count), t);
    g_free(t);
    g_free(ql);
}

static void on_filter(GtkWidget *w, gpointer d)
{
    (void)w;
    (void)d;
    fill_grid();
}

static void on_chip(GtkToggleButton *b, gpointer d)
{
    if (!gtk_toggle_button_get_active(b))
        return;
    P.cat = GPOINTER_TO_INT(d);
    fill_grid();
}

/* -------------------------------------------------------------- editor -- */

static void msg(const char *m) { gtk_label_set_text(GTK_LABEL(P.msg), m); }

static void fill_used(void);

static void saved_note(void)
{
    msg(mc_game_running(P.a) ? "Saved into the pack. In game, F3+T shows it." : "Saved into the pack.");
}

static void save_cur(void)
{
    if (!P.cur)
        return;
    if (write_texture(P.cur, &P.img)) {
        saved_note();
        gtk_label_set_text(GTK_LABEL(P.state), "changed");
    }
    else
        msg("Couldn't save it.");
}

static void push_undo(void)
{
    img_t *u = g_new0(img_t, 1);
    img_copy(u, &P.img);
    g_ptr_array_add(P.undo, u);
    if (P.undo->len > 40)
        g_ptr_array_remove_index(P.undo, 0);
}

static void free_img(gpointer p)
{
    img_free(p);
    g_free(p);
}

static void reset_sliders(void)
{
    P.loading = true;
    gtk_range_set_value(GTK_RANGE(P.adj[0]), 0);
    gtk_range_set_value(GTK_RANGE(P.adj[1]), 100);
    gtk_range_set_value(GTK_RANGE(P.adj[2]), 0);
    gtk_range_set_value(GTK_RANGE(P.adj[3]), 100);
    P.loading = false;
}

static void frame_label(void)
{
    char *t = g_strdup_printf("Frame %d of %d", P.frame + 1, P.frames);
    gtk_label_set_text(GTK_LABEL(P.frame_lbl), t);
    g_free(t);
    gtk_widget_set_visible(P.frame_box, P.frames > 1);
}

static void select_texture(const char *name)
{
    img_free(&P.img);
    img_free(&P.base);
    g_free(P.cur);
    P.cur = NULL;
    g_ptr_array_set_size(P.undo, 0);
    if (!name || !load_texture(P.zip, name, &P.img)) {
        gtk_widget_set_visible(P.editor, FALSE);
        gtk_widget_set_visible(P.empty, TRUE);
        return;
    }
    P.cur = g_strdup(name);
    img_copy(&P.base, &P.img);
    P.frame = 0;
    P.frames = P.img.h > P.img.w && P.img.h % P.img.w == 0 ? P.img.h / P.img.w : 1;
    frame_label();
    reset_sliders();
    char *nice = nice_name(name);
    gtk_label_set_text(GTK_LABEL(P.title), nice);
    gtk_widget_set_tooltip_text(P.title, short_name(name));
    g_free(nice);
    gtk_widget_set_visible(P.editor, TRUE);
    gtk_widget_set_visible(P.empty, FALSE);
    fill_used();
    gtk_label_set_text(GTK_LABEL(P.state), changed_in_pack(name) ? "changed" : "as in the game");
    msg("");
    gtk_widget_queue_draw(P.canvas);
}

static int frame_h(void) { return P.frames > 1 ? P.img.w : P.img.h; }

/* the canvas: pixel size and offset */
static void layout(int w, int h, double *k, double *ox, double *oy)
{
    int fh = frame_h();
    *k = floor(fmin((w - 8.0) / P.img.w, (h - 8.0) / fh));
    if (*k < 1)
        *k = fmin((w - 8.0) / P.img.w, (h - 8.0) / fh);
    *ox = (w - P.img.w * *k) / 2;
    *oy = (h - fh * *k) / 2;
}

static void draw_canvas(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    (void)data;
    if (!P.img.px)
        return;
    double k, ox, oy;
    layout(w, h, &k, &ox, &oy);
    int fh = frame_h();
    /* checkerboard under transparent pixels */
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < P.img.w; x++) {
            double c = (x + y) & 1 ? 0.22 : 0.3;
            cairo_set_source_rgb(cr, c, c, c);
            cairo_rectangle(cr, ox + x * k, oy + y * k, k, k);
            cairo_fill(cr);
        }
    cairo_surface_t *s = img_surface(&P.img, P.frame * fh, fh);
    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(s);
    if (k >= 8) {                         /* a faint pixel grid */
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_set_line_width(cr, 1);
        for (int x = 0; x <= P.img.w; x++) {
            cairo_move_to(cr, ox + x * k + 0.5, oy);
            cairo_line_to(cr, ox + x * k + 0.5, oy + fh * k);
        }
        for (int y = 0; y <= fh; y++) {
            cairo_move_to(cr, ox, oy + y * k + 0.5);
            cairo_line_to(cr, ox + P.img.w * k, oy + y * k + 0.5);
        }
        cairo_stroke(cr);
    }
}

static bool to_pixel(double px, double py, int *x, int *y)
{
    double k, ox, oy;
    layout(gtk_widget_get_width(P.canvas), gtk_widget_get_height(P.canvas), &k, &ox, &oy);
    *x = (int)floor((px - ox) / k);
    *y = (int)floor((py - oy) / k);
    return *x >= 0 && *y >= 0 && *x < P.img.w && *y < frame_h();
}

static guint8 *pixel(int x, int y) { return P.img.px + ((gsize)(P.frame * frame_h() + y) * P.img.w + x) * 4; }

static void set_colour(const guint8 c[4])
{
    memcpy(P.colour, c, 4);
    GdkRGBA r = {c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, c[3] / 255.0};
    mc_colour_set(P.colour_btn, &r);
}

static void set_tool(int t)
{
    P.tool = t;
    for (int i = 0; i < T_N; i++)
        if (i == t)
            gtk_widget_add_css_class(P.tool_btn[i], "active");
        else
            gtk_widget_remove_css_class(P.tool_btn[i], "active");
}

static void fill_at(int x0, int y0)
{
    int fh = frame_h();
    guint8 from[4];
    memcpy(from, pixel(x0, y0), 4);
    if (!memcmp(from, P.colour, 4))
        return;
    GArray *st = g_array_new(FALSE, FALSE, sizeof(int));
    int s = y0 * P.img.w + x0;
    g_array_append_val(st, s);
    while (st->len) {
        int c = g_array_index(st, int, st->len - 1);
        g_array_set_size(st, st->len - 1);
        int x = c % P.img.w, y = c / P.img.w;
        guint8 *p = pixel(x, y);
        if (memcmp(p, from, 4))
            continue;
        memcpy(p, P.colour, 4);
        int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
        for (int i = 0; i < 4; i++)
            if (nb[i][0] >= 0 && nb[i][1] >= 0 && nb[i][0] < P.img.w && nb[i][1] < fh) {
                int k = nb[i][1] * P.img.w + nb[i][0];
                g_array_append_val(st, k);
            }
    }
    g_array_unref(st);
}

static void plot(int x, int y, bool erase)
{
    static const guint8 clear[4] = {0};
    memcpy(pixel(x, y), erase ? clear : P.colour, 4);
}

static void line(int x0, int y0, int x1, int y1, bool erase)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        plot(x0, y0, erase);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static int drag_button;
static double drag_x, drag_y;

static void on_press(GtkGestureDrag *g, double x, double y, gpointer d)
{
    (void)d;
    int px, py;
    drag_button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    drag_x = x;
    drag_y = y;
    P.stroke = false;
    if (!P.cur || !to_pixel(x, y, &px, &py))
        return;
    int tool = drag_button == 3 ? T_ERASE : P.tool;
    if (tool == T_PICK) {
        set_colour(pixel(px, py));
        set_tool(T_DRAW);
        return;
    }
    push_undo();
    P.stroke = true;
    if (tool == T_FILL)
        fill_at(px, py);
    else
        plot(px, py, tool == T_ERASE);
    P.last_x = px;
    P.last_y = py;
    gtk_widget_queue_draw(P.canvas);
}

static void on_drag(GtkGestureDrag *g, double ox, double oy, gpointer d)
{
    (void)g;
    (void)d;
    int px, py;
    int tool = drag_button == 3 ? T_ERASE : P.tool;
    if (!P.stroke || tool == T_FILL || !to_pixel(drag_x + ox, drag_y + oy, &px, &py))
        return;
    line(P.last_x, P.last_y, px, py, tool == T_ERASE);
    P.last_x = px;
    P.last_y = py;
    gtk_widget_queue_draw(P.canvas);
}

static void on_release(GtkGestureDrag *g, double ox, double oy, gpointer d)
{
    (void)g;
    (void)ox;
    (void)oy;
    (void)d;
    if (!P.stroke)
        return;
    P.stroke = false;
    img_copy(&P.base, &P.img);
    reset_sliders();
    save_cur();
    fill_used();
}

static void on_colour(GtkWidget *b, gpointer d)
{
    (void)d;
    GdkRGBA cc;
    mc_colour_get(b, &cc);
    const GdkRGBA *c = &cc;
    P.colour[0] = clamp8(c->red);
    P.colour[1] = clamp8(c->green);
    P.colour[2] = clamp8(c->blue);
    P.colour[3] = clamp8(c->alpha);
}

static void draw_sw(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    guint32 c = GPOINTER_TO_UINT(data);
    cairo_set_source_rgb(cr, (c >> 24 & 255) / 255.0, (c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
}

static void on_sw(GtkButton *b, gpointer data)
{
    (void)b;
    guint32 c = GPOINTER_TO_UINT(data);
    guint8 k[4] = {c >> 24, c >> 16 & 255, c >> 8 & 255, c & 255};
    set_colour(k);
    if (P.tool != T_FILL)
        set_tool(T_DRAW);
}

/* the texture's own colours, most used first */
static void fill_used(void)
{
    gtk_flow_box_remove_all(GTK_FLOW_BOX(P.used));
    if (!P.img.px)
        return;
    GHashTable *cnt = g_hash_table_new(NULL, NULL);
    for (int k = 0; k < P.img.w * frame_h(); k++) {
        const guint8 *p = P.img.px + ((gsize)P.frame * frame_h() * P.img.w + k) * 4;
        if (p[3] < 8)
            continue;
        guint32 c = (guint32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
        gpointer key = GUINT_TO_POINTER(c);
        g_hash_table_insert(cnt, key, GINT_TO_POINTER(GPOINTER_TO_INT(g_hash_table_lookup(cnt, key)) + 1));
    }
    for (int shown = 0; shown < 24 && g_hash_table_size(cnt); shown++) {
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
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "mcl-swatch");
        GtkWidget *da = gtk_drawing_area_new();
        gtk_widget_set_size_request(da, 16, 16);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_sw, best, NULL);
        gtk_button_set_child(GTK_BUTTON(b), da);
        g_signal_connect(b, "clicked", G_CALLBACK(on_sw), best);
        gtk_flow_box_append(GTK_FLOW_BOX(P.used), b);
    }
    g_hash_table_unref(cnt);
}

static void on_tool(GtkButton *b, gpointer d) { (void)b; set_tool(GPOINTER_TO_INT(d)); }

static void on_adjust(GtkRange *r, gpointer d)
{
    (void)r;
    (void)d;
    if (P.loading || !P.base.px)
        return;
    img_copy(&P.img, &P.base);
    adjust(&P.img, gtk_range_get_value(GTK_RANGE(P.adj[0])), gtk_range_get_value(GTK_RANGE(P.adj[1])) / 100,
           gtk_range_get_value(GTK_RANGE(P.adj[2])) / 100, gtk_range_get_value(GTK_RANGE(P.adj[3])) / 100);
    gtk_widget_queue_draw(P.canvas);
    msg("Previewing. \"Keep this\" saves it.");
}

static void on_keep(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    if (!P.cur)
        return;
    img_t now = {0};
    img_copy(&now, &P.img);
    img_copy(&P.img, &P.base);
    push_undo();
    img_copy(&P.img, &now);
    img_free(&now);
    img_copy(&P.base, &P.img);
    reset_sliders();
    save_cur();
    fill_used();
}

static void on_undo(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    if (!P.undo->len)
        return;
    img_t *u = g_ptr_array_steal_index(P.undo, P.undo->len - 1);
    img_copy(&P.img, u);
    free_img(u);
    img_copy(&P.base, &P.img);
    reset_sliders();
    save_cur();
    fill_used();
    gtk_widget_queue_draw(P.canvas);
}

static void on_original(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    if (!P.cur)
        return;
    char *f = pack_file(P.cur), *n = g_strdup(P.cur);
    g_remove(f);
    g_free(f);
    select_texture(n);
    g_free(n);
    fill_grid();
    msg("Back to the game's own texture.");
}

static void on_frame(GtkButton *b, gpointer d)
{
    (void)b;
    P.frame = (P.frame + GPOINTER_TO_INT(d) + P.frames) % P.frames;
    frame_label();
    fill_used();
    gtk_widget_queue_draw(P.canvas);
}

static void on_picture_done(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f || !P.cur) {
        if (f)
            g_object_unref(f);
        return;
    }
    char *p = g_file_get_path(f), *d = NULL;
    gsize l;
    img_t pic = {0};
    if (g_file_get_contents(p, &d, &l, NULL)) {
        GBytes *b = g_bytes_new_take(d, l);
        img_from_bytes(b, &pic);
        g_bytes_unref(b);
    }
    if (pic.px) {                         /* nearest-neighbour into the current frame */
        push_undo();
        int fh = frame_h();
        for (int y = 0; y < fh; y++)
            for (int x = 0; x < P.img.w; x++)
                memcpy(pixel(x, y), pic.px + ((gsize)(y * pic.h / fh) * pic.w + x * pic.w / P.img.w) * 4, 4);
        img_free(&pic);
        img_copy(&P.base, &P.img);
        save_cur();
        fill_used();
        gtk_widget_queue_draw(P.canvas);
    } else {
        msg("That picture couldn't be read.");
    }
    g_free(p);
    g_object_unref(f);
}

static void on_picture(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Picture for this texture");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Pictures");
    gtk_file_filter_add_mime_type(ff, "image/png");
    gtk_file_filter_add_mime_type(ff, "image/jpeg");
    gtk_file_filter_add_mime_type(ff, "image/webp");
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(fd, G_LIST_MODEL(fl));
    gtk_file_dialog_open(fd, P.a->win, NULL, on_picture_done, NULL);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(fd);
}

static void on_external(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    if (!P.cur)
        return;
    save_cur();
    char *f = pack_file(P.cur);
    mc_open_path(P.a, f);
    g_free(f);
    msg("Opened in your image editor. Pick the texture again here after saving there.");
}

/* ------------------------------------------------------- packs, on/off -- */

static char *options_path(void) { return g_build_filename(P.a->mcdir, "options.txt", NULL); }

/* is file/<pack> in options.txt's resourcePacks list */
static bool pack_on(void)
{
    char *p = options_path(), *txt = NULL;
    bool on = false;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char *want = g_strdup_printf("\"file/%s\"", P.pack);
        for (char *l = txt; l && *l;) {
            char *e = strchr(l, '\n');
            if (g_str_has_prefix(l, "resourcePacks:")) {
                char *line = e ? g_strndup(l, e - l) : g_strdup(l);
                on = strstr(line, want) != NULL;
                g_free(line);
            }
            l = e ? e + 1 : NULL;
        }
        g_free(want);
    }
    g_free(txt);
    g_free(p);
    return on;
}

static void set_pack_on(bool on)
{
    char *p = options_path(), *txt = NULL;
    g_file_get_contents(p, &txt, NULL, NULL);
    GString *out = g_string_new(NULL);
    char *want = g_strdup_printf("\"file/%s\"", P.pack);
    bool found = false;
    char **lines = g_strsplit(txt ? txt : "", "\n", -1);
    for (char **l = lines; *l; l++) {
        if (g_str_has_prefix(*l, "resourcePacks:[")) {
            found = true;
            GString *nl = g_string_new("resourcePacks:[");
            char *in = g_strndup(*l + 15, strlen(*l + 15) - (g_str_has_suffix(*l, "]") ? 1 : 0));
            char **items = g_strsplit(in, ",", -1);
            bool first = true;
            for (char **it = items; *it; it++) {
                if (!**it || !strcmp(*it, want))
                    continue;
                g_string_append_printf(nl, "%s%s", first ? "" : ",", *it);
                first = false;
            }
            if (on)                       /* last = on top of the others */
                g_string_append_printf(nl, "%s%s", first ? "" : ",", want);
            g_string_append(nl, "]");
            g_string_append(out, nl->str);
            g_string_free(nl, TRUE);
            g_strfreev(items);
            g_free(in);
        } else {
            g_string_append(out, *l);
        }
        if (l[1])
            g_string_append_c(out, '\n');
    }
    if (!found && on)
        g_string_append_printf(out, "%sresourcePacks:[\"vanilla\",%s]\n", out->len && out->str[out->len - 1] != '\n' ? "\n" : "", want);
    g_file_set_contents(p, out->str, -1, NULL);
    g_strfreev(lines);
    g_free(want);
    g_string_free(out, TRUE);
    g_free(txt);
    g_free(p);
}

static void on_onoff(GObject *s, GParamSpec *ps, gpointer d)
{
    (void)ps;
    (void)d;
    if (P.loading)
        return;
    bool on = gtk_switch_get_active(GTK_SWITCH(s));
    if (mc_game_running(P.a)) {
        msg("The game is running and would overwrite this. In game: Options, Resource Packs.");
        P.loading = true;
        gtk_switch_set_active(GTK_SWITCH(s), !on);
        P.loading = false;
        return;
    }
    ensure_pack(NULL);
    set_pack_on(on);
    msg(on ? "On: the game uses it next time it starts." : "Off.");
}

static void fill_packs(void);

static void use_pack(const char *name)
{
    char *n = g_strdup(name);            /* name may be P.pack itself */
    g_free(P.pack);
    P.pack = n;
    if (!g_getenv("TUX_MC_PACK"))
        mc_cfg_set_str("pack", n);
    P.loading = true;
    gtk_switch_set_active(GTK_SWITCH(P.onoff), pack_on());
    P.loading = false;
    fill_grid();
    if (P.cur) {
        char *n = g_strdup(P.cur);
        select_texture(n);
        g_free(n);
    }
}

static void on_pack(GObject *dd, GParamSpec *ps, gpointer d)
{
    (void)ps;
    (void)d;
    if (P.filling)
        return;
    GtkStringObject *o = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dd));
    if (o)
        use_pack(gtk_string_object_get_string(o));
}

static void on_new_pack(GtkEntry *e, gpointer d)
{
    (void)d;
    char *n = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(e))));
    for (char *c = n; *c; c++)
        if (strchr("/\\:*?\"<>|", *c))
            *c = '-';
    if (*n && strcmp(n, ".") && strcmp(n, "..")) {
        g_free(P.pack);
        P.pack = g_strdup(n);
        ensure_pack(NULL);
        mc_cfg_set_str("pack", n);
        fill_packs();
        gtk_editable_set_text(GTK_EDITABLE(e), "");
        GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(e), GTK_TYPE_POPOVER);
        if (pop)
            gtk_popover_popdown(GTK_POPOVER(pop));
        msg("New pack made. Everything you change now goes into it.");
    }
    g_free(n);
}

static void fill_packs(void)
{
    P.filling = true;
    GtkStringList *sl = gtk_string_list_new(NULL);
    char *root = packs_root();
    GDir *d = g_dir_open(root, 0, NULL);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        char *meta = g_build_filename(root, e, "pack.mcmeta", NULL);
        if (g_file_test(meta, G_FILE_TEST_EXISTS))
            g_ptr_array_add(names, g_strdup(e));
        g_free(meta);
    }
    if (d)
        g_dir_close(d);
    g_free(root);
    bool have = false;
    for (guint i = 0; i < names->len; i++)
        have |= !strcmp(names->pdata[i], P.pack);
    if (!have)
        g_ptr_array_add(names, g_strdup(P.pack));
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
    guint sel = 0;
    for (guint i = 0; i < names->len; i++) {
        gtk_string_list_append(sl, names->pdata[i]);
        if (!strcmp(names->pdata[i], P.pack))
            sel = i;
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(P.packs), G_LIST_MODEL(sl));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(P.packs), sel);
    g_object_unref(sl);
    g_ptr_array_unref(names);
    P.filling = false;
    use_pack(P.pack);
}

static void on_export_done(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)data;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f)
        return;
    char *p = g_file_get_path(f), *d = pack_dir(), *err = NULL;
    if (mc_zip_write_dir(d, p, &err)) {
        char *m = g_strdup_printf("Saved %s. Anyone can drop it into their resourcepacks folder.", p);
        msg(m);
        g_free(m);
    } else {
        msg(err);
    }
    g_free(err);
    g_free(d);
    g_free(p);
    g_object_unref(f);
}

static void on_export(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    ensure_pack(NULL);
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Save the pack as a zip");
    char *n = g_strconcat(P.pack, ".zip", NULL);
    gtk_file_dialog_set_initial_name(fd, n);
    g_free(n);
    const char *dl = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (dl) {
        GFile *df = g_file_new_for_path(dl);
        gtk_file_dialog_set_initial_folder(fd, df);
        g_object_unref(df);
    }
    gtk_file_dialog_save(fd, P.a->win, NULL, on_export_done, NULL);
    g_object_unref(fd);
}

static void on_folder(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    ensure_pack(NULL);
    char *p = pack_dir();
    mc_open_path(P.a, p);
    g_free(p);
}

/* -------------------------------------------------------------- setup -- */

static void collect(const char *name, void *data)
{
    if (g_str_has_prefix(name, "assets/minecraft/textures/") && g_str_has_suffix(name, ".png"))
        g_ptr_array_add(data, g_strdup(name));
}

static void open_jar(void)
{
    g_free(P.jar);
    P.jar = NULL;
    mc_zip_close(P.zip);
    P.zip = NULL;
    if (P.names)
        g_ptr_array_unref(P.names);
    P.names = NULL;
    if (!P.a->mc_version)
        return;
    char *lib = g_build_filename(P.a->prism, "libraries", "com", "mojang", "minecraft", P.a->mc_version, NULL);
    char *f = g_strdup_printf("minecraft-%s-client.jar", P.a->mc_version);
    P.jar = g_build_filename(lib, f, NULL);
    g_free(f);
    g_free(lib);
    P.zip = mc_zip_open(P.jar);
    if (!P.zip)
        return;
    P.names = g_ptr_array_new_with_free_func(g_free);
    mc_zip_names(P.zip, collect, P.names);
    g_ptr_array_sort_values(P.names, (GCompareFunc)g_strcmp0);
    P.format = 0;
    GBytes *vj = mc_zip_get(P.zip, "version.json");
    if (vj) {                             /* "resource_major": 97 */
        char *s = g_strndup(g_bytes_get_data(vj, NULL), g_bytes_get_size(vj));
        char *r = strstr(s, "\"resource_major\"");
        if (!r)
            r = strstr(s, "\"resource\"");
        if (r && (r = strchr(r, ':')))
            P.format = atoi(r + 1);
        g_free(s);
        g_bytes_unref(vj);
    }
    if (P.format <= 0)
        P.format = 97;
}

static GtkWidget *button(const char *l, const char *tip, GCallback cb, gpointer d)
{
    GtkWidget *b = gtk_button_new_with_label(l);
    if (tip)
        gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, d);
    return b;
}

static GtkWidget *slider_row(GtkWidget *box, const char *label, double lo, double hi, double val)
{
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *l = mc_label(label, "dim");
    gtk_label_set_width_chars(GTK_LABEL(l), 10);
    gtk_box_append(GTK_BOX(r), l);
    GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, 1);
    gtk_range_set_value(GTK_RANGE(s), val);
    gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
    gtk_widget_set_hexpand(s, TRUE);
    g_signal_connect(s, "value-changed", G_CALLBACK(on_adjust), NULL);
    gtk_box_append(GTK_BOX(r), s);
    gtk_box_append(GTK_BOX(box), r);
    return s;
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    (void)w;
    (void)d;
    P.grid = NULL;
}

static GtkWidget *step(const char *num, const char *text)
{
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *n = mc_label(num, "mcl-step");
    gtk_widget_set_valign(n, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(r), n);
    GtkWidget *t = mc_label(text, NULL);
    gtk_label_set_wrap(GTK_LABEL(t), TRUE);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(r), t);
    return r;
}

static void menu_item(GtkWidget *box, const char *label, GCallback cb)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(b)), GTK_ALIGN_START);
    g_signal_connect(b, "clicked", cb, NULL);
    gtk_box_append(GTK_BOX(box), b);
}

GtkWidget *mc_page_packs(app_t *a)
{
    P.a = a;
    if (!P.undo) {
        P.undo = g_ptr_array_new_with_free_func(free_img);
        P.colour[0] = 0x3f;
        P.colour[1] = 0x86;
        P.colour[2] = 0xd9;
        P.colour[3] = 255;
        P.cat = C_BLOCK;
    }
    open_jar();
    g_free(P.pack);
    P.pack = g_getenv("TUX_MC_PACK") ? g_strdup(g_getenv("TUX_MC_PACK")) : mc_cfg_str("pack", "My pack");

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(page, "mcl-page");
    g_signal_connect(page, "destroy", G_CALLBACK(on_destroy), NULL);
    gtk_box_append(GTK_BOX(page), mc_label("Packs", "mcl-title"));

    /* the pack: which one, on in game, share */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(top), mc_label("Your pack", "dim"));
    P.packs = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_size_request(P.packs, 220, -1);
    g_signal_connect(P.packs, "notify::selected", G_CALLBACK(on_pack), NULL);
    gtk_box_append(GTK_BOX(top), P.packs);
    GtkWidget *nb = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(nb), "New");
    gtk_widget_set_tooltip_text(nb, "start another pack");
    GtkWidget *pop = gtk_popover_new();
    GtkWidget *pb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(pb), mc_label("Name it, then press Enter", "dim"));
    GtkWidget *ne = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ne), "My pack");
    g_signal_connect(ne, "activate", G_CALLBACK(on_new_pack), NULL);
    gtk_box_append(GTK_BOX(pb), ne);
    gtk_popover_set_child(GTK_POPOVER(pop), pb);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(nb), pop);
    gtk_box_append(GTK_BOX(top), nb);
    GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(sp, TRUE);
    gtk_box_append(GTK_BOX(top), sp);
    gtk_box_append(GTK_BOX(top), mc_label("Use in game", NULL));
    P.onoff = gtk_switch_new();
    gtk_widget_set_valign(P.onoff, GTK_ALIGN_CENTER);
    g_signal_connect(P.onoff, "notify::active", G_CALLBACK(on_onoff), NULL);
    gtk_box_append(GTK_BOX(top), P.onoff);
    gtk_box_append(GTK_BOX(top), button("Share as zip…", "save a file other people can use", G_CALLBACK(on_export), NULL));
    gtk_box_append(GTK_BOX(page), top);

    if (!P.names) {
        GtkWidget *c = mc_card("The game's textures aren't here yet",
                               "Start this instance once from Home, so Prism downloads the game. Then come back.");
        gtk_box_append(GTK_BOX(page), c);
        gtk_widget_set_sensitive(top, FALSE);
        return page;
    }

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_vexpand(row, TRUE);
    gtk_box_append(GTK_BOX(page), row);

    /* left: find a texture */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(left, TRUE);
    P.search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(P.search), "Find a texture: stone, diamond sword, zombie…");
    g_signal_connect(P.search, "search-changed", G_CALLBACK(on_filter), NULL);
    gtk_box_append(GTK_BOX(left), P.search);
    GtkWidget *chips = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *first = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(CHIP_ORDER); i++) {
        int c = CHIP_ORDER[i];
        GtkWidget *b = gtk_toggle_button_new_with_label(CATS[c]);
        gtk_widget_add_css_class(b, "mcl-chip");
        if (first)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(first));
        else
            first = b;
        if (c == P.cat)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
        g_signal_connect(b, "toggled", G_CALLBACK(on_chip), GINT_TO_POINTER(c));
        P.chips[c] = b;
        gtk_box_append(GTK_BOX(chips), b);
    }
    gtk_box_append(GTK_BOX(left), chips);
    GtkWidget *gsc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(gsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(gsc, TRUE);
    gtk_widget_add_css_class(gsc, "mcl-glass");
    P.grid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(P.grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(P.grid), 30);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(P.grid), TRUE);
    gtk_widget_set_valign(P.grid, GTK_ALIGN_START);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(gsc), P.grid);
    gtk_box_append(GTK_BOX(left), gsc);
    P.count = mc_label("", "dim");
    gtk_box_append(GTK_BOX(left), P.count);
    gtk_box_append(GTK_BOX(row), left);

    /* right: change it */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *rsc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(rsc, 520, -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(rsc), right);

    P.empty = mc_card("How it works", NULL);
    gtk_box_append(GTK_BOX(P.empty), step("1", "Pick a texture on the left. Search by name or use the buttons above "
                                                "the list."));
    gtk_box_append(GTK_BOX(P.empty), step("2", "Draw on it here. Every change is saved by itself, and Undo takes it "
                                                "back."));
    gtk_box_append(GTK_BOX(P.empty), step("3", "Switch on \"Use in game\" at the top. If the game is open, press "
                                                "F3+T in it to see your changes."));
    gtk_box_append(GTK_BOX(right), P.empty);

    P.editor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(P.editor, "mcl-glass");
    GtkWidget *hr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    P.title = mc_label("", "mcl-h2");
    gtk_label_set_ellipsize(GTK_LABEL(P.title), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(hr), P.title);
    P.state = mc_label("", "dim");
    gtk_widget_set_hexpand(P.state, TRUE);
    gtk_box_append(GTK_BOX(hr), P.state);
    gtk_box_append(GTK_BOX(hr), button("Undo", NULL, G_CALLBACK(on_undo), NULL));
    gtk_box_append(GTK_BOX(hr), button("Reset", "back to the game's own texture", G_CALLBACK(on_original), NULL));
    GtkWidget *more = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(more), "More");
    GtkWidget *mp = gtk_popover_new();
    GtkWidget *mb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    menu_item(mb, "Use a picture for it…", G_CALLBACK(on_picture));
    menu_item(mb, "Open in another image editor", G_CALLBACK(on_external));
    menu_item(mb, "Open the pack's folder", G_CALLBACK(on_folder));
    gtk_popover_set_child(GTK_POPOVER(mp), mb);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(more), mp);
    gtk_box_append(GTK_BOX(hr), more);
    gtk_box_append(GTK_BOX(P.editor), hr);

    GtkWidget *tr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    static const char *const TIPS[T_N] = {"draw with the colour (right button erases)", "make pixels see-through",
                                          "colour every touching pixel of the same colour", "take a colour from the texture"};
    for (int i = 0; i < T_N; i++) {
        P.tool_btn[i] = button(TOOL_NAMES[i], TIPS[i], G_CALLBACK(on_tool), GINT_TO_POINTER(i));
        gtk_widget_add_css_class(P.tool_btn[i], "mcl-tool");
        gtk_box_append(GTK_BOX(tr), P.tool_btn[i]);
    }
    gtk_box_append(GTK_BOX(P.editor), tr);

    P.canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(P.canvas, 360, 320);
    gtk_widget_add_css_class(P.canvas, "mcl-canvas");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(P.canvas), draw_canvas, NULL, NULL);
    GtkGesture *g = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);
    g_signal_connect(g, "drag-begin", G_CALLBACK(on_press), NULL);
    g_signal_connect(g, "drag-update", G_CALLBACK(on_drag), NULL);
    g_signal_connect(g, "drag-end", G_CALLBACK(on_release), NULL);
    gtk_widget_add_controller(P.canvas, GTK_EVENT_CONTROLLER(g));
    gtk_box_append(GTK_BOX(P.editor), P.canvas);
    P.frame_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(P.frame_box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(P.frame_box), button("◀", "previous frame", G_CALLBACK(on_frame), GINT_TO_POINTER(-1)));
    P.frame_lbl = mc_label("", "dim");
    gtk_box_append(GTK_BOX(P.frame_box), P.frame_lbl);
    gtk_box_append(GTK_BOX(P.frame_box), button("▶", "next frame", G_CALLBACK(on_frame), GINT_TO_POINTER(1)));
    gtk_box_append(GTK_BOX(P.editor), P.frame_box);

    /* colour: the wheel, and the texture's own colours next to it */
    GtkWidget *cr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    P.colour_btn = mc_colour_wheel_new(true, 180);
    mc_colour_on_change(P.colour_btn, on_colour, NULL);
    gtk_box_append(GTK_BOX(cr), P.colour_btn);
    GtkWidget *uc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(uc, TRUE);
    gtk_box_append(GTK_BOX(uc), mc_label("Already in this texture", "dim"));
    P.used = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(P.used), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(P.used), 6);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(P.used), 4);
    gtk_widget_set_valign(P.used, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(uc), P.used);
    gtk_box_append(GTK_BOX(cr), uc);
    gtk_box_append(GTK_BOX(P.editor), cr);

    GtkWidget *exp = gtk_expander_new("Change the colours of the whole texture");
    GtkWidget *eb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(eb, 8);
    P.adj[0] = slider_row(eb, "Hue", -180, 180, 0);
    P.adj[1] = slider_row(eb, "Saturation", 0, 200, 100);
    P.adj[2] = slider_row(eb, "Brightness", -60, 60, 0);
    P.adj[3] = slider_row(eb, "Contrast", 40, 200, 100);
    GtkWidget *kb = button("Keep this", "save what the sliders did", G_CALLBACK(on_keep), NULL);
    gtk_widget_set_halign(kb, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(eb), kb);
    gtk_expander_set_child(GTK_EXPANDER(exp), eb);
    gtk_box_append(GTK_BOX(P.editor), exp);
    gtk_widget_set_visible(P.editor, FALSE);
    gtk_box_append(GTK_BOX(right), P.editor);
    P.msg = mc_label("", "dim");
    gtk_label_set_wrap(GTK_LABEL(P.msg), TRUE);
    gtk_box_append(GTK_BOX(right), P.msg);
    gtk_box_append(GTK_BOX(row), rsc);

    set_tool(T_DRAW);
    set_colour(P.colour);
    fill_packs();
    const char *test = g_getenv("TUX_MC_TEXTURE");            /* screenshots */
    if (test) {
        char *n = g_strconcat("assets/minecraft/textures/", test, ".png", NULL);
        select_texture(n);
        g_free(n);
    }
    return page;
}
