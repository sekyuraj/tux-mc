/* mc-pages.c — tux-mc's Skins, Mods, Configs and Settings pages. */
#include "mc.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================ settings.ini */
static GKeyFile *kf;
static char *kf_path;

static void kf_load(void)
{
    if (kf)
        return;
    kf = g_key_file_new();
    kf_path = g_build_filename(g_get_user_config_dir(), "tux-mc", "settings.ini", NULL);
    g_key_file_load_from_file(kf, kf_path, G_KEY_FILE_KEEP_COMMENTS, NULL);
}

static void kf_save(void)
{
    char *dir = g_path_get_dirname(kf_path);
    g_mkdir_with_parents(dir, 0755);
    g_key_file_save_to_file(kf, kf_path, NULL);
    g_free(dir);
}

bool mc_cfg_bool(const char *key, bool def)
{
    kf_load();
    GError *e = NULL;
    bool v = g_key_file_get_boolean(kf, "launcher", key, &e);
    if (e) {
        g_error_free(e);
        return def;
    }
    return v;
}

int mc_cfg_int(const char *key, int def)
{
    kf_load();
    GError *e = NULL;
    int v = g_key_file_get_integer(kf, "launcher", key, &e);
    if (e) {
        g_error_free(e);
        return def;
    }
    return v;
}

void mc_cfg_set_bool(const char *key, bool v) { kf_load(); g_key_file_set_boolean(kf, "launcher", key, v); kf_save(); }
void mc_cfg_set_int(const char *key, int v) { kf_load(); g_key_file_set_integer(kf, "launcher", key, v); kf_save(); }

char *mc_cfg_str(const char *key, const char *def)
{
    kf_load();
    char *v = g_key_file_get_string(kf, "launcher", key, NULL);
    return v ? v : g_strdup(def);
}

void mc_cfg_set_str(const char *key, const char *v) { kf_load(); g_key_file_set_string(kf, "launcher", key, v); kf_save(); }

/* ================================================================ skins */
static char *skin_dir(void)
{
    char *d = g_build_filename(g_get_user_data_dir(), "tux-mc", "skins", NULL);
    g_mkdir_with_parents(d, 0755);
    return d;
}

static void draw_skin_stage(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    app_t *a = data;
    GdkRGBA bg;
    mc_pal(a, PAL_BG, &bg);
    double sh[3] = {bg.red * 0.5, bg.green * 0.5, bg.blue * 0.5};
    mc3d_draw(&a->skin_view, cr, w, h, sh);
}

/* a skin's face (+ hat) as a thumbnail */
static void draw_head(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    cairo_surface_t *s = data;
    double sc = MIN(w, h) / 8.0;
    double x = (w - 8 * sc) / 2, y = (h - 8 * sc) / 2;
    for (int layer = 0; layer < 2; layer++) {
        cairo_save(cr);
        cairo_rectangle(cr, x, y, 8 * sc, 8 * sc);
        cairo_clip(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, s, layer ? -40 : -8, -8);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

static void pick_skin(app_t *a, cairo_surface_t *s, const char *path, bool slim)
{
    slim |= mc3d_skin_is_slim(s);          /* the picture knows better than a file name */
    if (a->skin_pick && a->skin_pick != a->skin)
        cairo_surface_destroy(a->skin_pick);
    a->skin_pick = s == a->skin ? s : cairo_surface_reference(s);
    g_free(a->skin_pick_path);
    a->skin_pick_path = g_strdup(path);
    a->skin_view.skin = a->skin_pick;
    a->skin_view.slim = slim;
    a->skin_view.yaw_vel = 7;
    a->skin_view.idle = 0;
    gtk_switch_set_active(GTK_SWITCH(a->skin_slim), slim);
    gtk_widget_set_sensitive(a->skin_apply, path != NULL);
    gtk_widget_set_sensitive(a->skin_remove, path != NULL);
}

static void on_skin_tile(GtkButton *b, gpointer data)
{
    app_t *a = data;
    cairo_surface_t *s = g_object_get_data(G_OBJECT(b), "skin");
    const char *path = g_object_get_data(G_OBJECT(b), "path");
    pick_skin(a, s, path, g_str_has_suffix(path ? path : "", "_slim.png") || (!path && a->slim));
}

static GtkWidget *skin_tile(app_t *a, cairo_surface_t *s, const char *name, const char *path)
{
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "mcl-skin");
    gtk_widget_set_valign(b, GTK_ALIGN_START);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *da = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da), 48);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), 48);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_head, s, NULL);
    gtk_box_append(GTK_BOX(box), da);
    GtkWidget *l = mc_label(name, "mcl-skin-name");
    gtk_label_set_xalign(GTK_LABEL(l), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 10);
    gtk_box_append(GTK_BOX(box), l);
    gtk_button_set_child(GTK_BUTTON(b), box);
    if (s != a->skin)
        g_object_set_data_full(G_OBJECT(b), "skin", s, (GDestroyNotify)cairo_surface_destroy);
    else
        g_object_set_data(G_OBJECT(b), "skin", s);
    g_object_set_data_full(G_OBJECT(b), "path", g_strdup(path), g_free);
    g_signal_connect(b, "clicked", G_CALLBACK(on_skin_tile), a);
    return b;
}

void mc_skins_reload(app_t *a)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(a->skin_flow)))
        gtk_flow_box_remove(GTK_FLOW_BOX(a->skin_flow), c);
    if (a->skin)
        gtk_flow_box_append(GTK_FLOW_BOX(a->skin_flow), skin_tile(a, a->skin, "Current", NULL));
    char *dir = skin_dir();
    GDir *d = g_dir_open(dir, 0, NULL);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    for (const char *e; d && (e = g_dir_read_name(d));)
        if (g_str_has_suffix(e, ".png"))
            g_ptr_array_add(names, g_strdup(e));
    if (d)
        g_dir_close(d);
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
    for (guint i = 0; i < names->len; i++) {
        char *f = g_build_filename(dir, names->pdata[i], NULL);
        cairo_surface_t *s = cairo_image_surface_create_from_png(f);
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS && cairo_image_surface_get_width(s) == 64) {
            char *nm = g_strndup(names->pdata[i], strlen(names->pdata[i]) - 4);
            if (g_str_has_suffix(nm, "_slim"))
                nm[strlen(nm) - 5] = 0;
            gtk_flow_box_append(GTK_FLOW_BOX(a->skin_flow), skin_tile(a, s, nm, f));
            g_free(nm);
        } else {
            cairo_surface_destroy(s);
        }
        g_free(f);
    }
    g_ptr_array_unref(names);
    g_free(dir);
}

/* add a png to the library */
static bool add_skin_file(app_t *a, GFile *src)
{
    char *p = g_file_get_path(src);
    cairo_surface_t *s = p ? cairo_image_surface_create_from_png(p) : NULL;
    bool ok = s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS && cairo_image_surface_get_width(s) == 64 &&
              (cairo_image_surface_get_height(s) == 64 || cairo_image_surface_get_height(s) == 32);
    if (ok) {
        char *base = g_file_get_basename(src), *dir = skin_dir();
        char *dst = g_build_filename(dir, base, NULL);
        GFile *df = g_file_new_for_path(dst);
        g_file_copy(src, df, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        g_object_unref(df);
        mc_skins_reload(a);
        pick_skin(a, s, dst, false);
        mc_status(a, "Added %s. Check it, then “Use this skin”.", base);
        g_free(dst);
        g_free(dir);
        g_free(base);
    } else {
        mc_status(a, "That isn't a Minecraft skin (a 64x64 png).");
    }
    if (s)
        cairo_surface_destroy(s);
    g_free(p);
    return ok;
}

static void on_skin_file_done(GObject *src, GAsyncResult *res, gpointer data)
{
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (f) {
        add_skin_file(data, f);
        g_object_unref(f);
    }
}

static void on_skin_file(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Choose a skin");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Skins (png)");
    gtk_file_filter_add_mime_type(ff, "image/png");
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(fl));
    gtk_file_dialog_open(d, a->win, NULL, on_skin_file_done, a);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(d);
}

static gboolean on_skin_drop(GtkDropTarget *t, const GValue *v, double x, double y, gpointer data)
{
    (void)t;
    (void)x;
    (void)y;
    if (!G_VALUE_HOLDS(v, GDK_TYPE_FILE_LIST))
        return FALSE;
    bool any = false;
    for (GSList *l = g_value_get_boxed(v); l; l = l->next)
        any |= add_skin_file(data, l->data);
    return any;
}

/* ---- steal by name: Mojang profile -> textures -> png ---- */
typedef struct { app_t *a; char *name, *path, *err; bool slim; } fetch_t;

static void fetch_name_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    fetch_t *f = data;
    char *esc = g_uri_escape_string(f->name, NULL, FALSE);
    char *url = g_strdup_printf("https://api.mojang.com/users/profiles/minecraft/%s", esc);
    g_free(esc);
    GByteArray *b = g_byte_array_new();
    long code = mc_http(NULL, url, NULL, NULL, NULL, b);
    g_free(url);
    JsonParser *p = json_parser_new();
    char *id = NULL;
    if (code == 200 && json_parser_load_from_data(p, (char *)b->data, b->len, NULL))
        id = g_strdup(json_object_get_string_member_with_default(json_node_get_object(json_parser_get_root(p)), "id", ""));
    g_object_unref(p);
    g_byte_array_set_size(b, 0);
    if (!id || !*id) {
        f->err = g_strdup_printf("No player called %s", f->name);
        goto out;
    }
    url = g_strdup_printf("https://sessionserver.mojang.com/session/minecraft/profile/%s", id);
    code = mc_http(NULL, url, NULL, NULL, NULL, b);
    g_free(url);
    p = json_parser_new();
    char *skin_url = NULL;
    if (code == 200 && json_parser_load_from_data(p, (char *)b->data, b->len, NULL)) {
        JsonArray *props = json_object_get_array_member(json_node_get_object(json_parser_get_root(p)), "properties");
        for (guint i = 0; props && i < json_array_get_length(props); i++) {
            JsonObject *o = json_array_get_object_element(props, i);
            if (g_strcmp0(json_object_get_string_member(o, "name"), "textures"))
                continue;
            gsize n;
            guchar *dec = g_base64_decode(json_object_get_string_member(o, "value"), &n);
            JsonParser *p2 = json_parser_new();
            if (json_parser_load_from_data(p2, (char *)dec, n, NULL)) {
                JsonObject *tx = json_object_get_object_member(json_node_get_object(json_parser_get_root(p2)), "textures");
                JsonObject *sk = tx && json_object_has_member(tx, "SKIN") ? json_object_get_object_member(tx, "SKIN") : NULL;
                if (sk) {
                    skin_url = g_strdup(json_object_get_string_member(sk, "url"));
                    JsonObject *md = json_object_has_member(sk, "metadata") ? json_object_get_object_member(sk, "metadata") : NULL;
                    f->slim = md && !g_strcmp0(json_object_get_string_member_with_default(md, "model", ""), "slim");
                }
            }
            g_object_unref(p2);
            g_free(dec);
        }
    }
    g_object_unref(p);
    g_byte_array_set_size(b, 0);
    if (!skin_url) {
        f->err = g_strdup_printf("%s has the default skin", f->name);
        goto out;
    }
    if (mc_http(NULL, skin_url, NULL, NULL, NULL, b) == 200) {
        char *dir = skin_dir();
        char *fn = g_strdup_printf("%s%s.png", f->name, f->slim ? "_slim" : "");
        f->path = g_build_filename(dir, fn, NULL);
        g_file_set_contents(f->path, (char *)b->data, b->len, NULL);
        g_free(fn);
        g_free(dir);
    } else {
        f->err = g_strdup("Couldn't download the skin");
    }
    g_free(skin_url);
out:
    g_free(id);
    g_byte_array_unref(b);
    g_task_return_boolean(t, TRUE);
}

static void fetch_name_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)r;
    fetch_t *f = data;
    app_t *a = f->a;
    if (f->path) {
        mc_skins_reload(a);
        cairo_surface_t *sf = cairo_image_surface_create_from_png(f->path);
        if (cairo_surface_status(sf) == CAIRO_STATUS_SUCCESS)
            pick_skin(a, sf, f->path, f->slim);
        cairo_surface_destroy(sf);
        mc_status(a, "Got %s's skin. “Use this skin” makes it yours.", f->name);
    } else {
        mc_status(a, "%s", f->err ? f->err : "Failed");
    }
    g_free(f->name);
    g_free(f->path);
    g_free(f->err);
    g_free(f);
}

static void on_skin_name(GtkWidget *w, gpointer data)
{
    (void)w;
    app_t *a = data;
    const char *n = gtk_editable_get_text(GTK_EDITABLE(a->skin_name_entry));
    if (!*n)
        return;
    fetch_t *f = g_new0(fetch_t, 1);
    f->a = a;
    f->name = g_strdup(n);
    mc_status(a, "Looking up %s…", n);
    GTask *t = g_task_new(NULL, NULL, fetch_name_done, f);
    g_task_set_task_data(t, f, NULL);
    g_task_run_in_thread(t, fetch_name_thread);
    g_object_unref(t);
}

/* ---- upload ---- */
typedef struct { app_t *a; char *path, *err; bool slim; long code; } up_t;

static void upload_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    up_t *u = data;
    char *tok = mc_auth_token(u->a, &u->err);
    if (tok) {
        GByteArray *b = g_byte_array_new();
        u->code = mc_http_upload_skin(tok, u->slim ? "slim" : "classic", u->path, b);
        g_byte_array_unref(b);
        g_free(tok);
    }
    g_task_return_boolean(t, TRUE);
}

static void upload_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)r;
    up_t *u = data;
    app_t *a = u->a;
    a->busy = false;
    if (u->code == 200) {
        /* keep it as "the current skin" until Prism refreshes its copy */
        char *dir = g_build_filename(g_get_user_cache_dir(), "tux-mc", NULL);
        g_mkdir_with_parents(dir, 0755);
        char *dst = g_build_filename(dir, "skin.png", NULL), *slim = g_strconcat(dst, ".slim", NULL);
        GFile *sf = g_file_new_for_path(u->path), *df = g_file_new_for_path(dst);
        g_file_copy(sf, df, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        if (u->slim)
            g_file_set_contents(slim, "", 0, NULL);
        else
            g_unlink(slim);
        cairo_surface_t *ns = cairo_image_surface_create_from_png(dst);
        if (cairo_surface_status(ns) == CAIRO_STATUS_SUCCESS) {
            a->skin = ns;                 /* the old one stays alive in tiles; small leak is fine */
            a->slim = u->slim;
            a->view.skin = ns;
            a->view.slim = u->slim;
            a->view.yaw_vel = 10;
            a->view.idle = 0;
        }
        g_object_unref(sf);
        g_object_unref(df);
        g_free(dst);
        g_free(slim);
        g_free(dir);
        mc_skins_reload(a);
        mc_status(a, "New skin is live. Everyone sees it after rejoining.");
    } else {
        mc_status(a, "Upload failed: %s", u->err ? u->err : u->code == 400 ? "Mojang didn't accept that png" : "no connection");
    }
    g_free(u->path);
    g_free(u->err);
    g_free(u);
}

/* upload a skin png as the account's skin (the account menu uses it too) */
void mc_skin_upload(app_t *a, const char *path, bool slim)
{
    if (a->busy)
        return;
    a->busy = true;
    up_t *u = g_new0(up_t, 1);
    u->a = a;
    u->path = g_strdup(path);
    u->slim = slim;
    mc_status(a, "Uploading the skin…");
    GTask *t = g_task_new(NULL, NULL, upload_done, u);
    g_task_set_task_data(t, u, NULL);
    g_task_run_in_thread(t, upload_thread);
    g_object_unref(t);
}

static void on_skin_apply(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    if (a->busy || !a->skin_pick_path)
        return;
    a->busy = true;
    up_t *u = g_new0(up_t, 1);
    u->a = a;
    u->path = g_strdup(a->skin_pick_path);
    u->slim = gtk_switch_get_active(GTK_SWITCH(a->skin_slim));
    mc_status(a, "Uploading to Mojang…");
    GTask *t = g_task_new(NULL, NULL, upload_done, u);
    g_task_set_task_data(t, u, NULL);
    g_task_run_in_thread(t, upload_thread);
    g_object_unref(t);
}

static void on_slim(GObject *o, GParamSpec *ps, gpointer data)
{
    (void)ps;
    app_t *a = data;
    a->skin_view.slim = gtk_switch_get_active(GTK_SWITCH(o));
}

static void on_skin_remove(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    if (!a->skin_pick_path)
        return;
    GFile *f = g_file_new_for_path(a->skin_pick_path);
    if (g_file_trash(f, NULL, NULL)) {
        mc_status(a, "Moved to the trash");
        mc_skins_reload(a);
        pick_skin(a, a->skin, NULL, a->slim);
    }
    g_object_unref(f);
}

static void on_skin_folder(GtkButton *b, gpointer data)
{
    (void)b;
    char *d = skin_dir();
    mc_open_path(data, d);
    g_free(d);
}

GtkWidget *mc_page_skins(app_t *a)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(page, "mcl-page");
    a->skin_stage = mc_stage(a, &a->skin_view, draw_skin_stage);
    gtk_widget_set_size_request(a->skin_stage, 380, 480);
    gtk_box_append(GTK_BOX(page), a->skin_stage);

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_valign(right, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(right), mc_label("Skins", "mcl-title"));

    GtkWidget *lib = mc_card("Library", "Click one to try it on. Drop png files anywhere on this page.");
    a->skin_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(a->skin_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(a->skin_flow), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(a->skin_flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(a->skin_flow), 8);
    gtk_widget_set_valign(a->skin_flow, GTK_ALIGN_START);
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sc), 180);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), 260);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), a->skin_flow);
    gtk_box_append(GTK_BOX(lib), sc);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *add = gtk_button_new_with_label("Add skin file…");
    g_signal_connect(add, "clicked", G_CALLBACK(on_skin_file), a);
    gtk_box_append(GTK_BOX(row), add);
    GtkWidget *fold = gtk_button_new_with_label("Open folder");
    g_signal_connect(fold, "clicked", G_CALLBACK(on_skin_folder), a);
    gtk_box_append(GTK_BOX(row), fold);
    a->skin_remove = gtk_button_new_with_label("Remove");
    gtk_widget_set_tooltip_text(a->skin_remove, "the picked skin, to the trash");
    gtk_widget_set_sensitive(a->skin_remove, FALSE);
    g_signal_connect(a->skin_remove, "clicked", G_CALLBACK(on_skin_remove), a);
    gtk_box_append(GTK_BOX(row), a->skin_remove);
    gtk_box_append(GTK_BOX(lib), row);
    gtk_box_append(GTK_BOX(right), lib);

    GtkWidget *steal = mc_card("Copy a player's skin", "Any Java player name.");
    GtkWidget *srow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    a->skin_name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->skin_name_entry), "player name");
    gtk_widget_set_hexpand(a->skin_name_entry, TRUE);
    g_signal_connect(a->skin_name_entry, "activate", G_CALLBACK(on_skin_name), a);
    gtk_box_append(GTK_BOX(srow), a->skin_name_entry);
    GtkWidget *get = gtk_button_new_with_label("Get");
    g_signal_connect(get, "clicked", G_CALLBACK(on_skin_name), a);
    gtk_box_append(GTK_BOX(srow), get);
    gtk_box_append(GTK_BOX(steal), srow);
    gtk_box_append(GTK_BOX(right), steal);

    GtkWidget *use = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(use, "mcl-glass");
    gtk_box_append(GTK_BOX(use), mc_label("Slim arms", NULL));
    a->skin_slim = gtk_switch_new();
    gtk_widget_set_valign(a->skin_slim, GTK_ALIGN_CENTER);
    g_signal_connect(a->skin_slim, "notify::active", G_CALLBACK(on_slim), a);
    gtk_box_append(GTK_BOX(use), a->skin_slim);
    GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(sp, TRUE);
    gtk_box_append(GTK_BOX(use), sp);
    a->skin_apply = gtk_button_new_with_label("Use this skin");
    gtk_widget_add_css_class(a->skin_apply, "accent");
    g_signal_connect(a->skin_apply, "clicked", G_CALLBACK(on_skin_apply), a);
    gtk_box_append(GTK_BOX(use), a->skin_apply);
    gtk_box_append(GTK_BOX(right), use);
    gtk_box_append(GTK_BOX(page), right);

    GtkDropTarget *dt = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(dt, "drop", G_CALLBACK(on_skin_drop), a);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(dt));

    mc_skins_reload(a);
    if (a->skin)
        pick_skin(a, a->skin, NULL, a->slim);
    return page;
}

/* ================================================================= mods */
static void mod_free(mc_mod_t *m)
{
    g_free(m->file);
    g_free(m->id);
    g_free(m->name);
    g_free(m->version);
    g_free(m->desc);
    if (m->icon)
        g_bytes_unref(m->icon);
    g_free(m);
}

static GBytes *unzip_p(const char *jar, const char *entry)
{
    char *argv[] = {"unzip", "-p", (char *)jar, (char *)entry, NULL};
    char *out = NULL;
    gsize n = 0;
    GError *e = NULL;
    GSubprocess *p = g_subprocess_newv((const char *const *)argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                       G_SUBPROCESS_FLAGS_STDERR_SILENCE, &e);
    if (!p) {
        g_clear_error(&e);
        return NULL;
    }
    GBytes *b = NULL;
    if (g_subprocess_communicate(p, NULL, NULL, &b, NULL, NULL) && b && g_bytes_get_size(b) > 0) {
        g_object_unref(p);
        return b;
    }
    if (b)
        g_bytes_unref(b);
    g_object_unref(p);
    (void)out;
    (void)n;
    return NULL;
}

GdkTexture *mc_mod_icon(const mc_mod_t *m)
{
    return m && m->icon ? gdk_texture_new_from_bytes(m->icon, NULL) : NULL;
}

static void mods_scan_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    app_t *a = data;
    GPtrArray *mods = g_ptr_array_new_with_free_func((GDestroyNotify)mod_free);
    char *dir = g_build_filename(a->mcdir, "mods", NULL);
    GDir *d = g_dir_open(dir, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        bool on = g_str_has_suffix(e, ".jar"), off = g_str_has_suffix(e, ".jar.disabled");
        if (!on && !off)
            continue;
        mc_mod_t *m = g_new0(mc_mod_t, 1);
        m->file = g_build_filename(dir, e, NULL);
        m->enabled = on;
        GBytes *js = unzip_p(m->file, "fabric.mod.json");
        if (js) {
            JsonParser *p = json_parser_new();
            gsize n;
            const char *data = g_bytes_get_data(js, &n);
            if (json_parser_load_from_data(p, data, n, NULL) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(p))) {
                JsonObject *o = json_node_get_object(json_parser_get_root(p));
                m->id = g_strdup(json_object_get_string_member_with_default(o, "id", NULL));
                m->name = g_strdup(json_object_get_string_member_with_default(o, "name", NULL));
                m->version = g_strdup(json_object_get_string_member_with_default(o, "version", ""));
                m->desc = g_strdup(json_object_get_string_member_with_default(o, "description", ""));
                const char *icon = NULL;
                if (json_object_has_member(o, "icon")) {
                    JsonNode *in = json_object_get_member(o, "icon");
                    if (JSON_NODE_HOLDS_VALUE(in))
                        icon = json_node_get_string(in);
                    else if (JSON_NODE_HOLDS_OBJECT(in)) {   /* {"16": "...", "128": "..."}: the last */
                        GList *mem = json_object_get_members(json_node_get_object(in));
                        for (GList *l = mem; l; l = l->next)
                            icon = json_object_get_string_member(json_node_get_object(in), l->data);
                        g_list_free(mem);
                    }
                }
                if (icon)
                    m->icon = unzip_p(m->file, icon);
            }
            g_object_unref(p);
            g_bytes_unref(js);
        }
        if (!m->name)
            m->name = g_strdup(e);
        g_ptr_array_add(mods, m);
    }
    if (d)
        g_dir_close(d);
    g_free(dir);
    g_task_return_pointer(t, mods, (GDestroyNotify)g_ptr_array_unref);
}

static int mod_cmp(gconstpointer x, gconstpointer y)
{
    const mc_mod_t *a = *(mc_mod_t *const *)x, *b = *(mc_mod_t *const *)y;
    return g_ascii_strcasecmp(a->name, b->name);
}


static void on_mc_mod_toggle(GObject *sw, GParamSpec *ps, gpointer data)
{
    (void)ps;
    app_t *a = data;
    const char *file = g_object_get_data(sw, "file");
    bool on = gtk_switch_get_active(GTK_SWITCH(sw));
    char *to = on ? g_strndup(file, strlen(file) - strlen(".disabled")) : g_strconcat(file, ".disabled", NULL);
    if (g_rename(file, to) == 0) {
        g_object_set_data_full(sw, "file", g_strdup(to), g_free);
        mc_status(a, "%s for the next start", on ? "Enabled" : "Disabled");
    }
    g_free(to);
}

static void on_mod_remove(GtkButton *b, gpointer data)
{
    app_t *a = data;
    GtkWidget *sw = g_object_get_data(G_OBJECT(b), "switch");
    const char *file = g_object_get_data(G_OBJECT(sw), "file");
    GFile *f = g_file_new_for_path(file);
    if (g_file_trash(f, NULL, NULL)) {
        mc_status(a, "Moved to the trash");
        mc_mods_scan(a);
    }
    g_object_unref(f);
}

static GtkWidget *mod_row(app_t *a, mc_mod_t *m)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "mcl-row");
    GtkWidget *icon;
    GdkTexture *tex = mc_mod_icon(m);
    if (tex) {
        icon = gtk_picture_new_for_paintable(GDK_PAINTABLE(tex));
        g_object_unref(tex);
    } else {
        icon = gtk_image_new_from_icon_name("application-x-addon-symbolic");
    }
    gtk_widget_set_size_request(icon, 40, 40);
    gtk_widget_add_css_class(icon, "mcl-modicon");
    gtk_box_append(GTK_BOX(row), icon);
    GtkWidget *txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(txt, TRUE);
    char *t = g_markup_printf_escaped("<b>%s</b>  <span alpha='55%%'>%s</span>", m->name, m->version ? m->version : "");
    GtkWidget *nl = mc_label(NULL, NULL);
    gtk_label_set_markup(GTK_LABEL(nl), t);
    g_free(t);
    gtk_box_append(GTK_BOX(txt), nl);
    GtkWidget *dl = mc_label(m->desc ? m->desc : "", "dim");
    gtk_label_set_ellipsize(GTK_LABEL(dl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(dl), 80);
    gtk_box_append(GTK_BOX(txt), dl);
    gtk_box_append(GTK_BOX(row), txt);
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), m->enabled);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(sw), "file", g_strdup(m->file), g_free);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_mc_mod_toggle), a);
    gtk_box_append(GTK_BOX(row), sw);
    GtkWidget *rm = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(rm, "gear");
    gtk_widget_set_valign(rm, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(rm, "Remove (to the trash)");
    g_object_set_data(G_OBJECT(rm), "switch", sw);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_mod_remove), a);
    gtk_box_append(GTK_BOX(row), rm);
    g_object_set_data_full(G_OBJECT(row), "search",
                           g_utf8_strdown(m->name, -1), g_free);
    return row;
}

static void mods_scan_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    app_t *a = data;
    GPtrArray *mods = g_task_propagate_pointer(G_TASK(r), NULL);
    if (!mods)
        return;
    g_ptr_array_sort(mods, mod_cmp);
    gtk_list_box_remove_all(GTK_LIST_BOX(a->mods_list));
    for (guint i = 0; i < mods->len; i++)
        gtk_list_box_append(GTK_LIST_BOX(a->mods_list), mod_row(a, mods->pdata[i]));
    if (a->mods)
        g_ptr_array_unref(a->mods);
    a->mods = mods;
    mc_configs_refresh(a);
}

void mc_mods_scan(app_t *a)
{
    GTask *t = g_task_new(NULL, NULL, mods_scan_done, a);
    g_task_set_task_data(t, a, NULL);
    g_task_run_in_thread(t, mods_scan_thread);
    g_object_unref(t);
}

static gboolean mods_filter(GtkListBoxRow *row, gpointer data)
{
    app_t *a = data;
    const char *q = gtk_editable_get_text(GTK_EDITABLE(a->mods_search));
    if (!*q)
        return TRUE;
    const char *s = g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(row)), "search");
    char *ql = g_utf8_strdown(q, -1);
    bool hit = s && strstr(s, ql);
    g_free(ql);
    return hit;
}

static void on_mods_search(GtkSearchEntry *e, gpointer data)
{
    (void)e;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(((app_t *)data)->mods_list));
}

/* any file in: jars to mods/, packs to resourcepacks/ or shaderpacks/ */
static const char *route(app_t *a, GFile *f)
{
    char *p = g_file_get_path(f), *base = g_file_get_basename(f);
    const char *dir = NULL;
    if (g_str_has_suffix(base, ".jar"))
        dir = "mods";
    else if (g_str_has_suffix(base, ".zip")) {
        GBytes *b = unzip_p(p, "shaders/*.properties");
        if (!b)
            b = unzip_p(p, "shaders/composite.fsh");
        dir = b ? "shaderpacks" : "resourcepacks";
        if (b)
            g_bytes_unref(b);
    }
    if (dir) {
        char *dst = g_build_filename(a->mcdir, dir, base, NULL);
        GFile *df = g_file_new_for_path(dst);
        if (!g_file_copy(f, df, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL))
            dir = NULL;
        g_object_unref(df);
        g_free(dst);
    }
    g_free(p);
    g_free(base);
    return dir;
}

static void add_files(app_t *a, GSList *files)
{
    int mods = 0, rp = 0, sp = 0, bad = 0;
    for (GSList *l = files; l; l = l->next) {
        const char *d = route(a, l->data);
        if (!d)
            bad++;
        else if (!strcmp(d, "mods"))
            mods++;
        else if (!strcmp(d, "shaderpacks"))
            sp++;
        else
            rp++;
    }
    mc_status(a, "Added %d mod%s, %d resource pack%s, %d shader pack%s%s", mods, mods == 1 ? "" : "s", rp,
              rp == 1 ? "" : "s", sp, sp == 1 ? "" : "s", bad ? " (some files weren't mods or packs)" : "");
    if (mods)
        mc_mods_scan(a);
}

static gboolean on_mods_drop(GtkDropTarget *t, const GValue *v, double x, double y, gpointer data)
{
    (void)t;
    (void)x;
    (void)y;
    if (!G_VALUE_HOLDS(v, GDK_TYPE_FILE_LIST))
        return FALSE;
    add_files(data, g_value_get_boxed(v));
    return TRUE;
}

static void on_add_files_done(GObject *src, GAsyncResult *res, gpointer data)
{
    GListModel *l = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!l)
        return;
    GSList *files = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(l); i++)
        files = g_slist_append(files, g_list_model_get_item(l, i));
    add_files(data, files);
    g_slist_free_full(files, g_object_unref);
    g_object_unref(l);
}

static void on_add_files(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Add mods, resource packs or shaders");
    gtk_file_dialog_open_multiple(d, a->win, NULL, on_add_files_done, a);
    g_object_unref(d);
}

/* ---- Modrinth ---- */
typedef struct { app_t *a; char *query; GPtrArray *hits; } search_t;
typedef struct { char *id, *title, *desc, *icon_url; long downloads; GBytes *icon; } hit_t;

static void hit_free(hit_t *h)
{
    g_free(h->id);
    g_free(h->title);
    g_free(h->desc);
    g_free(h->icon_url);
    if (h->icon)
        g_bytes_unref(h->icon);
    g_free(h);
}

static void search_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    search_t *q = data;
    char *esc = g_uri_escape_string(q->query, NULL, FALSE);
    char *f = g_strdup_printf("[[\"versions:%s\"],[\"categories:%s\"],[\"project_type:mod\"]]",
                              q->a->mc_version ? q->a->mc_version : "", mc_loader_slug(q->a));
    char *facets = g_uri_escape_string(f, NULL, FALSE);
    g_free(f);
    char *url = g_strdup_printf("https://api.modrinth.com/v2/search?query=%s&facets=%s&limit=25", esc, facets);
    g_free(esc);
    g_free(facets);
    GByteArray *b = g_byte_array_new();
    q->hits = g_ptr_array_new_with_free_func((GDestroyNotify)hit_free);
    if (mc_http(NULL, url, NULL, NULL, NULL, b) == 200) {
        JsonParser *p = json_parser_new();
        if (json_parser_load_from_data(p, (char *)b->data, b->len, NULL)) {
            JsonArray *hits = json_object_get_array_member(json_node_get_object(json_parser_get_root(p)), "hits");
            for (guint i = 0; hits && i < json_array_get_length(hits); i++) {
                JsonObject *o = json_array_get_object_element(hits, i);
                hit_t *h = g_new0(hit_t, 1);
                h->id = g_strdup(json_object_get_string_member_with_default(o, "project_id", ""));
                h->title = g_strdup(json_object_get_string_member_with_default(o, "title", ""));
                h->desc = g_strdup(json_object_get_string_member_with_default(o, "description", ""));
                h->icon_url = g_strdup(json_object_get_string_member_with_default(o, "icon_url", ""));
                h->downloads = json_object_get_int_member_with_default(o, "downloads", 0);
                g_ptr_array_add(q->hits, h);
            }
        }
        g_object_unref(p);
    }
    g_free(url);
    for (guint i = 0; i < q->hits->len; i++) {       /* icons */
        hit_t *h = q->hits->pdata[i];
        if (!*h->icon_url)
            continue;
        g_byte_array_set_size(b, 0);
        if (mc_http(NULL, h->icon_url, NULL, NULL, NULL, b) == 200)
            h->icon = g_bytes_new(b->data, b->len);
    }
    g_byte_array_unref(b);
    g_task_return_boolean(t, TRUE);
}

typedef struct { app_t *a; char *id, *title, *err; int installed; } inst_t;

/* the newest file of a project for this instance's version and loader, plus
 * its required dependencies */
static void install_project(inst_t *in, const char *id, int depth)
{
    if (depth > 4)
        return;
    char *ls = g_strdup_printf("[\"%s\"]", mc_loader_slug(in->a)),
         *vs_ = g_strdup_printf("[\"%s\"]", in->a->mc_version ? in->a->mc_version : "");
    char *loaders = g_uri_escape_string(ls, NULL, FALSE), *gv = g_uri_escape_string(vs_, NULL, FALSE);
    g_free(ls);
    g_free(vs_);
    char *url = g_strdup_printf("https://api.modrinth.com/v2/project/%s/version?loaders=%s&game_versions=%s", id,
                                loaders, gv);
    g_free(loaders);
    g_free(gv);
    GByteArray *b = g_byte_array_new();
    long code = mc_http(NULL, url, NULL, NULL, NULL, b);
    g_free(url);
    JsonParser *p = json_parser_new();
    if (code != 200 || !json_parser_load_from_data(p, (char *)b->data, b->len, NULL)) {
        if (!in->err)
            in->err = g_strdup("Modrinth didn't answer");
        goto out;
    }
    JsonArray *vs = json_node_get_array(json_parser_get_root(p));
    if (!vs || !json_array_get_length(vs)) {
        if (!in->err)
            in->err = g_strdup_printf("no version for %s + %s", in->a->mc_version, mc_loader_name(in->a->loader));
        goto out;
    }
    JsonObject *v = json_array_get_object_element(vs, 0);
    JsonArray *files = json_object_get_array_member(v, "files");
    JsonObject *f = NULL;
    for (guint i = 0; files && i < json_array_get_length(files); i++) {
        JsonObject *o = json_array_get_object_element(files, i);
        if (!f || json_object_get_boolean_member_with_default(o, "primary", FALSE))
            f = o;
    }
    if (f) {
        const char *fn = json_object_get_string_member(f, "filename");
        char *dst = g_build_filename(in->a->mcdir, "mods", fn, NULL);
        if (!g_file_test(dst, G_FILE_TEST_EXISTS)) {
            GByteArray *jar = g_byte_array_new();
            if (mc_http(NULL, json_object_get_string_member(f, "url"), NULL, NULL, NULL, jar) == 200) {
                char *sha = g_compute_checksum_for_data(G_CHECKSUM_SHA512, jar->data, jar->len);
                JsonObject *hs = json_object_get_object_member(f, "hashes");
                if (hs && !g_strcmp0(sha, json_object_get_string_member_with_default(hs, "sha512", "")) &&
                    g_file_set_contents(dst, (char *)jar->data, jar->len, NULL))
                    in->installed++;
                else if (!in->err)
                    in->err = g_strdup("download didn't match its checksum");
                g_free(sha);
            }
            g_byte_array_unref(jar);
        }
        g_free(dst);
    }
    JsonArray *deps = json_object_get_array_member(v, "dependencies");
    for (guint i = 0; deps && i < json_array_get_length(deps); i++) {
        JsonObject *d = json_array_get_object_element(deps, i);
        if (g_strcmp0(json_object_get_string_member_with_default(d, "dependency_type", ""), "required"))
            continue;
        const char *pid = json_object_get_string_member_with_default(d, "project_id", NULL);
        if (pid && strcmp(pid, "P7dR8mSH"))          /* fabric-api is already there */
            install_project(in, pid, depth + 1);
    }
out:
    g_object_unref(p);
    g_byte_array_unref(b);
}

static void install_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    inst_t *in = data;
    install_project(in, in->id, 0);
    g_task_return_boolean(t, TRUE);
}

static void install_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)r;
    inst_t *in = data;
    if (in->installed)
        mc_status(in->a, "Installed %s%s", in->title, in->installed > 1 ? " and what it needs" : "");
    else
        mc_status(in->a, "%s: %s", in->title, in->err ? in->err : "already installed");
    mc_mods_scan(in->a);
    g_free(in->id);
    g_free(in->title);
    g_free(in->err);
    g_free(in);
}

static void on_install(GtkButton *b, gpointer data)
{
    app_t *a = data;
    inst_t *in = g_new0(inst_t, 1);
    in->a = a;
    in->id = g_strdup(g_object_get_data(G_OBJECT(b), "id"));
    in->title = g_strdup(g_object_get_data(G_OBJECT(b), "title"));
    mc_status(a, "Installing %s…", in->title);
    gtk_widget_set_sensitive(GTK_WIDGET(b), FALSE);
    gtk_button_set_label(b, "Installed");
    GTask *t = g_task_new(NULL, NULL, install_done, in);
    g_task_set_task_data(t, in, NULL);
    g_task_run_in_thread(t, install_thread);
    g_object_unref(t);
}

static void search_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)r;
    search_t *q = data;
    app_t *a = q->a;
    gtk_spinner_stop(GTK_SPINNER(a->mr_spinner));
    gtk_list_box_remove_all(GTK_LIST_BOX(a->mr_list));
    for (guint i = 0; i < q->hits->len; i++) {
        hit_t *h = q->hits->pdata[i];
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "mcl-row");
        GdkTexture *tex = h->icon ? gdk_texture_new_from_bytes(h->icon, NULL) : NULL;
        GtkWidget *icon = tex ? gtk_picture_new_for_paintable(GDK_PAINTABLE(tex))
                              : gtk_image_new_from_icon_name("application-x-addon-symbolic");
        if (tex)
            g_object_unref(tex);
        gtk_widget_set_size_request(icon, 40, 40);
        gtk_widget_add_css_class(icon, "mcl-modicon");
        gtk_box_append(GTK_BOX(row), icon);
        GtkWidget *txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(txt, TRUE);
        char *dl = h->downloads >= 1000000 ? g_strdup_printf("%.1fM", h->downloads / 1e6)
                   : h->downloads >= 1000  ? g_strdup_printf("%.0fk", h->downloads / 1e3)
                                           : g_strdup_printf("%ld", h->downloads);
        char *t = g_markup_printf_escaped("<b>%s</b>  <span alpha='55%%'>%s downloads</span>", h->title, dl);
        g_free(dl);
        GtkWidget *nl = mc_label(NULL, NULL);
        gtk_label_set_markup(GTK_LABEL(nl), t);
        g_free(t);
        gtk_box_append(GTK_BOX(txt), nl);
        GtkWidget *dd = mc_label(h->desc, "dim");
        gtk_label_set_ellipsize(GTK_LABEL(dd), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(dd), 80);
        gtk_box_append(GTK_BOX(txt), dd);
        gtk_box_append(GTK_BOX(row), txt);
        GtkWidget *ib = gtk_button_new_with_label("Install");
        gtk_widget_add_css_class(ib, "accent");
        gtk_widget_set_valign(ib, GTK_ALIGN_CENTER);
        g_object_set_data_full(G_OBJECT(ib), "id", g_strdup(h->id), g_free);
        g_object_set_data_full(G_OBJECT(ib), "title", g_strdup(h->title), g_free);
        g_signal_connect(ib, "clicked", G_CALLBACK(on_install), a);
        gtk_box_append(GTK_BOX(row), ib);
        gtk_list_box_append(GTK_LIST_BOX(a->mr_list), row);
    }
    if (!q->hits->len)
        mc_status(a, "Nothing on Modrinth for “%s” (%s, %s)", q->query, a->mc_version, mc_loader_name(a->loader));
    g_ptr_array_unref(q->hits);
    g_free(q->query);
    g_free(q);
}

static void on_mr_search(GtkWidget *w, gpointer data)
{
    (void)w;
    app_t *a = data;
    search_t *q = g_new0(search_t, 1);
    q->a = a;
    q->query = g_strdup(gtk_editable_get_text(GTK_EDITABLE(a->mr_entry)));
    gtk_spinner_start(GTK_SPINNER(a->mr_spinner));
    GTask *t = g_task_new(NULL, NULL, search_done, q);
    g_task_set_task_data(t, q, NULL);
    g_task_run_in_thread(t, search_thread);
    g_object_unref(t);
}

static void on_mods_view(GtkToggleButton *b, gpointer data)
{
    app_t *a = data;
    if (!gtk_toggle_button_get_active(b))
        return;
    const char *v = g_object_get_data(G_OBJECT(b), "view");
    gtk_stack_set_visible_child_name(GTK_STACK(a->mods_stack), v);
    if (!strcmp(v, "browse") && !gtk_widget_get_first_child(a->mr_list))
        on_mr_search(NULL, a);                       /* popular mods first */
}

static GtkWidget *scrolled(GtkWidget *child)
{
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), child);
    gtk_widget_set_vexpand(sc, TRUE);
    return sc;
}

GtkWidget *mc_page_mods(app_t *a)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(page, "mcl-page");
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *t = mc_label("Mods", "mcl-title");
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(head), t);
    GtkWidget *b1 = gtk_toggle_button_new_with_label("Installed"), *b2 = gtk_toggle_button_new_with_label("Modrinth");
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b2), GTK_TOGGLE_BUTTON(b1));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b1), TRUE);
    g_object_set_data(G_OBJECT(b1), "view", "installed");
    g_object_set_data(G_OBJECT(b2), "view", "browse");
    g_signal_connect(b1, "toggled", G_CALLBACK(on_mods_view), a);
    g_signal_connect(b2, "toggled", G_CALLBACK(on_mods_view), a);
    GtkWidget *seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(seg, "linked");
    gtk_widget_add_css_class(seg, "mcl-seg");
    gtk_box_append(GTK_BOX(seg), b1);
    gtk_box_append(GTK_BOX(seg), b2);
    gtk_box_append(GTK_BOX(head), seg);
    GtkWidget *add = gtk_button_new_with_label("Add files…");
    gtk_widget_add_css_class(add, "accent");
    gtk_widget_set_tooltip_text(add, "Mods (.jar), resource packs and shader packs (.zip). Or drop them here.");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_files), a);
    gtk_box_append(GTK_BOX(head), add);
    gtk_box_append(GTK_BOX(page), head);

    a->mods_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a->mods_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(a->mods_stack), 300);
    gtk_widget_set_vexpand(a->mods_stack, TRUE);

    GtkWidget *inst = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    a->mods_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(a->mods_search), "filter installed mods");
    g_signal_connect(a->mods_search, "search-changed", G_CALLBACK(on_mods_search), a);
    gtk_box_append(GTK_BOX(inst), a->mods_search);
    a->mods_list = gtk_list_box_new();
    gtk_widget_add_css_class(a->mods_list, "mcl-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(a->mods_list), GTK_SELECTION_NONE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(a->mods_list), mods_filter, a, NULL);
    gtk_box_append(GTK_BOX(inst), scrolled(a->mods_list));
    gtk_stack_add_named(GTK_STACK(a->mods_stack), inst, "installed");

    GtkWidget *br = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *sr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    a->mr_entry = gtk_search_entry_new();
    char *ph = g_strdup_printf("search Modrinth (%s, %s), Enter", a->mc_version ? a->mc_version : "?",
                               mc_loader_name(a->loader));
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(a->mr_entry), ph);
    g_free(ph);
    gtk_widget_set_hexpand(a->mr_entry, TRUE);
    g_signal_connect(a->mr_entry, "activate", G_CALLBACK(on_mr_search), a);
    gtk_box_append(GTK_BOX(sr), a->mr_entry);
    a->mr_spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(sr), a->mr_spinner);
    gtk_box_append(GTK_BOX(br), sr);
    a->mr_list = gtk_list_box_new();
    gtk_widget_add_css_class(a->mr_list, "mcl-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(a->mr_list), GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(br), scrolled(a->mr_list));
    gtk_stack_add_named(GTK_STACK(a->mods_stack), br, "browse");
    gtk_box_append(GTK_BOX(page), a->mods_stack);

    GtkDropTarget *dt = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(dt, "drop", G_CALLBACK(on_mods_drop), a);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(dt));
    mc_mods_scan(a);
    return page;
}

/* ============================================================= settings */
static char *instance_cfg(app_t *a) { return g_build_filename(a->instdir, "instance.cfg", NULL); }

/* set key=value in an INI-ish file under [General] (Prism's format) */
static void ini_set(GString *s, const char *key, const char *val)
{
    char *pre = g_strdup_printf("\n%s=", key);
    char *at = strstr(s->str, pre);
    char *line = g_strdup_printf("%s=%s", key, val);
    if (at) {
        gsize start = at - s->str + 1;
        char *end = strchr(s->str + start, '\n');
        gsize len = (end ? (gsize)(end - s->str) : s->len) - start;
        g_string_erase(s, start, len);
        g_string_insert(s, start, line);
    } else {
        char *g = strstr(s->str, "[General]\n");
        if (g)
            g_string_insert(s, g - s->str + 10, g_strconcat(line, "\n", NULL));
        else
            g_string_append_printf(s, "%s\n", line);
    }
    g_free(line);
    g_free(pre);
}

static int total_ram_gb(void) { return mc_sys_ram_gb(); }

void mc_settings_apply(app_t *a)
{
    char *p = instance_cfg(a), *txt = NULL;
    if (!g_file_get_contents(p, &txt, NULL, NULL)) {
        g_free(p);
        return;
    }
    GString *s = g_string_new("\n");
    g_string_append(s, txt);
    g_free(txt);
    char buf[64];
    int max_mb = mc_cfg_int("memory_mb", mc_cfg_int("memory_gb", 10) * 1024);
    int min_mb = MIN(mc_cfg_int("memory_min_mb", 4096), max_mb);
    ini_set(s, "OverrideMemory", "true");
    g_snprintf(buf, sizeof buf, "%d", max_mb);
    ini_set(s, "MaxMemAlloc", buf);
    g_snprintf(buf, sizeof buf, "%d", min_mb);
    ini_set(s, "MinMemAlloc", buf);

    char *extra = mc_cfg_str("jvm_args", "");
    /* UseTransparentHugePages only exists on Linux: java won't start with it elsewhere */
    char *jvm = g_strdup_printf("%s%s %s", mc_cfg_bool("zgc", true)
        ? "-XX:+UseZGC -XX:+UseCompactObjectHeaders -XX:+PerfDisableSharedMem"
        : "-XX:+UseG1GC -XX:+UseCompactObjectHeaders -XX:+PerfDisableSharedMem",
        MC_LINUX && mc_cfg_bool("zgc", true) ? " -XX:+UseTransparentHugePages" : "", extra);
    ini_set(s, "OverrideJavaArgs", "true");
    ini_set(s, "JvmArgs", g_strstrip(jvm));
    g_free(jvm);
    g_free(extra);

    /* the wrapper: `env VAR=… gamemoderun …` on Linux; Windows has no env,
     * so there only your own wrapper (if any) goes in */
    GString *w = g_string_new(MC_LINUX ? "env" : "");
    if (!MC_LINUX)
        ;
    else if (mc_cfg_bool("wayland", true))
        g_string_append(w, " SDL_VIDEO_DRIVER=wayland");
    if (MC_LINUX && mc_cfg_bool("glthread", true))
        g_string_append(w, " mesa_glthread=true");
    char *env = mc_cfg_str("env", "");
    if (MC_LINUX && *g_strstrip(env))
        g_string_append_printf(w, " %s", env);
    g_free(env);
    if (MC_LINUX && mc_cfg_bool("gamemode", true) && g_find_program_in_path("gamemoderun"))
        g_string_append(w, " gamemoderun");
    char *wrap = mc_cfg_str("wrapper", "");
    if (*g_strstrip(wrap))
        g_string_append_printf(w, "%s%s", w->len ? " " : "", wrap);
    g_free(wrap);
    ini_set(s, "OverrideCommands", w->len ? "true" : "false");
    ini_set(s, "WrapperCommand", w->str);
    g_string_free(w, TRUE);

    ini_set(s, "OverrideWindow", "true");
    ini_set(s, "LaunchMaximized", mc_cfg_bool("maximized", false) ? "true" : "false");
    g_snprintf(buf, sizeof buf, "%d", mc_cfg_int("win_w", 1280));
    ini_set(s, "MinecraftWinWidth", buf);
    g_snprintf(buf, sizeof buf, "%d", mc_cfg_int("win_h", 720));
    ini_set(s, "MinecraftWinHeight", buf);

    ini_set(s, "OverrideConsole", "true");
    ini_set(s, "ShowConsole", mc_cfg_bool("console", false) ? "true" : "false");
    ini_set(s, "ShowConsoleOnError", mc_cfg_bool("console_on_error", true) ? "true" : "false");
    ini_set(s, "AutoCloseConsole", "false");

    g_file_set_contents(p, s->str + 1, s->len - 1, NULL);
    g_string_free(s, TRUE);
    g_free(p);

    /* fullscreen lives in the game's options.txt */
    char *op = g_build_filename(a->mcdir, "options.txt", NULL);
    if (g_file_get_contents(op, &txt, NULL, NULL)) {
        GString *o = g_string_new("\n");
        g_string_append(o, txt);
        g_free(txt);
        char *at = strstr(o->str, "\nfullscreen:");
        const char *v = mc_cfg_bool("fullscreen", false) ? "fullscreen:true" : "fullscreen:false";
        if (at) {
            gsize st = at - o->str + 1;
            char *end = strchr(o->str + st, '\n');
            g_string_erase(o, st, (end ? (gsize)(end - o->str) : o->len) - st);
            g_string_insert(o, st, v);
        }
        g_file_set_contents(op, o->str + 1, o->len - 1, NULL);
        g_string_free(o, TRUE);
    }
    g_free(op);
}

/* ---- rows ---- */
static GtkWidget *row_box(GtkWidget *card, const char *title, const char *sub)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "mcl-setrow");
    GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_valign(t, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(t), mc_label(title, NULL));
    if (sub) {
        GtkWidget *s = mc_label(sub, "dim");
        gtk_label_set_wrap(GTK_LABEL(s), TRUE);
        gtk_box_append(GTK_BOX(t), s);
    }
    gtk_box_append(GTK_BOX(row), t);
    gtk_box_append(GTK_BOX(card), row);
    return row;
}

static void on_set_switch(GObject *sw, GParamSpec *ps, gpointer data)
{
    (void)ps;
    app_t *a = data;
    mc_cfg_set_bool(g_object_get_data(sw, "key"), gtk_switch_get_active(GTK_SWITCH(sw)));
    if (g_object_get_data(sw, "looks"))
        mc_apply_looks(a);
    else
        mc_status(a, "Saved, used from the next start");
}

static GtkWidget *set_row(app_t *a, GtkWidget *card, const char *key, bool def, const char *title, const char *sub)
{
    GtkWidget *row = row_box(card, title, sub);
    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(sw), mc_cfg_bool(key, def));
    g_object_set_data(G_OBJECT(sw), "key", (gpointer)key);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_set_switch), a);
    gtk_box_append(GTK_BOX(row), sw);
    return sw;
}

static GtkWidget *look_row(app_t *a, GtkWidget *card, const char *key, bool def, const char *title, const char *sub)
{
    GtkWidget *sw = set_row(a, card, key, def, title, sub);
    g_object_set_data(G_OBJECT(sw), "looks", GINT_TO_POINTER(1));
    return sw;
}

static void on_spin(GtkSpinButton *sp, gpointer data)
{
    app_t *a = data;
    mc_cfg_set_int(g_object_get_data(G_OBJECT(sp), "key"), gtk_spin_button_get_value_as_int(sp));
    if (g_object_get_data(G_OBJECT(sp), "looks"))
        mc_apply_looks(a);
}

static GtkWidget *spin_row(app_t *a, GtkWidget *card, const char *key, int def, int lo, int hi, int step,
                           const char *title, const char *sub)
{
    GtkWidget *row = row_box(card, title, sub);
    GtkWidget *sp = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_widget_set_valign(sp, GTK_ALIGN_CENTER);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), mc_cfg_int(key, def));
    g_object_set_data(G_OBJECT(sp), "key", (gpointer)key);
    g_signal_connect(sp, "value-changed", G_CALLBACK(on_spin), a);
    gtk_box_append(GTK_BOX(row), sp);
    return sp;
}

static void on_entry(GtkEditable *e, gpointer data)
{
    (void)data;
    mc_cfg_set_str(g_object_get_data(G_OBJECT(e), "key"), gtk_editable_get_text(e));
}

static GtkWidget *entry_row(app_t *a, GtkWidget *card, const char *key, const char *title, const char *sub,
                            const char *placeholder)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "mcl-setrow");
    gtk_box_append(GTK_BOX(box), mc_label(title, NULL));
    if (sub) {
        GtkWidget *s = mc_label(sub, "dim");
        gtk_label_set_wrap(GTK_LABEL(s), TRUE);
        gtk_box_append(GTK_BOX(box), s);
    }
    GtkWidget *e = gtk_entry_new();
    char *v = mc_cfg_str(key, "");
    gtk_editable_set_text(GTK_EDITABLE(e), v);
    g_free(v);
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    g_object_set_data(G_OBJECT(e), "key", (gpointer)key);
    g_signal_connect(e, "changed", G_CALLBACK(on_entry), a);
    gtk_box_append(GTK_BOX(box), e);
    gtk_box_append(GTK_BOX(card), box);
    return e;
}

/* ---- memory ---- */
static void mem_label(app_t *a, int mb)
{
    int total = total_ram_gb();
    char *t = mb % 1024 ? g_strdup_printf("%.1f GB", mb / 1024.0) : g_strdup_printf("%d GB", mb / 1024);
    gtk_label_set_text(GTK_LABEL(a->set_mem_label), t);
    g_free(t);
    if (mb > total * 1024 * 3 / 4)
        gtk_widget_add_css_class(a->set_mem_label, "warn");
    else
        gtk_widget_remove_css_class(a->set_mem_label, "warn");
}

static void on_mem(GtkRange *r, gpointer data)
{
    app_t *a = data;
    int mb = (int)lround(gtk_range_get_value(r) * 2) * 512;   /* half-GB steps */
    mc_cfg_set_int("memory_mb", mb);
    mem_label(a, mb);
}

static void on_open_prism(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    const char *args[] = {"--show-window", NULL};
    mc_sys_run_prism(args, NULL);
}

static void on_open_inst(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    mc_open_path(a, a->mcdir);
}

static void on_open_logs(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    char *d = g_build_filename(a->mcdir, "logs", NULL);
    mc_open_path(a, d);
    g_free(d);
}

static void on_open_shots(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    char *d = g_build_filename(a->mcdir, "screenshots", NULL);
    g_mkdir_with_parents(d, 0755);
    mc_open_path(a, d);
    g_free(d);
}

/* one section of the settings sub-menu: cards in two columns, scrolling */
static GtkWidget *section(GtkWidget *stack, const char *id, const char *title, GtkWidget **c1, GtkWidget **c2)
{
    GtkWidget *cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(cols), TRUE);
    *c1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    *c2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_box_append(GTK_BOX(cols), *c1);
    gtk_box_append(GTK_BOX(cols), *c2);
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), cols);
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_widget_set_hexpand(sc, TRUE);
    gtk_stack_add_titled(GTK_STACK(stack), sc, id, title);
    return sc;
}

GtkWidget *mc_page_settings(app_t *a)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(page, "mcl-page");
    gtk_box_append(GTK_BOX(page), mc_label("Settings", "mcl-title"));
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_vexpand(row, TRUE);
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 160);
    GtkWidget *side = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(side), GTK_STACK(stack));
    gtk_widget_add_css_class(side, "mcl-subnav");
    gtk_widget_set_size_request(side, 180, -1);
    gtk_box_append(GTK_BOX(row), side);
    gtk_box_append(GTK_BOX(row), stack);
    gtk_box_append(GTK_BOX(page), row);
    GtkWidget *c1, *c2;

    /* game */
    section(stack, "game", "Game", &c1, &c2);
    int total = total_ram_gb();
    char *ms = g_strdup_printf("How much RAM Java may take. This computer has %d GB.", total);
    GtkWidget *mem = mc_card("Memory", ms);
    g_free(ms);
    GtkWidget *mr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    a->set_mem = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, total, 0.5);
    gtk_scale_set_draw_value(GTK_SCALE(a->set_mem), FALSE);
    gtk_widget_set_hexpand(a->set_mem, TRUE);
    int mark = total > 48 ? 16 : total > 24 ? 8 : 4;
    for (int g = mark; g <= total; g += mark)
        gtk_scale_add_mark(GTK_SCALE(a->set_mem), g, GTK_POS_BOTTOM, NULL);
    a->set_mem_label = mc_label("", "mcl-h2");
    gtk_label_set_width_chars(GTK_LABEL(a->set_mem_label), 7);
    int mb = mc_cfg_int("memory_mb", mc_cfg_int("memory_gb", 10) * 1024);
    gtk_range_set_value(GTK_RANGE(a->set_mem), MIN(mb, total * 1024) / 1024.0);
    mem_label(a, mb);
    g_signal_connect(a->set_mem, "value-changed", G_CALLBACK(on_mem), a);
    gtk_box_append(GTK_BOX(mr), a->set_mem);
    gtk_box_append(GTK_BOX(mr), a->set_mem_label);
    gtk_box_append(GTK_BOX(mem), mr);
    spin_row(a, mem, "memory_min_mb", 4096, 512, total * 1024, 512, "Start with (MB)",
             "memory taken right away; the rest grows as needed");
    gtk_box_append(GTK_BOX(c1), mem);

    GtkWidget *win = mc_card("Game window", NULL);
    a->set_fullscreen = set_row(a, win, "fullscreen", false, "Fullscreen", "F11 still switches in game");
    set_row(a, win, "maximized", false, "Start maximized", NULL);
    spin_row(a, win, "win_w", 1280, 320, 7680, 10, "Width", NULL);
    spin_row(a, win, "win_h", 720, 240, 4320, 10, "Height", NULL);
    gtk_box_append(GTK_BOX(c2), win);

    /* performance */
    section(stack, "speed", "Performance", &c1, &c2);
    GtkWidget *speed = mc_card("Speed", "None of these lower the picture quality.");
    if (MC_LINUX) {                      /* Mesa, gamemode and Wayland are Linux things */
        a->set_glthread = set_row(a, speed, "glthread", true, "GPU threading",
                                  "mesa_glthread: OpenGL work on its own thread (Mesa drivers only)");
        a->set_gamemode = set_row(a, speed, "gamemode", true, "Gamemode",
                                  "performance CPU governor while playing (needs gamemoderun)");
    }
    a->set_zgc = set_row(a, speed, "zgc", true, "Low-pause garbage collector",
                         "ZGC: no stutter when Java cleans up memory. Off = G1");
    if (MC_LINUX)
        a->set_wayland = set_row(a, speed, "wayland", true, "Native Wayland",
                                 "run without XWayland (SDL_VIDEO_DRIVER=wayland)");
    gtk_box_append(GTK_BOX(c1), speed);

    GtkWidget *adv = mc_card("Advanced", "Added to what the switches above set.");
    entry_row(a, adv, "jvm_args", "Java arguments", NULL, "-Dfoo=bar -XX:…");
    if (MC_LINUX)
        entry_row(a, adv, "env", "Environment", NULL, "VAR=value VAR2=value");
    entry_row(a, adv, "wrapper", "Wrapper command", "runs the game through it, e.g. mangohud", "mangohud");
    gtk_box_append(GTK_BOX(c2), adv);

    /* launcher */
    section(stack, "launcher", "Launcher", &c1, &c2);
    GtkWidget *beh = mc_card("Launcher", NULL);
    a->set_close = set_row(a, beh, "close_on_start", false, "Close when the game is up", NULL);
    set_row(a, beh, "console", false, "Prism console", "show Prism's log window while playing");
    set_row(a, beh, "console_on_error", true, "Console on a crash", "open the log when the game crashes");
    look_row(a, beh, "show_stats", true, "Stats on Home", "version, mod count, time played");
    look_row(a, beh, "page_anim", true, "Page animations", NULL);
    gtk_box_append(GTK_BOX(c1), beh);

    GtkWidget *fold = mc_card("Folders", NULL);
    GtkWidget *br = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(br), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(br), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(br), 6);
    struct { const char *l; GCallback cb; } bs[] = {
        {"Game folder", G_CALLBACK(on_open_inst)}, {"Screenshots", G_CALLBACK(on_open_shots)},
        {"Logs", G_CALLBACK(on_open_logs)},        {"Open Prism", G_CALLBACK(on_open_prism)},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(bs); i++) {
        GtkWidget *b = gtk_button_new_with_label(bs[i].l);
        g_signal_connect(b, "clicked", bs[i].cb, a);
        gtk_flow_box_append(GTK_FLOW_BOX(br), b);
    }
    gtk_box_append(GTK_BOX(fold), br);
    gtk_box_append(GTK_BOX(c2), fold);

    /* looks */
    section(stack, "looks", "Looks", &c1, &c2);
    GtkWidget *looks = mc_card("Background", NULL);
    GtkWidget *bl = spin_row(a, looks, "bg_blur", 6, 0, 30, 1, "Wallpaper blur", NULL);
    g_object_set_data(G_OBJECT(bl), "looks", GINT_TO_POINTER(1));
    look_row(a, looks, "bg_motion", true, "Wallpaper drift", "slow zoom and pan");
    look_row(a, looks, "bg_dust", true, "Floating dust", NULL);
    gtk_box_append(GTK_BOX(c1), looks);
    GtkWidget *model = mc_card("Player model", NULL);
    look_row(a, model, "model_turn", true, "Player turns by itself", "when you leave it alone");
    look_row(a, model, "model_idle", true, "Player breathes", "idle sway and cape swing");
    gtk_box_append(GTK_BOX(c2), model);

    /* themes: one wide column */
    section(stack, "themes", "Themes", &c1, &c2);
    gtk_box_set_homogeneous(GTK_BOX(gtk_widget_get_parent(c1)), FALSE);
    gtk_widget_set_hexpand(c1, TRUE);
    gtk_widget_set_visible(c2, FALSE);
    gtk_box_append(GTK_BOX(c1), mc_themes_card(a));

    /* updates */
    section(stack, "updates", "Updates", &c1, &c2);
    gtk_box_append(GTK_BOX(c1), mc_update_card(a));

    const char *sub = g_getenv("TUX_MC_SETTINGS");          /* screenshots */
    if (sub)
        gtk_stack_set_visible_child_name(GTK_STACK(stack), sub);
    return page;
}
