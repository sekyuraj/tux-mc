/* mc-instances.c — the Instances page: every Prism instance, which one Start
 * plays, and new ones with any Minecraft version and loader.
 *
 * An instance is a folder in <prism>/instances with instance.cfg (its name)
 * and mmc-pack.json (Minecraft + loader). We write a short mmc-pack.json with
 * just the main components; Prism adds what they need (LWJGL and the like)
 * and downloads the game on the first start. Versions come from Prism's own
 * metadata server, so the lists match what Prism can install.
 */
#include "mc.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define META "https://meta.prismlauncher.org/v1/"

static const struct { const char *slug, *name, *uid; } LOADERS[] = {
    {"", "Vanilla", NULL},
    {"fabric", "Fabric", "net.fabricmc.fabric-loader"},
    {"quilt", "Quilt", "org.quiltmc.quilt-loader"},
    {"neoforge", "NeoForge", "net.neoforged"},
    {"forge", "Forge", "net.minecraftforge"},
};

/* --------------------------------------------------------- mmc-pack.json -- */

void mc_inst_read_pack(const char *instdir, char **mc, char **loader, char **loader_ver)
{
    *mc = NULL;
    *loader = g_strdup("");
    if (loader_ver)
        *loader_ver = NULL;
    char *path = g_build_filename(instdir, "mmc-pack.json", NULL);
    JsonParser *p = json_parser_new();
    if (json_parser_load_from_file(p, path, NULL)) {
        JsonNode *r = json_parser_get_root(p);
        JsonObject *o = r && JSON_NODE_HOLDS_OBJECT(r) ? json_node_get_object(r) : NULL;
        JsonArray *cs = o && json_object_has_member(o, "components") ? json_object_get_array_member(o, "components")
                                                                      : NULL;
        for (guint i = 0; cs && i < json_array_get_length(cs); i++) {
            JsonObject *c = json_array_get_object_element(cs, i);
            const char *uid = c ? json_object_get_string_member_with_default(c, "uid", "") : "";
            const char *ver = c ? json_object_get_string_member_with_default(c, "version", NULL) : NULL;
            if (!ver)
                ver = c ? json_object_get_string_member_with_default(c, "cachedVersion", NULL) : NULL;
            if (!strcmp(uid, "net.minecraft")) {
                g_free(*mc);
                *mc = g_strdup(ver);
            }
            for (size_t k = 1; k < G_N_ELEMENTS(LOADERS); k++)
                if (!strcmp(uid, LOADERS[k].uid)) {
                    g_free(*loader);
                    *loader = g_strdup(LOADERS[k].slug);
                    if (loader_ver) {
                        g_free(*loader_ver);
                        *loader_ver = g_strdup(ver);
                    }
                }
        }
    }
    g_object_unref(p);
    g_free(path);
}

const char *mc_loader_name(const char *slug)
{
    for (size_t k = 0; k < G_N_ELEMENTS(LOADERS); k++)
        if (!g_strcmp0(slug, LOADERS[k].slug))
            return LOADERS[k].name;
    return "Vanilla";
}

static char *cfg_value(const char *instdir, const char *key)
{
    char *p = g_build_filename(instdir, "instance.cfg", NULL), *txt = NULL, *res = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **l = g_strsplit(txt, "\n", -1);
        char *pre = g_strconcat(key, "=", NULL);
        for (char **q = l; *q && !res; q++)
            if (g_str_has_prefix(*q, pre))
                res = g_strdup(g_strchomp(*q + strlen(pre)));
        g_free(pre);
        g_strfreev(l);
        g_free(txt);
    }
    g_free(p);
    return res;
}

static int count_jars(const char *instdir)
{
    char *dir = g_build_filename(instdir, "minecraft", "mods", NULL);
    GDir *d = g_dir_open(dir, 0, NULL);
    int n = 0;
    for (const char *e; d && (e = g_dir_read_name(d));)
        n += g_str_has_suffix(e, ".jar");
    if (d)
        g_dir_close(d);
    g_free(dir);
    return n;
}

/* ------------------------------------------------------------------ list -- */

typedef struct {
    char *id, *name, *mc, *loader, *loader_ver;
    int mods;
    long played;
} inst_t;

static void inst_free(inst_t *i)
{
    g_free(i->id);
    g_free(i->name);
    g_free(i->mc);
    g_free(i->loader);
    g_free(i->loader_ver);
    g_free(i);
}

static int inst_cmp(gconstpointer x, gconstpointer y)
{
    const inst_t *a = *(inst_t *const *)x, *b = *(inst_t *const *)y;
    return g_utf8_collate(a->name, b->name);
}

static GPtrArray *scan(app_t *a)
{
    GPtrArray *all = g_ptr_array_new_with_free_func((GDestroyNotify)inst_free);
    char *root = g_build_filename(a->prism, "instances", NULL);
    GDir *d = g_dir_open(root, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        if (*e == '.' || *e == '_')                 /* Prism's .tmp and _MMC_TEMP */
            continue;
        char *dir = g_build_filename(root, e, NULL), *cfg = g_build_filename(dir, "instance.cfg", NULL);
        if (g_file_test(cfg, G_FILE_TEST_EXISTS)) {
            inst_t *i = g_new0(inst_t, 1);
            i->id = g_strdup(e);
            i->name = cfg_value(dir, "name");
            if (!i->name || !*i->name) {
                g_free(i->name);
                i->name = g_strdup(e);
            }
            mc_inst_read_pack(dir, &i->mc, &i->loader, &i->loader_ver);
            i->mods = count_jars(dir);
            char *t = cfg_value(dir, "totalTimePlayed");
            i->played = t ? atol(t) : 0;
            g_free(t);
            g_ptr_array_add(all, i);
        }
        g_free(cfg);
        g_free(dir);
    }
    if (d)
        g_dir_close(d);
    g_free(root);
    g_ptr_array_sort(all, inst_cmp);
    return all;
}

/* ---------------------------------------------------------------- switch -- */

static void use_instance(app_t *a, const char *id)
{
    if (a->launching) {
        mc_status(a, "Wait until the game is up, then switch instances");
        return;
    }
    mc_cfg_set_str("instance", id);
    g_free(a->instance);
    a->instance = g_strdup(id);
    mc_reload_window(a, "instances");
    mc_status(a, "Start now plays “%s”", id);
}

static void on_use(GtkButton *b, gpointer data)
{
    use_instance(data, g_object_get_data(G_OBJECT(b), "id"));
}

static void on_folder(GtkButton *b, gpointer data)
{
    app_t *a = data;
    char *dir = g_build_filename(a->prism, "instances", (char *)g_object_get_data(G_OBJECT(b), "id"), NULL);
    mc_open_path(a, dir);
    g_free(dir);
}

static gboolean unarm(gpointer b)
{
    if (g_object_get_data(b, "armed")) {
        g_object_set_data(b, "armed", NULL);
        gtk_button_set_label(b, "Delete");
        gtk_widget_remove_css_class(b, "mcl-danger");
    }
    g_object_unref(b);
    return G_SOURCE_REMOVE;
}

/* the first click arms the button, a second one within 4 s deletes (to the trash) */
static void on_delete(GtkButton *b, gpointer data)
{
    app_t *a = data;
    const char *id = g_object_get_data(G_OBJECT(b), "id");
    if (!g_object_get_data(G_OBJECT(b), "armed")) {
        g_object_set_data(G_OBJECT(b), "armed", GINT_TO_POINTER(1));
        gtk_button_set_label(b, "Sure?");
        gtk_widget_add_css_class(GTK_WIDGET(b), "mcl-danger");
        g_timeout_add_seconds(4, unarm, g_object_ref(b));
        return;
    }
    if (mc_sys_game_running(g_object_get_data(G_OBJECT(b), "dir"))) {
        mc_status(a, "That instance is running");
        return;
    }
    GFile *f = g_file_new_for_path(g_object_get_data(G_OBJECT(b), "dir"));
    GError *e = NULL;
    if (g_file_trash(f, NULL, &e)) {
        mc_status(a, "“%s” is in the trash", id);
        if (!strcmp(id, a->instance)) {         /* deleted the one Start plays */
            g_free(a->instance);
            GPtrArray *left = scan(a);
            a->instance = g_strdup(left->len ? ((inst_t *)left->pdata[0])->id : "26.3");
            g_ptr_array_unref(left);
            mc_cfg_set_str("instance", a->instance);
            mc_reload_window(a, "instances");
        } else {
            mc_instances_refresh(a);
        }
    } else {
        mc_status(a, "Couldn't delete it: %s", e->message);
        g_error_free(e);
    }
    g_object_unref(f);
}

static GtkWidget *small_button(const char *label, GCallback cb, app_t *a, const char *id, const char *dir)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(b), "id", g_strdup(id), g_free);
    if (dir)
        g_object_set_data_full(G_OBJECT(b), "dir", g_strdup(dir), g_free);
    g_signal_connect(b, "clicked", cb, a);
    return b;
}

static GtkWidget *inst_row(app_t *a, inst_t *i)
{
    bool current = !strcmp(i->id, a->instance);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "mcl-row");
    if (current)
        gtk_widget_add_css_class(row, "mcl-current");

    GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(t), mc_label(i->name, "mcl-h2"));
    GString *s = g_string_new(i->mc ? i->mc : "unknown version");
    g_string_append_printf(s, "  ·  %s", mc_loader_name(i->loader));
    if (i->loader_ver)
        g_string_append_printf(s, " %s", i->loader_ver);
    if (*i->loader)
        g_string_append_printf(s, "  ·  %d mod%s", i->mods, i->mods == 1 ? "" : "s");
    if (i->played)
        g_string_append_printf(s, "  ·  %ldh %02ldm", i->played / 3600, i->played / 60 % 60);
    else
        g_string_append(s, "  ·  not played yet");
    gtk_box_append(GTK_BOX(t), mc_label(s->str, "dim"));
    g_string_free(s, TRUE);
    gtk_box_append(GTK_BOX(row), t);

    char *dir = g_build_filename(a->prism, "instances", i->id, NULL);
    if (current) {
        GtkWidget *l = mc_label("Start plays this", "dim");
        gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), l);
    } else {
        GtkWidget *use = small_button("Use", G_CALLBACK(on_use), a, i->id, NULL);
        gtk_widget_add_css_class(use, "suggested-action");
        gtk_box_append(GTK_BOX(row), use);
    }
    gtk_box_append(GTK_BOX(row), small_button("Folder", G_CALLBACK(on_folder), a, i->id, NULL));
    gtk_box_append(GTK_BOX(row), small_button("Delete", G_CALLBACK(on_delete), a, i->id, dir));
    g_free(dir);
    return row;
}

void mc_instances_refresh(app_t *a)
{
    if (!a->inst_list)
        return;
    for (GtkWidget *c; (c = gtk_widget_get_first_child(a->inst_list));)
        gtk_box_remove(GTK_BOX(a->inst_list), c);
    GPtrArray *all = scan(a);
    for (guint k = 0; k < all->len; k++)
        gtk_box_append(GTK_BOX(a->inst_list), inst_row(a, all->pdata[k]));
    if (!all->len)
        gtk_box_append(GTK_BOX(a->inst_list), mc_label("No instances yet. Make one on the right.", "dim"));
    g_ptr_array_unref(all);
}

/* ------------------------------------------------------------- versions -- */

/* Prism's metadata for one component: the "versions" array, newest first */
static JsonParser *meta(const char *uid, JsonArray **vs)
{
    char *url = g_strconcat(META, uid, "/index.json", NULL);
    GByteArray *b = g_byte_array_new();
    long code = mc_http(NULL, url, NULL, NULL, NULL, b);
    g_free(url);
    JsonParser *p = json_parser_new();
    *vs = NULL;
    if (code == 200 && json_parser_load_from_data(p, (char *)b->data, b->len, NULL)) {
        JsonNode *r = json_parser_get_root(p);
        JsonObject *o = r && JSON_NODE_HOLDS_OBJECT(r) ? json_node_get_object(r) : NULL;
        if (o && json_object_has_member(o, "versions"))
            *vs = json_object_get_array_member(o, "versions");
    }
    g_byte_array_unref(b);
    return p;
}

static void versions_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)data;
    (void)c;
    JsonArray *vs;
    JsonParser *p = meta("net.minecraft", &vs);
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; vs && i < json_array_get_length(vs); i++) {
        JsonObject *v = json_array_get_object_element(vs, i);
        const char *id = json_object_get_string_member_with_default(v, "version", NULL);
        const char *type = json_object_get_string_member_with_default(v, "type", "");
        if (id)                                  /* "s:" snapshots and old betas, "r:" releases */
            g_ptr_array_add(out, g_strconcat(!strcmp(type, "release") ? "r:" : "s:", id, NULL));
    }
    g_object_unref(p);
    if (out->len)
        g_task_return_pointer(t, out, (GDestroyNotify)g_ptr_array_unref);
    else {
        g_ptr_array_unref(out);
        g_task_return_new_error(t, G_IO_ERROR, G_IO_ERROR_FAILED, "no answer from meta.prismlauncher.org");
    }
}

static void fill_versions(app_t *a)
{
    GtkStringList *l = gtk_string_list_new(NULL);
    bool snaps = gtk_check_button_get_active(GTK_CHECK_BUTTON(a->inst_snaps));
    for (guint i = 0; a->mc_versions && i < a->mc_versions->len; i++) {
        const char *v = a->mc_versions->pdata[i];
        if (snaps || v[0] == 'r')
            gtk_string_list_append(l, v + 2);
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(a->inst_ver), G_LIST_MODEL(l));
    g_object_unref(l);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->inst_ver), 0);
}

static void versions_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    app_t *a = data;
    GError *e = NULL;
    GPtrArray *vs = g_task_propagate_pointer(G_TASK(r), &e);
    if (!a->inst_ver)
        goto out;
    if (!vs) {
        gtk_label_set_text(GTK_LABEL(a->inst_msg), "Couldn't get the version list. Check the internet and reopen.");
        goto out;
    }
    if (a->mc_versions)
        g_ptr_array_unref(a->mc_versions);
    a->mc_versions = g_ptr_array_ref(vs);
    fill_versions(a);
    gtk_widget_set_sensitive(a->inst_create, TRUE);
    gtk_label_set_text(GTK_LABEL(a->inst_msg), "");
out:
    if (vs)
        g_ptr_array_unref(vs);
    g_clear_error(&e);
}

static void on_snaps(GtkCheckButton *c, gpointer data)
{
    (void)c;
    fill_versions(data);
}

/* ---------------------------------------------------------------- create -- */

typedef struct {
    app_t *a;
    char *name, *id, *mc, *loader;
    bool fabric_api;
    char *err, *note;
} make_t;

static void make_free(make_t *m)
{
    g_free(m->name);
    g_free(m->id);
    g_free(m->mc);
    g_free(m->loader);
    g_free(m->err);
    g_free(m->note);
    g_free(m);
}

/* the loader version for this Minecraft: Fabric/Quilt work with any version
 * that has intermediary mappings; NeoForge/Forge builds name the one they need */
static char *pick_loader(const char *uid, const char *mc, char **err)
{
    JsonArray *vs;
    char *res = NULL;
    if (!strcmp(uid, "net.fabricmc.fabric-loader") || !strcmp(uid, "org.quiltmc.quilt-loader")) {
        JsonParser *im = meta("net.fabricmc.intermediary", &vs);
        bool mapped = false;
        for (guint i = 0; vs && i < json_array_get_length(vs) && !mapped; i++)
            mapped = !g_strcmp0(json_object_get_string_member_with_default(json_array_get_object_element(vs, i),
                                                                           "version", ""), mc);
        g_object_unref(im);
        if (!mapped) {
            *err = g_strdup_printf("%s has no %s yet", mc,
                                   strstr(uid, "quilt") ? "Quilt" : "Fabric");
            return NULL;
        }
        JsonParser *p = meta(uid, &vs);
        const char *first = NULL;              /* newest stable, the recommended one if marked */
        for (guint i = 0; vs && i < json_array_get_length(vs) && !res; i++) {
            JsonObject *v = json_array_get_object_element(vs, i);
            const char *ver = json_object_get_string_member_with_default(v, "version", NULL);
            if (!ver || g_strcmp0(json_object_get_string_member_with_default(v, "type", "release"), "release"))
                continue;
            if (!first)
                first = ver;
            if (json_object_get_boolean_member_with_default(v, "recommended", FALSE))
                res = g_strdup(ver);
        }
        if (!res && first)
            res = g_strdup(first);
        g_object_unref(p);
    } else {
        JsonParser *p = meta(uid, &vs);
        const char *first = NULL;
        for (guint i = 0; vs && i < json_array_get_length(vs) && !res; i++) {
            JsonObject *v = json_array_get_object_element(vs, i);
            JsonArray *rq = json_object_has_member(v, "requires") ? json_object_get_array_member(v, "requires") : NULL;
            bool fits = false;
            for (guint k = 0; rq && k < json_array_get_length(rq); k++) {
                JsonObject *q = json_array_get_object_element(rq, k);
                fits |= !g_strcmp0(json_object_get_string_member_with_default(q, "uid", ""), "net.minecraft") &&
                        !g_strcmp0(json_object_get_string_member_with_default(q, "equals", ""), mc);
            }
            if (!fits)
                continue;
            const char *ver = json_object_get_string_member_with_default(v, "version", NULL);
            if (!first)
                first = ver;
            if (json_object_get_boolean_member_with_default(v, "recommended", FALSE))
                res = g_strdup(ver);
        }
        if (!res && first)
            res = g_strdup(first);
        g_object_unref(p);
    }
    if (!res && !*err)
        *err = g_strdup_printf("no %s for %s", strstr(uid, "neoforged") ? "NeoForge" : "Forge", mc);
    return res;
}

static void add_component(JsonBuilder *jb, const char *uid, const char *ver, bool important, bool dep_only)
{
    json_builder_begin_object(jb);
    json_builder_set_member_name(jb, "uid");
    json_builder_add_string_value(jb, uid);
    json_builder_set_member_name(jb, "version");
    json_builder_add_string_value(jb, ver);
    if (important) {
        json_builder_set_member_name(jb, "important");
        json_builder_add_boolean_value(jb, TRUE);
    }
    if (dep_only) {
        json_builder_set_member_name(jb, "dependencyOnly");
        json_builder_add_boolean_value(jb, TRUE);
    }
    json_builder_end_object(jb);
}

/* a folder name Prism and every filesystem accept */
static char *folder_for(app_t *a, const char *name)
{
    GString *s = g_string_new(NULL);
    for (const char *p = name; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 32 || strchr("<>:\"/\\|?*", (int)(c < 128 ? c : 'x')) )
            g_string_append_c(s, '_');
        else
            g_string_append_unichar(s, c);
    }
    g_strstrip(s->str);
    s->len = strlen(s->str);
    while (s->len && (s->str[s->len - 1] == '.' || s->str[s->len - 1] == ' '))
        g_string_truncate(s, s->len - 1);
    if (!s->len || *s->str == '.' || *s->str == '_')
        g_string_prepend(s, "mc");
    char *base = g_string_free(s, FALSE), *id = g_strdup(base);
    for (int n = 2;; n++) {
        char *dir = g_build_filename(a->prism, "instances", id, NULL);
        bool taken = g_file_test(dir, G_FILE_TEST_EXISTS);
        g_free(dir);
        if (!taken)
            break;
        g_free(id);
        id = g_strdup_printf("%s (%d)", base, n);
    }
    g_free(base);
    return id;
}

static void make_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    make_t *m = data;
    app_t *a = m->a;
    const char *uid = NULL;
    for (size_t k = 0; k < G_N_ELEMENTS(LOADERS); k++)
        if (!strcmp(m->loader, LOADERS[k].slug))
            uid = LOADERS[k].uid;
    char *lver = uid ? pick_loader(uid, m->mc, &m->err) : NULL;
    if (uid && !lver) {
        g_task_return_boolean(t, FALSE);
        return;
    }

    JsonBuilder *jb = json_builder_new();
    json_builder_begin_object(jb);
    json_builder_set_member_name(jb, "components");
    json_builder_begin_array(jb);
    add_component(jb, "net.minecraft", m->mc, true, false);
    if (uid && (!strcmp(m->loader, "fabric") || !strcmp(m->loader, "quilt")))
        add_component(jb, "net.fabricmc.intermediary", m->mc, false, true);
    if (uid)
        add_component(jb, uid, lver, false, false);
    json_builder_end_array(jb);
    json_builder_set_member_name(jb, "formatVersion");
    json_builder_add_int_value(jb, 1);
    json_builder_end_object(jb);
    JsonGenerator *g = json_generator_new();
    json_generator_set_pretty(g, TRUE);
    JsonNode *root = json_builder_get_root(jb);
    json_generator_set_root(g, root);
    char *pack = json_generator_to_data(g, NULL);
    json_node_unref(root);
    g_object_unref(g);
    g_object_unref(jb);

    m->id = folder_for(a, m->name);
    char *dir = g_build_filename(a->prism, "instances", m->id, NULL);
    char *mods = g_build_filename(dir, "minecraft", "mods", NULL);
    char *pp = g_build_filename(dir, "mmc-pack.json", NULL), *cp = g_build_filename(dir, "instance.cfg", NULL);
    char *cfg = g_strdup_printf("[General]\nConfigVersion=1.2\nInstanceType=OneSix\nname=%s\niconKey=default\n",
                                m->name);
    bool ok = g_mkdir_with_parents(mods, 0755) == 0 && g_file_set_contents(pp, pack, -1, NULL) &&
              g_file_set_contents(cp, cfg, -1, NULL);
    if (!ok)
        m->err = g_strdup_printf("can't write to %s", dir);
    if (ok && m->fabric_api && uid && (!strcmp(m->loader, "fabric") || !strcmp(m->loader, "quilt"))) {
        char *e = NULL;
        if (!mc_fabric_api(mods, m->mc, &e))     /* not fatal: the instance is there */
            m->note = g_strdup_printf("Fabric API wasn't added (%s)", e ? e : "?");
        g_free(e);
    }
    g_free(cfg);
    g_free(pp);
    g_free(cp);
    g_free(mods);
    g_free(dir);
    g_free(pack);
    g_free(lver);
    g_task_return_boolean(t, ok);
}

static void make_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)data;
    make_t *m = g_task_get_task_data(G_TASK(r));
    app_t *a = m->a;
    bool ok = g_task_propagate_boolean(G_TASK(r), NULL);
    if (!a->inst_create)
        return;
    gtk_widget_set_sensitive(a->inst_create, TRUE);
    gtk_spinner_set_spinning(GTK_SPINNER(a->inst_spin), FALSE);
    if (!ok) {
        gtk_label_set_text(GTK_LABEL(a->inst_msg), m->err ? m->err : "Something went wrong");
        return;
    }
    char *msg = g_strdup_printf("Made “%s”. Prism downloads the game the first time you start it.%s%s",
                                m->name, m->note ? " " : "", m->note ? m->note : "");
    gtk_label_set_text(GTK_LABEL(a->inst_msg), msg);
    g_free(msg);
    gtk_editable_set_text(GTK_EDITABLE(a->inst_name), "");
    mc_instances_refresh(a);
}

static void on_create(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->inst_ver));
    if (!item) {
        gtk_label_set_text(GTK_LABEL(a->inst_msg), "Pick a Minecraft version");
        return;
    }
    const char *mc = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    guint li = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->inst_loader));
    if (li >= G_N_ELEMENTS(LOADERS))
        li = 0;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(a->inst_name));
    make_t *m = g_new0(make_t, 1);
    m->a = a;
    m->mc = g_strdup(mc);
    m->loader = g_strdup(LOADERS[li].slug);
    m->name = *g_strstrip(g_strdup(name)) ? g_strstrip(g_strdup(name))
                                          : g_strdup_printf("%s %s", mc, LOADERS[li].name);
    m->fabric_api = gtk_check_button_get_active(GTK_CHECK_BUTTON(a->inst_fapi));
    gtk_widget_set_sensitive(a->inst_create, FALSE);
    gtk_spinner_set_spinning(GTK_SPINNER(a->inst_spin), TRUE);
    gtk_label_set_text(GTK_LABEL(a->inst_msg), "Making it…");
    GTask *t = g_task_new(NULL, NULL, make_done, a);
    g_task_set_task_data(t, m, (GDestroyNotify)make_free);
    g_task_run_in_thread(t, make_thread);
    g_object_unref(t);
}

static void on_loader(GObject *dd, GParamSpec *ps, gpointer data)
{
    (void)ps;
    app_t *a = data;
    guint li = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
    gtk_widget_set_visible(a->inst_fapi, li == 1 || li == 2);
}

static void on_page_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    app_t *a = data;
    a->inst_list = a->inst_ver = a->inst_create = a->inst_msg = NULL;
}

static GtkWidget *form_row(GtkWidget *card, const char *title, GtkWidget *w)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "mcl-setrow");
    gtk_box_append(GTK_BOX(box), mc_label(title, NULL));
    gtk_box_append(GTK_BOX(box), w);
    gtk_box_append(GTK_BOX(card), box);
    return w;
}

GtkWidget *mc_page_instances(app_t *a)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(page, "mcl-page");
    g_signal_connect(page, "destroy", G_CALLBACK(on_page_destroy), a);
    gtk_box_append(GTK_BOX(page), mc_label("Instances", "mcl-title"));
    GtkWidget *cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_vexpand(cols, TRUE);

    /* the list */
    GtkWidget *left = mc_card(NULL, NULL);
    gtk_widget_set_hexpand(left, TRUE);
    gtk_box_append(GTK_BOX(left),
                   mc_label("Each instance has its own version, mods, worlds and settings. Start plays the one you "
                            "pick here.", "dim"));
    a->inst_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), a->inst_list);
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_box_append(GTK_BOX(left), sc);
    gtk_box_append(GTK_BOX(cols), left);

    /* a new one */
    GtkWidget *right = mc_card("New instance", NULL);
    gtk_widget_set_size_request(right, 360, -1);
    gtk_widget_set_valign(right, GTK_ALIGN_START);
    a->inst_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->inst_name), "e.g. Survival 1.21");
    form_row(right, "Name", a->inst_name);

    GtkStringList *none = gtk_string_list_new((const char *const[]){"loading…", NULL});
    a->inst_ver = gtk_drop_down_new(G_LIST_MODEL(none), NULL);
    gtk_drop_down_set_enable_search(GTK_DROP_DOWN(a->inst_ver), TRUE);
    GtkExpression *ex = gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string");
    gtk_drop_down_set_expression(GTK_DROP_DOWN(a->inst_ver), ex);
    gtk_expression_unref(ex);
    form_row(right, "Minecraft version", a->inst_ver);
    a->inst_snaps = gtk_check_button_new_with_label("Show snapshots and old versions");
    g_signal_connect(a->inst_snaps, "toggled", G_CALLBACK(on_snaps), a);
    gtk_box_append(GTK_BOX(right), a->inst_snaps);

    const char *names[G_N_ELEMENTS(LOADERS) + 1];
    for (size_t k = 0; k < G_N_ELEMENTS(LOADERS); k++)
        names[k] = LOADERS[k].name;
    names[G_N_ELEMENTS(LOADERS)] = NULL;
    a->inst_loader = gtk_drop_down_new_from_strings(names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->inst_loader), 1);
    g_signal_connect(a->inst_loader, "notify::selected", G_CALLBACK(on_loader), a);
    form_row(right, "Mod loader", a->inst_loader);
    a->inst_fapi = gtk_check_button_new_with_label("Add Fabric API (most mods need it)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(a->inst_fapi), TRUE);
    gtk_box_append(GTK_BOX(right), a->inst_fapi);

    GtkWidget *go = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(go, 8);
    a->inst_create = gtk_button_new_with_label("Create");
    gtk_widget_add_css_class(a->inst_create, "suggested-action");
    gtk_widget_set_sensitive(a->inst_create, FALSE);
    g_signal_connect(a->inst_create, "clicked", G_CALLBACK(on_create), a);
    a->inst_spin = gtk_spinner_new();
    gtk_box_append(GTK_BOX(go), a->inst_create);
    gtk_box_append(GTK_BOX(go), a->inst_spin);
    gtk_box_append(GTK_BOX(right), go);
    a->inst_msg = mc_label("Getting the version list…", "dim");
    gtk_label_set_wrap(GTK_LABEL(a->inst_msg), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(a->inst_msg), 30);   /* wrap, don't widen the card */
    gtk_label_set_xalign(GTK_LABEL(a->inst_msg), 0);
    gtk_box_append(GTK_BOX(right), a->inst_msg);
    gtk_box_append(GTK_BOX(cols), right);
    gtk_box_append(GTK_BOX(page), cols);

    mc_instances_refresh(a);
    if (a->mc_versions) {
        fill_versions(a);
        gtk_widget_set_sensitive(a->inst_create, TRUE);
        gtk_label_set_text(GTK_LABEL(a->inst_msg), "");
    } else {
        GTask *t = g_task_new(NULL, NULL, versions_done, a);
        g_task_run_in_thread(t, versions_thread);
        g_object_unref(t);
    }
    return page;
}

const char *mc_loader_slug(app_t *a)
{
    return a->loader && *a->loader ? a->loader : "fabric";
}
