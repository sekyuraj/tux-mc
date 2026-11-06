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
    GtkWidget *flow, *name, *btn[NKEYS], *apply, *del, *msg;
    preset_t cur;                        /* what the editor shows */
    bool loading;
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
    for (int d = 0; d < 2; d++) {
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
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(T.btn[k]), &c);
    }
    gtk_widget_set_visible(T.del, p->own);
    g_object_set_data_full(G_OBJECT(T.del), "file", g_strdup(p->file), g_free);
    T.loading = false;
    preview();
}

static void on_color(GObject *b, GParamSpec *ps, gpointer data)
{
    (void)ps;
    if (T.loading)
        return;
    int k = GPOINTER_TO_INT(data);
    const GdkRGBA *c = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(b));
    g_snprintf(T.cur.col[k], 8, "#%02x%02x%02x", (int)(c->red * 255 + 0.5), (int)(c->green * 255 + 0.5),
               (int)(c->blue * 255 + 0.5));
    preview();
}

static void draw_swatch(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    const preset_t *p = data;
    static const int show[] = {0, 2, 7, 9};          /* bg, raised, text, accent */
    for (int i = 0; i < 4; i++) {
        GdkRGBA c;
        gdk_rgba_parse(&c, p->col[show[i]]);
        gdk_cairo_set_source_rgba(cr, &c);
        cairo_rectangle(cr, w * i / 4.0, 0, w / 4.0 + 1, h);
        cairo_fill(cr);
    }
}

static void fill(void);

static void on_pick(GtkButton *b, gpointer data)
{
    (void)data;
    for (GtkWidget *c = gtk_widget_get_first_child(T.flow); c; c = gtk_widget_get_next_sibling(c))
        gtk_widget_remove_css_class(gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(c)), "active");
    gtk_widget_add_css_class(GTK_WIDGET(b), "active");
    editor_show(g_object_get_data(G_OBJECT(b), "preset"));
    gtk_label_set_text(GTK_LABEL(T.msg), "Previewing. Apply keeps it.");
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
        gtk_widget_set_size_request(sw, 120, 26);
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
    GtkWidget *card = mc_card("Theme presets",
                              "Pick one to see it right away, change any colour, then Apply. Save keeps it as "
                              "your own preset.");
    g_signal_connect(card, "destroy", G_CALLBACK(on_destroy), NULL);
    T.flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(T.flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(T.flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(T.flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(T.flow), 8);
    gtk_box_append(GTK_BOX(card), T.flow);

    GtkWidget *nr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(nr, 10);
    gtk_box_append(GTK_BOX(nr), mc_label("Name", NULL));
    T.name = gtk_entry_new();
    gtk_widget_set_hexpand(T.name, TRUE);
    gtk_box_append(GTK_BOX(nr), T.name);
    gtk_box_append(GTK_BOX(card), nr);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    GtkColorDialog *cd = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(cd, FALSE);
    for (int k = 0; k < NKEYS; k++) {
        GtkWidget *l = mc_label(KEYS[k].label, "dim");
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_width_chars(GTK_LABEL(l), 14);
        if (k % 3)
            gtk_widget_set_margin_start(l, 28);
        T.btn[k] = gtk_color_dialog_button_new(g_object_ref(cd));
        g_signal_connect(T.btn[k], "notify::rgba", G_CALLBACK(on_color), GINT_TO_POINTER(k));
        int col = (k % 3) * 2, row = k / 3;
        gtk_grid_attach(GTK_GRID(grid), l, col, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), T.btn[k], col + 1, row, 1, 1);
    }
    g_object_unref(cd);
    gtk_box_append(GTK_BOX(card), grid);

    GtkWidget *br = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(br, 8);
    T.apply = gtk_button_new_with_label("Apply");
    g_signal_connect(T.apply, "clicked", G_CALLBACK(on_apply), NULL);
    GtkWidget *save = gtk_button_new_with_label("Save as preset");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save), NULL);
    GtkWidget *rev = gtk_button_new_with_label("Undo preview");
    g_signal_connect(rev, "clicked", G_CALLBACK(on_revert), NULL);
    T.del = gtk_button_new_with_label("Delete preset");
    gtk_widget_add_css_class(T.del, "mcl-danger");
    g_signal_connect(T.del, "clicked", G_CALLBACK(on_delete), NULL);
    gtk_box_append(GTK_BOX(br), T.apply);
    gtk_box_append(GTK_BOX(br), save);
    gtk_box_append(GTK_BOX(br), rev);
    gtk_box_append(GTK_BOX(br), T.del);
    gtk_box_append(GTK_BOX(card), br);
    T.msg = mc_label("", "dim");
    gtk_label_set_xalign(GTK_LABEL(T.msg), 0);
    gtk_box_append(GTK_BOX(card), T.msg);

    fill();
    const char *test = g_getenv("TUX_MC_PRESET");          /* screenshots: preview this one */
    for (guint i = 0; test && i < T.presets->len; i++)
        if (!strcmp(((preset_t *)T.presets->pdata[i])->name, test)) {
            editor_show(T.presets->pdata[i]);
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
        gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(T.btn[k]), &c);
    }
    gtk_widget_set_visible(T.del, start->own);
    g_object_set_data_full(G_OBJECT(T.del), "file", g_strdup(start->file), g_free);
    T.loading = false;
    return card;
}
