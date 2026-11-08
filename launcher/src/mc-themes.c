/* mc-themes.c — Settings → Themes: colour presets for the launcher and Prism.
 *
 * A preset is the 13 colours the stylesheet template (theme/tux-mc.css) uses.
 * Built-in ones live here, your own are ~/.config/tux-mc/presets/<name>.sh
 * (same key="#rrggbb" lines as theme/palettes/ files), and the desktop themes in
 * ~/.config/themes/<name>/theme.sh show up too when they exist.
 * Picking one previews it at once; Apply writes ~/.config/tux-mc/style.css
 * (the launcher reloads it) and Prism's "tux" theme when Prism is closed.
 */
#include "mc.h"
#include "theme-black.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define NKEYS 13
static const struct { const char *key, *label; } KEYS[NKEYS] = {
    {"bg", "Background"},        {"bg_alt", "Panels"},          {"surface", "Raised"},
    {"selection", "Selection"},  {"muted", "Borders"},          {"dim", "Faint text"},
    {"subtext", "Second text"},  {"fg", "Text"},                {"fg_bright", "Bright text"},
    {"accent", "Accent"},        {"accent_bright", "Accent, bright"}, {"urgent", "Alert"},
    {"warn", "Warning"},
};

typedef struct {
    char *name, *file;                   /* file: NULL = built in */
    bool own;                            /* in ~/.config/tux-mc/presets: can be deleted */
    char col[NKEYS][8];
} preset_t;

static const struct { const char *name; const char *c[NKEYS]; } BUILTIN[] = {
    {"Black", {"#000000", "#0a0a0a", "#1a1a1a", "#333333", "#4a4a4a", "#707070", "#a6a6a6", "#ffffff", "#ffffff",
               "#ffffff", "#ffffff", "#ffffff", "#d9d9d9"}},
    {"Paper", {"#f4f1ea", "#ebe6dc", "#ddd6c8", "#cfc6b4", "#a89f8c", "#857c6a", "#5e5648", "#2b2620", "#14110d",
               "#8a5a2b", "#6e4520", "#a33a2a", "#9a6b16"}},
    {"Deep sea", {"#06111c", "#0a1826", "#12263a", "#1c3650", "#2d4b66", "#557590", "#8fa9bf", "#d6e3ee", "#f2f7fb",
                  "#4f9bd1", "#7cc0f0", "#e0705a", "#e0b45a"}},
    {"Moss", {"#0d120c", "#131a11", "#1f2a1c", "#2c3b28", "#43553d", "#67785f", "#9fae95", "#dde5d4", "#f3f7ee",
              "#8fb46a", "#b5d68f", "#d9785b", "#d8b25e"}},
    {"Ember", {"#120c0a", "#1a110e", "#2a1b16", "#3b2620", "#57382e", "#7d5a4e", "#b39488", "#eadbd3", "#fbf3ee",
               "#e07b39", "#f4a46a", "#e5533d", "#e8b04a"}},
};

static struct {
    app_t *a;
    GPtrArray *presets;                  /* preset_t */
    GtkWidget *flow, *name, *btn[NKEYS], *apply, *del, *msg, *simple[3], *undo;
    preset_t cur;                        /* what the editor shows */
    preset_t prev;                       /* what was in use before the last click, for Undo */
    char prev_name[64];
    preset_t applied;                    /* what's in use now */
    char applied_name[64];
    bool loading, have_prev;
} T;

/* ---------------------------------------------------------------- files -- */

static void preset_free(preset_t *p)
{
    g_free(p->name);
    g_free(p->file);
    g_free(p);
}

static char *presets_dir(void) { return g_build_filename(g_get_user_config_dir(), "tux-mc", "presets", NULL); }

/* key="#rrggbb" lines; missing keys keep what was there */
static bool read_palette(const char *path, preset_t *p)
{
    char *txt = NULL;
    if (!g_file_get_contents(path, &txt, NULL, NULL))
        return false;
    int found = 0;
    char **lines = g_strsplit(txt, "\n", -1);
    for (char **l = lines; *l; l++) {
        char *s = g_strstrip(*l), *eq = strchr(s, '=');
        if (!eq || *s == '#')
            continue;
        *eq = 0;
        const char *v = eq + 1;
        if (*v == '"' || *v == '\'')
            v++;
        if (!strcmp(s, "name") && !p->name) {
            char *n = g_strdup(v);
            char *q = strpbrk(n, "\"'");
            if (q)
                *q = 0;
            p->name = n;
            continue;
        }
        if (*v != '#' || strlen(v) < 7)
            continue;
        for (int k = 0; k < NKEYS; k++)
            if (!strcmp(s, KEYS[k].key)) {
                g_strlcpy(p->col[k], v, 8);
                found++;
            }
    }
    g_strfreev(lines);
    g_free(txt);
    return found >= 8;
}

static void builtin(preset_t *p, int i)
{
    for (int k = 0; k < NKEYS; k++)
        g_strlcpy(p->col[k], BUILTIN[i].c[k], 8);
}

static GPtrArray *load_all(void)
{
    GPtrArray *all = g_ptr_array_new_with_free_func((GDestroyNotify)preset_free);
    for (size_t i = 0; i < G_N_ELEMENTS(BUILTIN); i++) {
        preset_t *p = g_new0(preset_t, 1);
        p->name = g_strdup(BUILTIN[i].name);
        builtin(p, (int)i);
        g_ptr_array_add(all, p);
    }
    /* yours, then the desktop's */
    char *dirs[2] = {presets_dir(), g_build_filename(g_get_user_config_dir(), "themes", NULL)};
    for (int d = 0; d < (g_getenv("TUX_MC_SHOTS") ? 0 : 2); d++) {     /* screenshots: built-in ones only */
        GDir *dir = g_dir_open(dirs[d], 0, NULL);
        GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
        for (const char *e; dir && (e = g_dir_read_name(dir));)
            g_ptr_array_add(names, g_strdup(e));
        if (dir)
            g_dir_close(dir);
        g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
        for (guint i = 0; i < names->len; i++) {
            const char *e = names->pdata[i];
            char *path = d == 0 ? (g_str_has_suffix(e, ".sh") ? g_build_filename(dirs[d], e, NULL) : NULL)
                                : (strcmp(e, "current") && strcmp(e, "templates")
                                       ? g_build_filename(dirs[d], e, "theme.sh", NULL) : NULL);
            preset_t *p = g_new0(preset_t, 1);
            builtin(p, 0);
            bool ok = path && read_palette(path, p), dup = false;
            for (guint k = 0; ok && k < all->len && !dup; k++) {  /* same colours as one listed already */
                const preset_t *q = all->pdata[k];
                dup = true;
                for (int c = 0; c < NKEYS && dup; c++)
                    dup = !g_ascii_strcasecmp(q->col[c], p->col[c]);
            }
            if (ok && !dup) {
                if (!p->name)
                    p->name = d == 0 ? g_strndup(e, strlen(e) - 3) : g_strdup(e);
                if (d == 1) {             /* "Desktop: Moonlit Pines" */
                    char *n = g_strdup_printf("Desktop: %s", p->name);
                    g_free(p->name);
                    p->name = n;
                }
                p->file = path;
                p->own = d == 0;
                g_ptr_array_add(all, p);
            } else {
                g_free(path);
                preset_free(p);
            }
        }
        g_ptr_array_unref(names);
        g_free(dirs[d]);
    }
    return all;
}

/* ${key} -> colour; ${name} -> the preset's name */
static char *render(const char *tpl, const preset_t *p, const char *name)
{
    GString *out = g_string_new(NULL);
    for (const char *s = tpl; *s;) {
        if (s[0] == '$' && s[1] == '{') {
            const char *end = strchr(s, '}');
            if (end) {
                char *key = g_strndup(s + 2, end - s - 2);
                const char *v = NULL;
                for (int k = 0; k < NKEYS && !v; k++)
                    if (!strcmp(key, KEYS[k].key))
                        v = p->col[k];
                if (!v && !strcmp(key, "name"))
                    v = name;
                g_free(key);
                if (v) {
                    g_string_append(out, v);
                    s = end + 1;
                    continue;
                }
            }
        }
        g_string_append_c(out, *s++);
    }
    return g_string_free(out, FALSE);
}

/* -------------------------------------------------------------- editing -- */

static void preview(void)
{
    char *css = render(TUX_STYLE_TEMPLATE, &T.cur, "tux");
    gtk_css_provider_load_from_string(T.a->css, css);
    g_free(css);
}

static void editor_show(const preset_t *p)
{
    T.loading = true;
    T.cur = *p;
    T.cur.name = T.cur.file = NULL;
    gtk_editable_set_text(GTK_EDITABLE(T.name), p->name);
    for (int k = 0; k < NKEYS; k++) {
        GdkRGBA c;
        gdk_rgba_parse(&c, p->col[k]);
        mc_colour_set(T.btn[k], &c);
    }
    gtk_widget_set_visible(T.del, p->own);
    g_object_set_data_full(G_OBJECT(T.del), "file", g_strdup(p->file), g_free);
    T.loading = false;
    preview();
}

static void on_color(GtkWidget *b, gpointer data)
{
    if (T.loading)
        return;
    int k = GPOINTER_TO_INT(data);
    GdkRGBA cc;
    mc_colour_get(b, &cc);
    const GdkRGBA *c = &cc;
    g_snprintf(T.cur.col[k], 8, "#%02x%02x%02x", (int)(c->red * 255 + 0.5), (int)(c->green * 255 + 0.5),
               (int)(c->blue * 255 + 0.5));
    preview();
}

static void set_col(cairo_t *cr, const preset_t *p, int k)
{
    GdkRGBA c;
    gdk_rgba_parse(&c, p->col[k]);
    gdk_cairo_set_source_rgba(cr, &c);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
    cairo_close_path(cr);
}

/* a tiny launcher in the preset's colours: sidebar, title, text, a card and a button */
static void draw_swatch(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    const preset_t *p = data;
    rrect(cr, 0, 0, w, h, 8);
    cairo_clip(cr);
    set_col(cr, p, 0);
    cairo_paint(cr);
    double sw = w * 0.28;
    set_col(cr, p, 1);
    cairo_rectangle(cr, 0, 0, sw, h);
    cairo_fill(cr);
    for (int i = 0; i < 4; i++) {
        set_col(cr, p, i == 1 ? 7 : 5);
        cairo_rectangle(cr, sw * 0.18, h * (0.2 + i * 0.14), sw * (i == 1 ? 0.6 : 0.5), h * 0.05);
        cairo_fill(cr);
    }
    set_col(cr, p, 9);
    cairo_rectangle(cr, sw * 0.06, h * 0.33, 2, h * 0.09);
    cairo_fill(cr);
    double x = sw + w * 0.07;
    set_col(cr, p, 8);
    cairo_rectangle(cr, x, h * 0.14, w * 0.3, h * 0.08);
    cairo_fill(cr);
    set_col(cr, p, 6);
    cairo_rectangle(cr, x, h * 0.3, w * 0.45, h * 0.045);
    cairo_fill(cr);
    set_col(cr, p, 2);
    rrect(cr, x, h * 0.44, w * 0.58, h * 0.3, 4);
    cairo_fill_preserve(cr);
    set_col(cr, p, 4);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    set_col(cr, p, 7);
    cairo_rectangle(cr, x + w * 0.04, h * 0.51, w * 0.3, h * 0.045);
    cairo_fill(cr);
    set_col(cr, p, 6);
    cairo_rectangle(cr, x + w * 0.04, h * 0.6, w * 0.4, h * 0.035);
    cairo_fill(cr);
    set_col(cr, p, 9);
    rrect(cr, x, h * 0.8, w * 0.22, h * 0.12, 3);
    cairo_fill(cr);
}

static void fill(void);
static void on_apply(GtkButton *b, gpointer data);
static void simple_from_cur(void);

static void remember_prev(void)
{
    T.prev = T.applied;
    g_strlcpy(T.prev_name, T.applied_name, sizeof T.prev_name);
    T.have_prev = true;
    gtk_widget_set_sensitive(T.undo, TRUE);
}

static void mark_active(const char *name)
{
    for (GtkWidget *c = gtk_widget_get_first_child(T.flow); c; c = gtk_widget_get_next_sibling(c)) {
        GtkWidget *b = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(c));
        const preset_t *p = g_object_get_data(G_OBJECT(b), "preset");
        if (p && !strcmp(p->name, name))
            gtk_widget_add_css_class(b, "active");
        else
            gtk_widget_remove_css_class(b, "active");
    }
}

/* one click: in use right away, everywhere */
static void on_pick(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    const preset_t *p = g_object_get_data(G_OBJECT(b), "preset");
    remember_prev();
    editor_show(p);
    simple_from_cur();
    on_apply(NULL, NULL);
    mark_active(p->name);
    char *m = g_strdup_printf("Using %s. Undo goes back to %s.", p->name, T.prev_name);
    gtk_label_set_text(GTK_LABEL(T.msg), m);
    g_free(m);
}

static void on_undo_pick(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    if (!T.have_prev)
        return;
    preset_t p = T.prev;
    p.name = T.prev_name;
    p.file = NULL;
    p.own = false;
    char back[64];
    g_strlcpy(back, T.prev_name, sizeof back);
    T.have_prev = false;
    gtk_widget_set_sensitive(T.undo, FALSE);
    editor_show(&p);
    simple_from_cur();
    on_apply(NULL, NULL);
    mark_active(back);
    char *m = g_strdup_printf("Back to %s.", back);
    gtk_label_set_text(GTK_LABEL(T.msg), m);
    g_free(m);
}

/* ----------------------------------------------- three colours -> all 13 -- */

static void rgb_of(const char *hex, double c[3])
{
    GdkRGBA r;
    gdk_rgba_parse(&r, hex);
    c[0] = r.red;
    c[1] = r.green;
    c[2] = r.blue;
}

static void put_mix(char out[8], const double a[3], const double b[3], double t)
{
    g_snprintf(out, 8, "#%02x%02x%02x", (int)lround((a[0] + (b[0] - a[0]) * t) * 255),
               (int)lround((a[1] + (b[1] - a[1]) * t) * 255), (int)lround((a[2] + (b[2] - a[2]) * t) * 255));
}

static double luma(const double c[3]) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

/* background, text and accent decide the rest: steps between bg and text */
static void derive(preset_t *p, const double bg[3], const double fg[3], const double ac[3])
{
    static const double STEP[] = {0, 0.035, 0.09, 0.16, 0.26, 0.42, 0.66};   /* bg .. subtext */
    for (int k = 0; k < 7; k++)
        put_mix(p->col[k], bg, fg, STEP[k]);
    put_mix(p->col[7], fg, fg, 0);
    double far[3] = {luma(bg) < 0.5 ? 1 : 0, luma(bg) < 0.5 ? 1 : 0, luma(bg) < 0.5 ? 1 : 0};
    put_mix(p->col[8], fg, far, 0.5);
    put_mix(p->col[9], ac, ac, 0);
    put_mix(p->col[10], ac, fg, 0.3);
    double red[3] = {0.9, 0.33, 0.24}, amber[3] = {0.88, 0.69, 0.3};
    put_mix(p->col[11], red, fg, 0.1);
    put_mix(p->col[12], amber, fg, 0.1);
}

static void simple_rgb(int i, double c[3])
{
    GdkRGBA rr;
    mc_colour_get(T.simple[i], &rr);
    const GdkRGBA *r = &rr;
    c[0] = r->red;
    c[1] = r->green;
    c[2] = r->blue;
}

static void simple_set(int i, const double c[3])
{
    GdkRGBA r = {c[0], c[1], c[2], 1};
    mc_colour_set(T.simple[i], &r);
}

static void simple_from_cur(void)
{
    T.loading = true;
    double c[3];
    static const int K[3] = {0, 7, 9};
    for (int i = 0; i < 3; i++) {
        rgb_of(T.cur.col[K[i]], c);
        simple_set(i, c);
    }
    T.loading = false;
}

static void editor_buttons_from_cur(void)
{
    T.loading = true;
    for (int k = 0; k < NKEYS; k++) {
        GdkRGBA c;
        gdk_rgba_parse(&c, T.cur.col[k]);
        mc_colour_set(T.btn[k], &c);
    }
    T.loading = false;
}

static void on_simple(GtkWidget *b, gpointer data)
{
    (void)b;
    (void)data;
    if (T.loading)
        return;
    double bg[3], fg[3], ac[3];
    simple_rgb(0, bg);
    simple_rgb(1, fg);
    simple_rgb(2, ac);
    derive(&T.cur, bg, fg, ac);
    editor_buttons_from_cur();
    preview();
    gtk_label_set_text(GTK_LABEL(T.msg), "Previewing. \"Save and use\" keeps it.");
}

static void on_swap(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    double bg[3], fg[3];
    simple_rgb(0, bg);
    simple_rgb(1, fg);
    T.loading = true;
    simple_set(0, fg);
    T.loading = false;
    simple_set(1, bg);
}

/* the picture's colours: a dark or light common one for the background, the
 * most colourful common one for the accent */
static void from_picture(const char *path)
{
    GdkTexture *t = gdk_texture_new_from_filename(path, NULL);
    if (!t) {
        gtk_label_set_text(GTK_LABEL(T.msg), "That picture couldn't be read.");
        return;
    }
    int w = gdk_texture_get_width(t), h = gdk_texture_get_height(t);
    guint8 *px = g_malloc((gsize)w * h * 4);
    gdk_texture_download(t, px, w * 4);
    g_object_unref(t);
    enum { B = 16 };
    static double sum[B * B * B][3];
    static int cnt[B * B * B];
    memset(sum, 0, sizeof sum);
    memset(cnt, 0, sizeof cnt);
    int step = MAX(1, (int)sqrt((double)w * h / 40000));
    for (int y = 0; y < h; y += step)
        for (int x = 0; x < w; x += step) {
            const guint8 *p = px + ((gsize)y * w + x) * 4;    /* BGRA, premultiplied */
            if (p[3] < 128)
                continue;
            int k = (p[2] / 16 * B + p[1] / 16) * B + p[0] / 16;
            sum[k][0] += p[2] / 255.0;
            sum[k][1] += p[1] / 255.0;
            sum[k][2] += p[0] / 255.0;
            cnt[k]++;
        }
    g_free(px);
    int bgk = -1, ack = -1;
    double best_bg = -1, best_ac = -1, total = 0;
    for (int k = 0; k < B * B * B; k++)
        total += cnt[k];
    for (int k = 0; k < B * B * B; k++) {
        if (!cnt[k])
            continue;
        double c[3] = {sum[k][0] / cnt[k], sum[k][1] / cnt[k], sum[k][2] / cnt[k]};
        double mx = fmax(c[0], fmax(c[1], c[2])), mn = fmin(c[0], fmin(c[1], c[2])), l = luma(c);
        double share = cnt[k] / total;
        double sbg = share * (l < 0.25 ? 1.5 : l > 0.85 ? 1 : 0.2);
        double sac = sqrt(share) * (mx - mn) * (l > 0.2 && l < 0.85 ? 1 : 0.3);
        if (sbg > best_bg) { best_bg = sbg; bgk = k; }
        if (sac > best_ac) { best_ac = sac; ack = k; }
    }
    if (bgk < 0) {
        gtk_label_set_text(GTK_LABEL(T.msg), "No colours found in that picture.");
        return;
    }
    double bg[3] = {sum[bgk][0] / cnt[bgk], sum[bgk][1] / cnt[bgk], sum[bgk][2] / cnt[bgk]};
    double ac[3] = {sum[ack][0] / cnt[ack], sum[ack][1] / cnt[ack], sum[ack][2] / cnt[ack]};
    bool dark = luma(bg) < 0.5;
    /* keep backgrounds calm: pull them toward black or white */
    double end[3] = {dark ? 0 : 1, dark ? 0 : 1, dark ? 0 : 1};
    for (int i = 0; i < 3; i++)
        bg[i] = bg[i] + (end[i] - bg[i]) * 0.55;
    double fg[3] = {dark ? 0.9 : 0.12, dark ? 0.91 : 0.12, dark ? 0.92 : 0.13};
    for (int i = 0; i < 3; i++)
        fg[i] = fg[i] * 0.9 + ac[i] * 0.1;
    T.loading = true;
    simple_set(0, bg);
    simple_set(1, fg);
    T.loading = false;
    simple_set(2, ac);
    char *base = g_path_get_basename(path), *dot = strrchr(base, '.');
    if (g_str_has_prefix(base, "wallpaper")) {          /* ~/.config/themes/<name>/wallpaper.png */
        char *dir = g_path_get_dirname(path);
        g_free(base);
        base = g_path_get_basename(dir);
        dot = NULL;
        g_free(dir);
    }
    if (dot)
        *dot = 0;
    gtk_editable_set_text(GTK_EDITABLE(T.name), base);
    g_free(base);
    gtk_label_set_text(GTK_LABEL(T.msg), "Colours taken from the picture. Change any of the three, then \"Save and use\".");
}

static void on_picture_done(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f)
        return;
    char *p = g_file_get_path(f);
    from_picture(p);
    g_free(p);
    g_object_unref(f);
}

static void on_picture(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Take colours from a picture");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Pictures");
    gtk_file_filter_add_mime_type(ff, "image/png");
    gtk_file_filter_add_mime_type(ff, "image/jpeg");
    gtk_file_filter_add_mime_type(ff, "image/webp");
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(fl));
    const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    if (pics) {
        GFile *pf = g_file_new_for_path(pics);
        gtk_file_dialog_set_initial_folder(d, pf);
        g_object_unref(pf);
    }
    gtk_file_dialog_open(d, T.a->win, NULL, on_picture_done, NULL);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(d);
}

static void on_save(GtkButton *b, gpointer data);

static void on_save_use(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    const char *n = gtk_editable_get_text(GTK_EDITABLE(T.name));
    if (!*n || !g_ascii_strcasecmp(n, "Black") || !g_ascii_strcasecmp(n, "Paper"))
        gtk_editable_set_text(GTK_EDITABLE(T.name), "My theme");
    remember_prev();
    on_save(NULL, NULL);
    on_apply(NULL, NULL);
    mark_active(gtk_editable_get_text(GTK_EDITABLE(T.name)));
    gtk_label_set_text(GTK_LABEL(T.msg), "Saved with your presets and in use.");
}

static void write_prism(const char *name, char **note)
{
    if (mc_prism_running()) {           /* it would write its own theme back on exit */
        *note = g_strdup(" Prism is open, so its theme stays as it was.");
        return;
    }
    char *dir = g_build_filename(T.a->prism, "themes", "tux", NULL);
    g_mkdir_with_parents(dir, 0755);
    char *j = render(PRISM_THEME_JSON_TEMPLATE, &T.cur, name), *c = render(PRISM_THEME_CSS_TEMPLATE, &T.cur, name);
    char *jp = g_build_filename(dir, "theme.json", NULL), *cp = g_build_filename(dir, "themeStyle.css", NULL);
    bool ok = g_file_set_contents(jp, j, -1, NULL) && g_file_set_contents(cp, c, -1, NULL);
    *note = g_strdup(ok ? " Prism has it too." : " Prism's theme couldn't be written.");
    g_free(j);
    g_free(c);
    g_free(jp);
    g_free(cp);
    g_free(dir);
}

static void on_apply(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    char *dir = g_build_filename(g_get_user_config_dir(), "tux-mc", NULL);
    g_mkdir_with_parents(dir, 0755);
    char *path = g_build_filename(dir, "style.css", NULL);
    char *css = render(TUX_STYLE_TEMPLATE, &T.cur, "tux");
    bool ok = g_file_set_contents(path, css, -1, NULL);
    /* the same colours for the mod's right-shift menu (Theme.java reads this) */
    GString *pal = g_string_new("# written by tux-mc (Settings, Themes): the in-game menu's colours\n");
    char *qn = g_strescape(gtk_editable_get_text(GTK_EDITABLE(T.name)), NULL);
    g_string_append_printf(pal, "name=\"%s\"\n", qn);
    g_free(qn);
    for (int k = 0; k < NKEYS; k++)
        g_string_append_printf(pal, "%s=\"%s\"\n", KEYS[k].key, T.cur.col[k]);
    char *palp = g_build_filename(dir, "theme.sh", NULL);
    if (ok)
        g_file_set_contents(palp, pal->str, -1, NULL);
    g_free(palp);
    g_string_free(pal, TRUE);
    char *note = NULL;
    if (ok)
        write_prism(gtk_editable_get_text(GTK_EDITABLE(T.name)), &note);
    char *m = g_strdup_printf(ok ? "Applied, in game too (the right-shift menu).%s" : "Couldn't write %s",
                              ok ? (note ? note : "") : path);
    gtk_label_set_text(GTK_LABEL(T.msg), m);
    mc_cfg_set_str("theme_preset", gtk_editable_get_text(GTK_EDITABLE(T.name)));
    if (ok) {
        T.applied = T.cur;
        g_strlcpy(T.applied_name, gtk_editable_get_text(GTK_EDITABLE(T.name)), sizeof T.applied_name);
    }
    g_free(m);
    g_free(note);
    g_free(css);
    g_free(path);
    g_free(dir);
}

static void on_save(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(T.name));
    if (!*name) {
        gtk_label_set_text(GTK_LABEL(T.msg), "Give it a name first");
        return;
    }
    GString *f = g_string_new(NULL);
    for (const char *s = name; *s; s++)
        g_string_append_c(f, g_ascii_isalnum(*s) ? g_ascii_tolower(*s) : '-');
    g_string_append(f, ".sh");
    char *dir = presets_dir();
    g_mkdir_with_parents(dir, 0755);
    char *path = g_build_filename(dir, f->str, NULL);
    GString *out = g_string_new("# tux-mc theme preset\n");
    char *qn = g_strescape(name, NULL);
    g_string_append_printf(out, "name=\"%s\"\n", qn);
    g_free(qn);
    for (int k = 0; k < NKEYS; k++)
        g_string_append_printf(out, "%s=\"%s\"\n", KEYS[k].key, T.cur.col[k]);
    bool ok = g_file_set_contents(path, out->str, -1, NULL);
    gtk_label_set_text(GTK_LABEL(T.msg), ok ? "Saved to your presets." : "Couldn't save it.");
    g_string_free(out, TRUE);
    g_string_free(f, TRUE);
    g_free(path);
    g_free(dir);
    if (ok)
        fill();
}

static void on_delete(GtkButton *b, gpointer data)
{
    (void)data;
    const char *file = g_object_get_data(G_OBJECT(b), "file");
    if (file && g_remove(file) == 0) {
        gtk_label_set_text(GTK_LABEL(T.msg), "Deleted. The look stays until you pick another.");
        fill();
    }
}

static void on_revert(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    char *p = g_build_filename(g_get_user_config_dir(), "tux-mc", "style.css", NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS))
        gtk_css_provider_load_from_path(T.a->css, p);
    else
        gtk_css_provider_load_from_string(T.a->css, "");
    g_free(p);
    gtk_label_set_text(GTK_LABEL(T.msg), "Back to the applied theme.");
}

static void fill(void)
{
    if (T.presets)
        g_ptr_array_unref(T.presets);
    T.presets = load_all();
    gtk_flow_box_remove_all(GTK_FLOW_BOX(T.flow));
    char *active = mc_cfg_str("theme_preset", "");
    for (guint i = 0; i < T.presets->len; i++) {
        preset_t *p = T.presets->pdata[i];
        GtkWidget *b = gtk_button_new();
        gtk_widget_add_css_class(b, "mcl-preset");
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *sw = gtk_drawing_area_new();
        gtk_widget_set_size_request(sw, 150, 92);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(sw), draw_swatch, p, NULL);
        gtk_box_append(GTK_BOX(box), sw);
        GtkWidget *l = mc_label(p->name, NULL);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(l), 16);
        gtk_box_append(GTK_BOX(box), l);
        gtk_button_set_child(GTK_BUTTON(b), box);
        g_object_set_data(G_OBJECT(b), "preset", p);
        g_signal_connect(b, "clicked", G_CALLBACK(on_pick), NULL);
        if (!strcmp(active, p->name))
            gtk_widget_add_css_class(b, "active");
        gtk_flow_box_append(GTK_FLOW_BOX(T.flow), b);
    }
    g_free(active);
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    T.flow = NULL;
}

GtkWidget *mc_themes_card(app_t *a)
{
    T.a = a;
    GtkWidget *card = mc_card("Themes", "Click one and it's used straight away, here, in Prism and in the "
                                        "game's right-shift menu.");
    g_signal_connect(card, "destroy", G_CALLBACK(on_destroy), NULL);
    T.flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(T.flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(T.flow), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(T.flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(T.flow), 8);
    gtk_box_append(GTK_BOX(card), T.flow);
    GtkWidget *ur = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    T.undo = gtk_button_new_with_label("Undo");
    gtk_widget_set_sensitive(T.undo, FALSE);
    g_signal_connect(T.undo, "clicked", G_CALLBACK(on_undo_pick), NULL);
    gtk_box_append(GTK_BOX(ur), T.undo);
    T.msg = mc_label("", "dim");
    gtk_label_set_xalign(GTK_LABEL(T.msg), 0);
    gtk_label_set_wrap(GTK_LABEL(T.msg), TRUE);
    gtk_box_append(GTK_BOX(ur), T.msg);
    gtk_box_append(GTK_BOX(card), ur);

    /* make your own from three colours */
    gtk_box_append(GTK_BOX(card), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(card), mc_label("Make your own", "mcl-h2"));
    GtkWidget *hint = mc_label("Pick three colours, or take them from a picture (your wallpaper works well). "
                               "Everything else is worked out from these.", "dim");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_append(GTK_BOX(card), hint);
    GtkWidget *sr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    static const char *const SL[3] = {"Background", "Text", "Accent"};
    for (int i = 0; i < 3; i++) {
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_append(GTK_BOX(col), mc_label(SL[i], "dim"));
        T.simple[i] = mc_colour_button_new(false);
        gtk_widget_set_size_request(T.simple[i], 110, -1);
        mc_colour_on_change(T.simple[i], on_simple, NULL);
        gtk_box_append(GTK_BOX(col), T.simple[i]);
        gtk_box_append(GTK_BOX(sr), col);
    }
    GtkWidget *swap = gtk_button_new_with_label("Light / dark");
    gtk_widget_set_tooltip_text(swap, "swap background and text");
    gtk_widget_set_valign(swap, GTK_ALIGN_END);
    g_signal_connect(swap, "clicked", G_CALLBACK(on_swap), NULL);
    gtk_box_append(GTK_BOX(sr), swap);
    GtkWidget *pic = gtk_button_new_with_label("From a picture…");
    gtk_widget_set_valign(pic, GTK_ALIGN_END);
    g_signal_connect(pic, "clicked", G_CALLBACK(on_picture), NULL);
    gtk_box_append(GTK_BOX(sr), pic);
    gtk_box_append(GTK_BOX(card), sr);

    GtkWidget *nr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(nr), mc_label("Name", NULL));
    T.name = gtk_entry_new();
    gtk_widget_set_hexpand(T.name, TRUE);
    gtk_box_append(GTK_BOX(nr), T.name);
    GtkWidget *su = gtk_button_new_with_label("Save and use");
    gtk_widget_add_css_class(su, "suggested-action");
    g_signal_connect(su, "clicked", G_CALLBACK(on_save_use), NULL);
    gtk_box_append(GTK_BOX(nr), su);
    T.del = gtk_button_new_with_label("Delete preset");
    gtk_widget_add_css_class(T.del, "mcl-danger");
    g_signal_connect(T.del, "clicked", G_CALLBACK(on_delete), NULL);
    gtk_box_append(GTK_BOX(nr), T.del);
    gtk_box_append(GTK_BOX(card), nr);

    /* every colour, for people who want that */
    GtkWidget *exp = gtk_expander_new("Fine-tune every colour");
    gtk_widget_set_margin_top(exp, 6);
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(inner, 8);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    for (int k = 0; k < NKEYS; k++) {
        GtkWidget *l = mc_label(KEYS[k].label, "dim");
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_width_chars(GTK_LABEL(l), 14);
        if (k % 3)
            gtk_widget_set_margin_start(l, 28);
        T.btn[k] = mc_colour_button_new(false);
        mc_colour_on_change(T.btn[k], on_color, GINT_TO_POINTER(k));
        int col = (k % 3) * 2, row = k / 3;
        gtk_grid_attach(GTK_GRID(grid), l, col, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), T.btn[k], col + 1, row, 1, 1);
    }
    gtk_box_append(GTK_BOX(inner), grid);
    GtkWidget *br = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    T.apply = gtk_button_new_with_label("Use these");
    g_signal_connect(T.apply, "clicked", G_CALLBACK(on_apply), NULL);
    GtkWidget *rev = gtk_button_new_with_label("Undo preview");
    g_signal_connect(rev, "clicked", G_CALLBACK(on_revert), NULL);
    gtk_box_append(GTK_BOX(br), T.apply);
    gtk_box_append(GTK_BOX(br), rev);
    gtk_box_append(GTK_BOX(inner), br);
    gtk_expander_set_child(GTK_EXPANDER(exp), inner);
    gtk_box_append(GTK_BOX(card), exp);

    fill();
    const char *test = g_getenv("TUX_MC_PRESET");          /* screenshots: preview this one */
    for (guint i = 0; test && i < T.presets->len; i++)
        if (!strcmp(((preset_t *)T.presets->pdata[i])->name, test)) {
            editor_show(T.presets->pdata[i]);
            simple_from_cur();
            return card;
        }
    /* the editor starts on the applied preset, or Black; nothing is previewed yet */
    char *active = mc_cfg_str("theme_preset", "Black");
    preset_t *start = T.presets->pdata[0];
    for (guint i = 0; i < T.presets->len; i++)
        if (!strcmp(((preset_t *)T.presets->pdata[i])->name, active))
            start = T.presets->pdata[i];
    g_free(active);
    T.loading = true;
    T.cur = *start;
    T.cur.name = T.cur.file = NULL;
    gtk_editable_set_text(GTK_EDITABLE(T.name), start->name);
    for (int k = 0; k < NKEYS; k++) {
        GdkRGBA c;
        gdk_rgba_parse(&c, start->col[k]);
        mc_colour_set(T.btn[k], &c);
    }
    gtk_widget_set_visible(T.del, start->own);
    g_object_set_data_full(G_OBJECT(T.del), "file", g_strdup(start->file), g_free);
    T.loading = false;
    T.applied = T.cur;
    g_strlcpy(T.applied_name, start->name, sizeof T.applied_name);
    simple_from_cur();
    if (g_getenv("TUX_MC_THEME_PICTURE"))                  /* screenshots */
        from_picture(g_getenv("TUX_MC_THEME_PICTURE"));
    return card;
}
