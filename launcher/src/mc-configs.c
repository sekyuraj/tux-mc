/* mc-configs.c — tux-mc's Configs page.
 *
 * config/ grouped by mod: one tile per mod (its icon, its name), click it
 * for that mod's files and an editor with line numbers, colours for keys,
 * strings, numbers and comments, search (Ctrl+F) and save (Ctrl+S). Files
 * that belong to no installed mod land under "Other"; the game's own
 * options.txt is under "Minecraft". */
#include "mc.h"

#include <json-glib/json-glib.h>
#include <string.h>

typedef struct {
    char *title;
    const mc_mod_t *mod;                 /* NULL for Minecraft / Other */
    const char *icon_name;               /* when there's no mod icon */
    GPtrArray *files;                    /* full paths */
} group_t;

static GPtrArray *groups;                /* group_t */

static void group_free(group_t *g)
{
    g_free(g->title);
    g_ptr_array_unref(g->files);
    g_free(g);
}

static bool text_file(const char *n)
{
    static const char *ext[] = {".json", ".json5", ".toml", ".properties", ".cfg", ".txt", ".yml", ".yaml", ".conf",
                                ".ini", ".snbt"};
    for (size_t i = 0; i < G_N_ELEMENTS(ext); i++)
        if (g_str_has_suffix(n, ext[i]))
            return true;
    return false;
}

static void walk(const char *dir, GPtrArray *out)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        char *full = g_build_filename(dir, e, NULL);
        if (g_file_test(full, G_FILE_TEST_IS_DIR))
            walk(full, out);
        else if (text_file(e))
            g_ptr_array_add(out, g_strdup(full));
        g_free(full);
    }
    if (d)
        g_dir_close(d);
}

/* lower case letters and digits only: "Dynamic FPS" and "dynamic_fps" meet */
static char *norm(const char *s)
{
    GString *o = g_string_new(NULL);
    for (; s && *s; s++)
        if (g_ascii_isalnum(*s))
            g_string_append_c(o, g_ascii_tolower(*s));
    return g_string_free(o, FALSE);
}

/* the installed mod whose id or name the entry starts with (longest wins) */
static const mc_mod_t *owner(app_t *a, const char *entry)
{
    char *stem = g_strdup(entry), *dot = strchr(stem, '.');
    if (dot)
        *dot = 0;
    char *n = norm(stem);
    const mc_mod_t *best = NULL;
    size_t best_len = 0;
    for (guint i = 0; a->mods && i < a->mods->len; i++) {
        const mc_mod_t *m = a->mods->pdata[i];
        const char *cand[] = {m->id, m->name};
        for (int k = 0; k < 2; k++) {
            char *c = norm(cand[k]);
            size_t l = strlen(c);
            if (l >= 3 && l > best_len && g_str_has_prefix(n, c)) {
                best = m;
                best_len = l;
            }
            g_free(c);
        }
    }
    g_free(n);
    g_free(stem);
    return best;
}

static group_t *group_for(GHashTable *by, const char *key, const char *title, const mc_mod_t *mod, const char *icon)
{
    group_t *g = g_hash_table_lookup(by, key);
    if (!g) {
        g = g_new0(group_t, 1);
        g->title = g_strdup(title);
        g->mod = mod;
        g->icon_name = icon;
        g->files = g_ptr_array_new_with_free_func(g_free);
        g_hash_table_insert(by, g_strdup(key), g);
        g_ptr_array_add(groups, g);
    }
    return g;
}

static int group_cmp(gconstpointer x, gconstpointer y)
{
    const group_t *a = *(group_t *const *)x, *b = *(group_t *const *)y;
    bool sa = !a->mod, sb = !b->mod;     /* Minecraft first, Other last */
    if (sa || sb) {
        if (!strcmp(a->title, "Minecraft") || (sb && !strcmp(b->title, "Other")))
            return -1;
        if (!strcmp(b->title, "Minecraft") || (sa && !strcmp(a->title, "Other")))
            return 1;
    }
    return g_ascii_strcasecmp(a->title, b->title);
}

static void build_groups(app_t *a)
{
    if (groups)
        g_ptr_array_unref(groups);
    groups = g_ptr_array_new_with_free_func((GDestroyNotify)group_free);
    GHashTable *by = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    char *opt = g_build_filename(a->mcdir, "options.txt", NULL);
    if (g_file_test(opt, G_FILE_TEST_EXISTS))
        g_ptr_array_add(group_for(by, "\x01minecraft", "Minecraft", NULL, "applications-games-symbolic")->files,
                        g_strdup(opt));
    g_free(opt);

    char *root = g_build_filename(a->mcdir, "config", NULL);
    GDir *d = g_dir_open(root, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        char *full = g_build_filename(root, e, NULL);
        GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
        if (g_file_test(full, G_FILE_TEST_IS_DIR))
            walk(full, files);
        else if (text_file(e))
            g_ptr_array_add(files, g_strdup(full));
        if (files->len) {
            const mc_mod_t *m = owner(a, e);
            group_t *g = m ? group_for(by, m->id ? m->id : m->name, m->name, m, NULL)
                           : group_for(by, "\x02other", "Other", NULL, "folder-symbolic");
            for (guint i = 0; i < files->len; i++)
                g_ptr_array_add(g->files, g_strdup(files->pdata[i]));
        }
        g_ptr_array_unref(files);
        g_free(full);
    }
    if (d)
        g_dir_close(d);
    g_free(root);
    g_hash_table_unref(by);
    g_ptr_array_sort(groups, group_cmp);
    for (guint i = 0; i < groups->len; i++) {
        group_t *g = groups->pdata[i];
        g_ptr_array_sort_values(g->files, (GCompareFunc)g_ascii_strcasecmp);
    }
}

/* ------------------------------------------------------------ highlight -- */
static bool is_json(const char *p) { return p && (g_str_has_suffix(p, ".json") || g_str_has_suffix(p, ".json5")); }

static GtkTextBuffer *buf(app_t *a) { return gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->cfg_view)); }

static void make_tags(app_t *a)
{
    GtkTextBuffer *b = buf(a);
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(b);
    GdkRGBA acc, acc2, warn, dim, br;
    mc_pal(a, PAL_ACCENT, &acc);
    mc_pal(a, PAL_ACCENT2, &acc2);
    mc_pal(a, PAL_WARN, &warn);
    mc_pal(a, PAL_DIM, &dim);
    mc_pal(a, PAL_BRIGHT, &br);
    struct { const char *name; GdkRGBA *c; int weight; bool italic; } t[] = {
        {"key", &acc, 600, false},   {"string", &warn, 400, false}, {"number", &acc2, 400, false},
        {"const", &acc2, 600, false}, {"comment", &dim, 400, true},  {"section", &br, 700, false},
        {"found", NULL, 400, false},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(t); i++) {
        GtkTextTag *tag = gtk_text_tag_table_lookup(tt, t[i].name);
        if (!tag)
            tag = gtk_text_buffer_create_tag(b, t[i].name, NULL);
        if (t[i].c)
            g_object_set(tag, "foreground-rgba", t[i].c, "weight", t[i].weight, "style",
                         t[i].italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL, NULL);
        else {
            GdkRGBA bg = acc;
            bg.alpha = 0.35;
            g_object_set(tag, "background-rgba", &bg, NULL);
        }
    }
}

static void tag(GtkTextBuffer *b, const char *name, int line, int from, int to)
{
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_line_index(b, &s, line, from);
    gtk_text_buffer_get_iter_at_line_index(b, &e, line, to);
    gtk_text_buffer_apply_tag_by_name(b, name, &s, &e);
}

static bool is_num_start(const char *p) { return g_ascii_isdigit(*p) || ((*p == '-' || *p == '.') && g_ascii_isdigit(p[1])); }

/* values on one line: strings, numbers, true/false/null */
static void values(GtkTextBuffer *b, int line, const char *l, int from, int to, bool json)
{
    for (int i = from; i < to;) {
        char c = l[i];
        if (c == '"' || (c == '\'' && !json)) {
            int j = i + 1;
            while (j < to && l[j] != c)
                j += l[j] == '\\' ? 2 : 1;
            j = MIN(j + 1, to);
            int k = j;
            while (json && k < to && (l[k] == ' ' || l[k] == '\t'))
                k++;
            tag(b, json && k < to && l[k] == ':' ? "key" : "string", line, i, j);
            i = j;
        } else if (is_num_start(l + i) && (i == 0 || !g_ascii_isalnum(l[i - 1]))) {
            int j = i + 1;
            while (j < to && (g_ascii_isalnum(l[j]) || l[j] == '.' || l[j] == '_'))
                j++;
            tag(b, "number", line, i, j);
            i = j;
        } else if (g_ascii_isalpha(c) && (i == 0 || !g_ascii_isalnum(l[i - 1]))) {
            int j = i;
            while (j < to && (g_ascii_isalnum(l[j]) || l[j] == '_'))
                j++;
            char *w = g_strndup(l + i, j - i);
            if (!g_ascii_strcasecmp(w, "true") || !g_ascii_strcasecmp(w, "false") || !strcmp(w, "null"))
                tag(b, "const", line, i, j);
            g_free(w);
            i = j;
        } else {
            i++;
        }
    }
}

static void highlight(app_t *a)
{
    GtkTextBuffer *b = buf(a);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    const char *tags[] = {"key", "string", "number", "const", "comment", "section"};
    for (size_t i = 0; i < G_N_ELEMENTS(tags); i++)
        gtk_text_buffer_remove_tag_by_name(b, tags[i], &s, &e);
    if (!a->cfg_path)
        return;
    bool json = is_json(a->cfg_path) || g_str_has_suffix(a->cfg_path, ".snbt");
    char *txt = gtk_text_buffer_get_text(b, &s, &e, FALSE);
    char **lines = g_strsplit(txt, "\n", -1);
    bool in_block = false;               /* json5 / * ... * / */
    for (int n = 0; lines[n]; n++) {
        const char *l = lines[n];
        int len = strlen(l), i = 0;
        while (i < len && (l[i] == ' ' || l[i] == '\t'))
            i++;
        if (in_block) {
            const char *end = strstr(l, "*/");
            tag(b, "comment", n, 0, end ? (int)(end - l) + 2 : len);
            in_block = !end;
            continue;
        }
        if (l[i] == '#' || l[i] == ';' || (l[i] == '/' && l[i + 1] == '/') || (l[i] == '!' && !json)) {
            tag(b, "comment", n, i, len);
            continue;
        }
        if (l[i] == '/' && l[i + 1] == '*') {
            const char *end = strstr(l + i + 2, "*/");
            tag(b, "comment", n, i, end ? (int)(end - l) + 2 : len);
            in_block = !end;
            continue;
        }
        if (json) {
            values(b, n, l, i, len, true);
            continue;
        }
        if (l[i] == '[') {               /* [section] */
            tag(b, "section", n, i, len);
            continue;
        }
        /* key = value / key: value / key:value (options.txt) */
        int eq = -1;
        for (int j = i; j < len; j++)
            if (l[j] == '=' || l[j] == ':') {
                eq = j;
                break;
            }
        if (eq > i) {
            tag(b, "key", n, i, eq);
            values(b, n, l, eq + 1, len, false);
        } else {
            values(b, n, l, i, len, false);
        }
    }
    g_strfreev(lines);
    g_free(txt);
}

static guint hl_src;

static gboolean highlight_later(gpointer data)
{
    hl_src = 0;
    highlight(data);
    return G_SOURCE_REMOVE;
}

/* --------------------------------------------------------------- gutter -- */
static void draw_gutter(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)h;
    app_t *a = data;
    GtkTextView *tv = GTK_TEXT_VIEW(a->cfg_view);
    GdkRGBA dim, fg;
    mc_pal(a, PAL_DIM, &dim);
    mc_pal(a, PAL_FG, &fg);
    GdkRectangle vis;
    gtk_text_view_get_visible_rect(tv, &vis);
    GtkTextIter it, cur;
    gtk_text_buffer_get_iter_at_mark(buf(a), &cur, gtk_text_buffer_get_insert(buf(a)));
    gtk_text_view_get_line_at_y(tv, &it, vis.y, NULL);
    PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(da), NULL);
    PangoFontDescription *fd = pango_font_description_from_string("monospace 9");
    pango_layout_set_font_description(pl, fd);
    for (;;) {
        int y, lh;
        gtk_text_view_get_line_yrange(tv, &it, &y, &lh);
        if (y > vis.y + vis.height)
            break;
        int wx, wy;
        gtk_text_view_buffer_to_window_coords(tv, GTK_TEXT_WINDOW_LEFT, 0, y, &wx, &wy);
        char num[16];
        g_snprintf(num, sizeof num, "%d", gtk_text_iter_get_line(&it) + 1);
        pango_layout_set_text(pl, num, -1);
        int tw, th;
        pango_layout_get_pixel_size(pl, &tw, &th);
        GdkRGBA *c = gtk_text_iter_get_line(&it) == gtk_text_iter_get_line(&cur) ? &fg : &dim;
        cairo_set_source_rgba(cr, c->red, c->green, c->blue, c == &fg ? 0.9 : 0.6);
        cairo_move_to(cr, w - tw - 10, wy + (lh - th) / 2.0);
        pango_cairo_show_layout(cr, pl);
        if (!gtk_text_iter_forward_line(&it))
            break;
    }
    pango_font_description_free(fd);
    g_object_unref(pl);
}

static void gutter_width(app_t *a)
{
    int lines = gtk_text_buffer_get_line_count(buf(a)), digits = 1;
    while (lines >= 10) {
        lines /= 10;
        digits++;
    }
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a->cfg_gutter), 18 + 8 * MAX(digits, 2));
}

static void redraw_gutter(GtkAdjustment *adj, gpointer data) { (void)adj; gtk_widget_queue_draw(((app_t *)data)->cfg_gutter); }

/* ----------------------------------------------------------------- files -- */
static void update_info(app_t *a);

static void set_dirty(app_t *a, bool d)
{
    a->cfg_dirty = d;
    gtk_widget_set_sensitive(a->cfg_save, d);
    if (a->cfg_path)
        update_info(a);
}

static char *rel_name(app_t *a, const char *path);

static void update_info(app_t *a)
{
    char *rel = rel_name(a, a->cfg_path);
    int n = gtk_text_buffer_get_line_count(buf(a));
    char *info = g_strdup_printf("%s  ·  %d line%s%s", rel, n, n == 1 ? "" : "s", a->cfg_dirty ? "  ·  not saved" : "");
    gtk_label_set_text(GTK_LABEL(a->cfg_info), info);
    g_free(info);
    g_free(rel);
}

static bool file_open(app_t *a, const char *path)
{
    char *txt = NULL;
    gsize n;
    if (!g_file_get_contents(path, &txt, &n, NULL) || !g_utf8_validate(txt, n, NULL)) {
        mc_status(a, "Can't edit %s here (not a text file)", path);
        g_free(txt);
        return false;
    }
    g_free(a->cfg_path);
    a->cfg_path = g_strdup(path);
    a->cfg_loading = true;
    gtk_text_buffer_set_text(buf(a), txt, n);
    a->cfg_loading = false;
    GtkTextIter s;
    gtk_text_buffer_get_start_iter(buf(a), &s);
    gtk_text_buffer_place_cursor(buf(a), &s);
    g_free(txt);
    set_dirty(a, false);
    char *base = g_path_get_basename(path);
    gtk_label_set_text(GTK_LABEL(a->cfg_title), base);
    g_free(base);
    update_info(a);
    gutter_width(a);
    make_tags(a);                        /* the palette is only styled once the window is up */
    highlight(a);
    gtk_widget_set_visible(a->cfg_tidy, is_json(path));
    return true;
}

static void on_file_row(GtkListBox *l, GtkListBoxRow *row, gpointer data)
{
    (void)l;
    app_t *a = data;
    const char *path = g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(row)), "path");
    if (!g_strcmp0(path, a->cfg_path))
        return;
    if (a->cfg_dirty) {
        mc_status(a, "Unsaved changes: save (Ctrl+S) or revert them first");
        for (GtkWidget *c = gtk_widget_get_first_child(a->cfg_files); c; c = gtk_widget_get_next_sibling(c))
            if (!g_strcmp0(g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(c))), "path"),
                           a->cfg_path))
                gtk_list_box_select_row(GTK_LIST_BOX(a->cfg_files), GTK_LIST_BOX_ROW(c));
        return;
    }
    file_open(a, path);
}

/* "config/sodium-options.json" for the list, relative to the game folder */
static char *rel_name(app_t *a, const char *path)
{
    char *cfg = g_build_filename(a->mcdir, "config", NULL);
    const char *r = g_str_has_prefix(path, cfg) ? path + strlen(cfg) + 1
                    : g_str_has_prefix(path, a->mcdir) ? path + strlen(a->mcdir) + 1 : path;
    char *res = g_strdup(r);
    g_free(cfg);
    return res;
}

static void open_group(app_t *a, group_t *g)
{
    if (a->cfg_dirty) {
        mc_status(a, "Unsaved changes: save (Ctrl+S) or revert them first");
        return;
    }
    g_free(a->cfg_mod);
    a->cfg_mod = g_strdup(g->title);
    gtk_label_set_text(GTK_LABEL(a->cfg_mod_title), g->title);
    GdkTexture *tex = mc_mod_icon(g->mod);
    if (tex) {
        gtk_image_set_from_paintable(GTK_IMAGE(a->cfg_mod_icon), GDK_PAINTABLE(tex));
        g_object_unref(tex);
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(a->cfg_mod_icon), g->icon_name ? g->icon_name : "application-x-addon-symbolic");
    }
    gtk_list_box_remove_all(GTK_LIST_BOX(a->cfg_files));
    for (guint i = 0; i < g->files->len; i++) {
        char *rel = rel_name(a, g->files->pdata[i]);
        GtkWidget *l = mc_label(rel, "mcl-cfg");
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_tooltip_text(l, rel);
        g_object_set_data_full(G_OBJECT(l), "path", g_strdup(g->files->pdata[i]), g_free);
        gtk_list_box_append(GTK_LIST_BOX(a->cfg_files), l);
        g_free(rel);
    }
    gtk_widget_set_visible(g_object_get_data(G_OBJECT(a->cfg_files), "box"), g->files->len > 1);
    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(a->cfg_files), 0);
    if (first) {
        gtk_list_box_select_row(GTK_LIST_BOX(a->cfg_files), first);
        g_free(a->cfg_path);
        a->cfg_path = NULL;
        file_open(a, g->files->pdata[0]);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(a->cfg_stack), "files");
}

static void on_tile(GtkButton *b, gpointer data)
{
    open_group(data, g_object_get_data(G_OBJECT(b), "group"));
}

static void on_back(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    if (a->cfg_dirty) {
        mc_status(a, "Unsaved changes: save (Ctrl+S) or revert them first");
        return;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(a->cfg_stack), "grid");
}

static GtkWidget *tile(group_t *g)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "mcl-cfgtile");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *img;
    GdkTexture *tex = mc_mod_icon(g->mod);
    if (tex) {
        img = gtk_image_new_from_paintable(GDK_PAINTABLE(tex));
        g_object_unref(tex);
    } else {
        img = gtk_image_new_from_icon_name(g->icon_name ? g->icon_name : "application-x-addon-symbolic");
    }
    gtk_image_set_pixel_size(GTK_IMAGE(img), 48);
    gtk_widget_add_css_class(img, "mcl-modicon");
    gtk_box_append(GTK_BOX(box), img);
    GtkWidget *name = gtk_label_new(g->title);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(name), 14);
    gtk_box_append(GTK_BOX(box), name);
    char *n = g_strdup_printf(g->files->len == 1 ? "%u file" : "%u files", g->files->len);
    GtkWidget *cnt = gtk_label_new(n);
    gtk_widget_add_css_class(cnt, "dim");
    g_free(n);
    gtk_box_append(GTK_BOX(box), cnt);
    gtk_button_set_child(GTK_BUTTON(b), box);
    g_object_set_data(G_OBJECT(b), "group", g);
    char *s = g_utf8_strdown(g->title, -1);
    g_object_set_data_full(G_OBJECT(b), "search", s, g_free);
    return b;
}

static gboolean tile_filter(GtkFlowBoxChild *c, gpointer data)
{
    app_t *a = data;
    const char *q = gtk_editable_get_text(GTK_EDITABLE(a->cfg_search));
    if (!*q)
        return TRUE;
    char *ql = g_utf8_strdown(q, -1);
    GtkWidget *b = gtk_flow_box_child_get_child(c);
    group_t *g = g_object_get_data(G_OBJECT(b), "group");
    bool hit = strstr(g_object_get_data(G_OBJECT(b), "search"), ql) != NULL;
    for (guint i = 0; !hit && g && i < g->files->len; i++) {
        char *f = g_utf8_strdown(g->files->pdata[i], -1);
        hit = strstr(f, ql) != NULL;
        g_free(f);
    }
    g_free(ql);
    return hit;
}

static void on_tile_search(GtkSearchEntry *e, gpointer data)
{
    (void)e;
    gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(((app_t *)data)->cfg_grid));
}

void mc_configs_refresh(app_t *a)
{
    if (!a->cfg_grid)
        return;
    if (a->cfg_dirty)                    /* don't pull the rug out from an edit */
        return;
    build_groups(a);
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(a->cfg_grid)))
        gtk_flow_box_remove(GTK_FLOW_BOX(a->cfg_grid), c);
    for (guint i = 0; i < groups->len; i++) {
        GtkWidget *t = tile(groups->pdata[i]);
        g_signal_connect(t, "clicked", G_CALLBACK(on_tile), a);
        gtk_flow_box_append(GTK_FLOW_BOX(a->cfg_grid), t);
    }
    /* still looking at a mod: re-point at its new group */
    if (a->cfg_mod && !strcmp(gtk_stack_get_visible_child_name(GTK_STACK(a->cfg_stack)), "files"))
        for (guint i = 0; i < groups->len; i++)
            if (!strcmp(((group_t *)groups->pdata[i])->title, a->cfg_mod))
                open_group(a, groups->pdata[i]);
}

/* --------------------------------------------------------------- editing -- */
static void on_changed(GtkTextBuffer *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    gtk_widget_queue_draw(a->cfg_gutter);
    if (!a->cfg_path || a->cfg_loading)
        return;
    set_dirty(a, true);
    gutter_width(a);
    if (hl_src)
        g_source_remove(hl_src);
    hl_src = g_timeout_add(180, highlight_later, a);
}

static void on_cursor(GObject *o, GParamSpec *ps, gpointer data)
{
    (void)o;
    (void)ps;
    gtk_widget_queue_draw(((app_t *)data)->cfg_gutter);
}

static void on_save(GtkButton *bt, gpointer data)
{
    (void)bt;
    app_t *a = data;
    if (!a->cfg_path || !a->cfg_dirty)
        return;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf(a), &s, &e);
    char *txt = gtk_text_buffer_get_text(buf(a), &s, &e, FALSE);
    GError *err = NULL;
    if (g_file_set_contents(a->cfg_path, txt, -1, &err)) {
        set_dirty(a, false);
        mc_status(a, mc_game_running(a) ? "Saved. Some mods only read it at the next start."
                                        : "Saved. The game uses it next time it starts.");
    } else {
        mc_status(a, "Couldn't save: %s", err->message);
        g_error_free(err);
    }
    g_free(txt);
}

static void on_revert(GtkButton *bt, gpointer data)
{
    (void)bt;
    app_t *a = data;
    if (!a->cfg_path)
        return;
    char *p = g_strdup(a->cfg_path);
    file_open(a, p);
    g_free(p);
    mc_status(a, "Back to what's on disk");
}

/* JSON on one line (sodium-fingerprint & co): spread it out, one key per line */
static void on_tidy(GtkButton *bt, gpointer data)
{
    (void)bt;
    app_t *a = data;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buf(a), &s, &e);
    char *txt = gtk_text_buffer_get_text(buf(a), &s, &e, FALSE);
    JsonParser *p = json_parser_new();
    GError *err = NULL;
    if (json_parser_load_from_data(p, txt, -1, &err)) {
        JsonGenerator *g = json_generator_new();
        json_generator_set_root(g, json_parser_get_root(p));
        json_generator_set_pretty(g, TRUE);
        json_generator_set_indent(g, 2);
        char *out = json_generator_to_data(g, NULL);
        gtk_text_buffer_set_text(buf(a), out, -1);
        g_free(out);
        g_object_unref(g);
        mc_status(a, "Tidied. Save to keep it that way.");
    } else {
        mc_status(a, "Not plain JSON (%s), left as it is", err->message);
        g_error_free(err);
    }
    g_object_unref(p);
    g_free(txt);
}

static void on_open_file(GtkButton *bt, gpointer data)
{
    (void)bt;
    app_t *a = data;
    if (a->cfg_path)
        mc_open_path(a, a->cfg_path);
}

static void on_folder(GtkButton *bt, gpointer data)
{
    (void)bt;
    app_t *a = data;
    char *d = a->cfg_path ? g_path_get_dirname(a->cfg_path) : g_build_filename(a->mcdir, "config", NULL);
    mc_open_path(a, d);
    g_free(d);
}

/* search: highlight every hit, jump to the next one */
static void find(app_t *a, bool next)
{
    GtkTextBuffer *b = buf(a);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    gtk_text_buffer_remove_tag_by_name(b, "found", &s, &e);
    const char *q = gtk_editable_get_text(GTK_EDITABLE(a->cfg_searchbar));
    if (!*q)
        return;
    GtkTextIter it = s, ms, me;
    int hits = 0;
    while (gtk_text_iter_forward_search(&it, q, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &ms, &me, NULL)) {
        gtk_text_buffer_apply_tag_by_name(b, "found", &ms, &me);
        it = me;
        hits++;
    }
    GtkTextIter from;
    gtk_text_buffer_get_iter_at_mark(b, &from, gtk_text_buffer_get_insert(b));
    if (next)
        gtk_text_iter_forward_char(&from);
    if (gtk_text_iter_forward_search(&from, q, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &ms, &me, NULL) ||
        gtk_text_iter_forward_search(&s, q, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &ms, &me, NULL)) {
        gtk_text_buffer_select_range(b, &ms, &me);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(a->cfg_view), &ms, 0.1, FALSE, 0, 0.3);
    }
    mc_status(a, hits ? "%d match%s" : "Not in this file", hits, hits == 1 ? "" : "es");
}

static void on_find_changed(GtkSearchEntry *e, gpointer data) { (void)e; find(data, false); }
static void on_find_next(GtkSearchEntry *e, gpointer data) { (void)e; find(data, true); }

static gboolean sc_save(GtkWidget *w, GVariant *v, gpointer data) { (void)w; (void)v; on_save(NULL, data); return TRUE; }

static gboolean sc_find(GtkWidget *w, GVariant *v, gpointer data)
{
    (void)w;
    (void)v;
    app_t *a = data;
    if (!strcmp(gtk_stack_get_visible_child_name(GTK_STACK(a->cfg_stack)), "files"))
        gtk_widget_grab_focus(a->cfg_searchbar);
    else
        gtk_widget_grab_focus(a->cfg_search);
    return TRUE;
}

static GtkWidget *scrolled(GtkWidget *child, bool hscroll)
{
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), hscroll ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), child);
    gtk_widget_set_vexpand(sc, TRUE);
    return sc;
}

static GtkWidget *button(const char *label, const char *tip, GCallback cb, app_t *a)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    if (tip)
        gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, a);
    return b;
}

GtkWidget *mc_page_configs(app_t *a)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(page, "mcl-page");
    a->cfg_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a->cfg_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(a->cfg_stack), 260);
    gtk_widget_set_vexpand(a->cfg_stack, TRUE);

    /* all mods */
    GtkWidget *grid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *t = mc_label("Configs", "mcl-title");
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(head), t);
    a->cfg_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(a->cfg_search), "find a mod or file");
    gtk_widget_set_size_request(a->cfg_search, 280, -1);
    gtk_widget_set_valign(a->cfg_search, GTK_ALIGN_CENTER);
    g_signal_connect(a->cfg_search, "search-changed", G_CALLBACK(on_tile_search), a);
    gtk_box_append(GTK_BOX(head), a->cfg_search);
    GtkWidget *fold = button("Open folder", "the config folder in your file manager", G_CALLBACK(on_folder), a);
    gtk_widget_set_valign(fold, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(head), fold);
    gtk_box_append(GTK_BOX(grid), head);
    gtk_box_append(GTK_BOX(grid), mc_label("Pick a mod to see and edit its settings files.", "dim"));
    a->cfg_grid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(a->cfg_grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(a->cfg_grid), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(a->cfg_grid), 10);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(a->cfg_grid), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(a->cfg_grid), 10);
    gtk_flow_box_set_filter_func(GTK_FLOW_BOX(a->cfg_grid), tile_filter, a, NULL);
    gtk_widget_set_valign(a->cfg_grid, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(grid), scrolled(a->cfg_grid, false));
    gtk_stack_add_named(GTK_STACK(a->cfg_stack), grid, "grid");

    /* one mod */
    GtkWidget *files = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *fh = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *back = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(back, "All mods");
    gtk_widget_set_valign(back, GTK_ALIGN_CENTER);
    g_signal_connect(back, "clicked", G_CALLBACK(on_back), a);
    gtk_box_append(GTK_BOX(fh), back);
    a->cfg_mod_icon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(a->cfg_mod_icon), 40);
    gtk_widget_add_css_class(a->cfg_mod_icon, "mcl-modicon");
    gtk_box_append(GTK_BOX(fh), a->cfg_mod_icon);
    a->cfg_mod_title = mc_label("", "mcl-title");
    gtk_widget_set_hexpand(a->cfg_mod_title, TRUE);
    gtk_box_append(GTK_BOX(fh), a->cfg_mod_title);
    gtk_box_append(GTK_BOX(files), fh);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_vexpand(body, TRUE);
    a->cfg_files = gtk_list_box_new();
    gtk_widget_add_css_class(a->cfg_files, "mcl-list");
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(a->cfg_files), TRUE);
    g_signal_connect(a->cfg_files, "row-activated", G_CALLBACK(on_file_row), a);
    GtkWidget *fl = scrolled(a->cfg_files, false);
    gtk_widget_set_size_request(fl, 280, -1);
    GtkWidget *flbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(flbox), fl);
    g_object_set_data(G_OBJECT(a->cfg_files), "box", flbox);
    gtk_box_append(GTK_BOX(body), flbox);

    GtkWidget *ed = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(ed, "mcl-panel");
    gtk_widget_set_hexpand(ed, TRUE);
    GtkWidget *eh = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *tt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(tt, TRUE);
    a->cfg_title = mc_label("", "mcl-h2");
    gtk_box_append(GTK_BOX(tt), a->cfg_title);
    a->cfg_info = mc_label("", "dim");
    gtk_box_append(GTK_BOX(tt), a->cfg_info);
    gtk_box_append(GTK_BOX(eh), tt);
    a->cfg_searchbar = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(a->cfg_searchbar), "search (Ctrl+F)");
    gtk_widget_set_size_request(a->cfg_searchbar, 200, -1);
    gtk_widget_set_valign(a->cfg_searchbar, GTK_ALIGN_CENTER);
    g_signal_connect(a->cfg_searchbar, "search-changed", G_CALLBACK(on_find_changed), a);
    g_signal_connect(a->cfg_searchbar, "activate", G_CALLBACK(on_find_next), a);
    gtk_box_append(GTK_BOX(eh), a->cfg_searchbar);
    a->cfg_tidy = button("Tidy", "lay the JSON out one key per line", G_CALLBACK(on_tidy), a);
    gtk_widget_set_valign(a->cfg_tidy, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(eh), a->cfg_tidy);
    GtkWidget *bt[] = {
        button("Revert", "throw away changes since the last save", G_CALLBACK(on_revert), a),
        button("Text editor", "open this file in your own editor", G_CALLBACK(on_open_file), a),
        button("Folder", "show the file in your file manager", G_CALLBACK(on_folder), a),
    };
    for (size_t i = 0; i < G_N_ELEMENTS(bt); i++) {
        gtk_widget_set_valign(bt[i], GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(eh), bt[i]);
    }
    a->cfg_save = button("Save", "Ctrl+S", G_CALLBACK(on_save), a);
    gtk_widget_add_css_class(a->cfg_save, "accent");
    gtk_widget_set_valign(a->cfg_save, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(a->cfg_save, FALSE);
    gtk_box_append(GTK_BOX(eh), a->cfg_save);
    gtk_box_append(GTK_BOX(ed), eh);

    a->cfg_view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(a->cfg_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(a->cfg_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(a->cfg_view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(a->cfg_view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(a->cfg_view), 40);
    gtk_widget_add_css_class(a->cfg_view, "mcl-editor");
    a->cfg_gutter = gtk_drawing_area_new();
    gtk_widget_add_css_class(a->cfg_gutter, "mcl-gutter");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a->cfg_gutter), draw_gutter, a, NULL);
    gtk_text_view_set_gutter(GTK_TEXT_VIEW(a->cfg_view), GTK_TEXT_WINDOW_LEFT, a->cfg_gutter);
    g_signal_connect(buf(a), "changed", G_CALLBACK(on_changed), a);
    g_signal_connect(buf(a), "notify::cursor-position", G_CALLBACK(on_cursor), a);
    GtkWidget *es = scrolled(a->cfg_view, true);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(es)), "value-changed",
                     G_CALLBACK(redraw_gutter), a);
    gtk_box_append(GTK_BOX(ed), es);
    gtk_box_append(GTK_BOX(body), ed);
    gtk_box_append(GTK_BOX(files), body);
    gtk_stack_add_named(GTK_STACK(a->cfg_stack), files, "files");
    gtk_box_append(GTK_BOX(page), a->cfg_stack);

    GtkEventController *keys = gtk_shortcut_controller_new();
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(keys),
        gtk_shortcut_new(gtk_shortcut_trigger_parse_string("<Control>s"), gtk_callback_action_new(sc_save, a, NULL)));
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(keys),
        gtk_shortcut_new(gtk_shortcut_trigger_parse_string("<Control>f"), gtk_callback_action_new(sc_find, a, NULL)));
    gtk_widget_add_controller(page, keys);
    mc_configs_refresh(a);
    return page;
}
