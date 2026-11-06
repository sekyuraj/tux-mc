/* mc-import.c — the Import page: bring worlds, schematics, resource packs,
 * shader packs and screenshots into the current instance, from another
 * launcher's folder (the Minecraft Launcher, CurseForge, the Modrinth App,
 * other Prism instances, or any folder you pick) or from files (zips, .litematic
 * and friends, dropped or chosen).
 *
 * It only ever copies: nothing is moved or deleted where it comes from, and
 * nothing already in the instance is overwritten ("World (2)" instead). A
 * world is copied or unpacked into a hidden folder first and only gets its
 * real name once it's complete and has a level.dat. Free space is checked
 * before anything starts. */
#include "mc.h"

#include <glib/gstdio.h>
#include <string.h>

enum { K_WORLD, K_SCHEM, K_RPACK, K_SPACK, K_SHOT, K_MOD, NK };
static const struct { const char *title, *dest; } KIND[NK] = {
    {"Worlds", "saves"},        {"Schematics", "schematics"},   {"Resource packs", "resourcepacks"},
    {"Shader packs", "shaderpacks"}, {"Screenshots", "screenshots"}, {"Mods", "mods"},
};

typedef struct {
    char *name, *dir;                    /* dir: the game folder (saves/, resourcepacks/ ...) */
} src_t;

typedef struct {
    int kind;
    char *path, *label, *detail;
    guint64 size;
    GtkWidget *check;
} item_t;

static struct {
    app_t *a;
    GPtrArray *sources, *items;
    GtkWidget *dd, *list, *go, *bar, *msg, *page;
    bool busy;
    /* progress, written by the worker */
    gint64 total;
    gssize done;                         /* bytes copied, g_atomic_pointer_* */
    guint tick;
} I;

/* ---------------------------------------------------------------- helpers -- */

static void src_free(src_t *s)
{
    g_free(s->name);
    g_free(s->dir);
    g_free(s);
}

static void item_free(item_t *it)
{
    g_free(it->path);
    g_free(it->label);
    g_free(it->detail);
    g_free(it);
}

static guint64 tree_size(const char *path)
{
    GStatBuf st;
    if (g_lstat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode))
        return S_ISREG(st.st_mode) ? (guint64)st.st_size : 0;
    guint64 n = 0;
    GDir *d = g_dir_open(path, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        char *p = g_build_filename(path, e, NULL);
        n += tree_size(p);
        g_free(p);
    }
    if (d)
        g_dir_close(d);
    return n;
}

static char *when(const char *path)
{
    GStatBuf st;
    if (g_stat(path, &st) != 0)
        return g_strdup("");
    GDateTime *dt = g_date_time_new_from_unix_local(st.st_mtime);
    char *s = g_date_time_format(dt, "%e %b %Y");
    g_date_time_unref(dt);
    return g_strstrip(s);
}

static bool is_dir(const char *p) { return g_file_test(p, G_FILE_TEST_IS_DIR); }

static bool has_level(const char *dir)
{
    char *l = g_build_filename(dir, "level.dat", NULL);
    bool r = g_file_test(l, G_FILE_TEST_IS_REGULAR);
    g_free(l);
    return r;
}

static int kind_of_file(const char *name)
{
    char *l = g_ascii_strdown(name, -1);
    int k = -1;
    if (g_str_has_suffix(l, ".litematic") || g_str_has_suffix(l, ".schem") || g_str_has_suffix(l, ".schematic") ||
        g_str_has_suffix(l, ".nbt"))
        k = K_SCHEM;
    else if (g_str_has_suffix(l, ".png"))
        k = K_SHOT;
    else if (g_str_has_suffix(l, ".jar"))
        k = K_MOD;
    g_free(l);
    return k;
}

/* what a zip is: a world (prefix of its level.dat in *prefix), a shader pack, a resource pack, or -1 */
static int kind_of_zip(const char *zip, char **prefix)
{
    char **names = mc_zip_list(zip);
    if (!names)
        return -1;
    int k = -1;
    const char *best = NULL;
    for (char **n = names; *n; n++) {
        const char *base = strrchr(*n, '/') ? strrchr(*n, '/') + 1 : *n;
        if (!strcmp(base, "level.dat") && (!best || strlen(*n) < strlen(best)))
            best = *n;
    }
    if (best) {
        k = K_WORLD;
        if (prefix)
            *prefix = g_strndup(best, strlen(best) - strlen("level.dat"));
    } else {
        for (char **n = names; *n && k < 0; n++)
            if (g_str_has_prefix(*n, "shaders/") || strstr(*n, "/shaders/"))
                k = K_SPACK;
        for (char **n = names; *n && k < 0; n++)
            if (!strcmp(*n, "pack.mcmeta"))
                k = K_RPACK;
    }
    g_strfreev(names);
    return k;
}

/* a dropped folder: a world, an unzipped shader pack or resource pack, or -1 */
static int kind_of_dir(const char *dir)
{
    if (has_level(dir))
        return K_WORLD;
    char *sh = g_build_filename(dir, "shaders", NULL), *mm = g_build_filename(dir, "pack.mcmeta", NULL);
    int k = is_dir(sh) ? K_SPACK : g_file_test(mm, G_FILE_TEST_IS_REGULAR) ? K_RPACK : -1;
    g_free(sh);
    g_free(mm);
    return k;
}

/* "name", or "name (2)", "name (3)"... whatever is free in dir */
static char *free_name(const char *dir, const char *name, bool keep_ext)
{
    const char *dot = keep_ext ? strrchr(name, '.') : NULL;   /* "pack (2).zip" */
    char *stem = dot && dot != name ? g_strndup(name, dot - name) : g_strdup(name);
    const char *ext = dot && dot != name ? dot : "";
    char *cand = g_strdup(name);
    for (int n = 2;; n++) {
        char *p = g_build_filename(dir, cand, NULL);
        bool taken = g_file_test(p, G_FILE_TEST_EXISTS);
        g_free(p);
        if (!taken)
            break;
        g_free(cand);
        cand = g_strdup_printf("%s (%d)%s", stem, n, ext);
    }
    g_free(stem);
    return cand;
}

/* ---------------------------------------------------------------- sources -- */

static void add_src(GPtrArray *all, const char *name, const char *dir)
{
    if (!dir || !is_dir(dir))
        return;
    for (guint i = 0; i < all->len; i++)          /* the same folder twice */
        if (!g_strcmp0(((src_t *)all->pdata[i])->dir, dir))
            return;
    src_t *s = g_new0(src_t, 1);
    s->name = g_strdup(name);
    s->dir = g_strdup(dir);
    g_ptr_array_add(all, s);
}

/* every subfolder of root as "<label>: <name>", the game folder being root/<x>/<sub> */
static void add_profiles(GPtrArray *all, const char *label, const char *root, const char *sub, const char *skip)
{
    GDir *d = g_dir_open(root, 0, NULL);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    for (const char *e; d && (e = g_dir_read_name(d));)
        if (*e != '.' && *e != '_' && g_strcmp0(e, skip))
            g_ptr_array_add(names, g_strdup(e));
    if (d)
        g_dir_close(d);
    g_ptr_array_sort_values(names, (GCompareFunc)g_utf8_collate);
    for (guint i = 0; i < names->len; i++) {
        char *dir = sub ? g_build_filename(root, names->pdata[i], sub, NULL)
                        : g_build_filename(root, names->pdata[i], NULL);
        char *n = g_strdup_printf("%s: %s", label, (char *)names->pdata[i]);
        add_src(all, n, dir);
        g_free(n);
        g_free(dir);
    }
    g_ptr_array_unref(names);
}

static GPtrArray *find_sources(app_t *a)
{
    GPtrArray *all = g_ptr_array_new_with_free_func((GDestroyNotify)src_free);
    const char *home = g_get_home_dir();
#ifdef _WIN32
    const char *appdata = g_getenv("APPDATA");
    char *mc = appdata ? g_build_filename(appdata, ".minecraft", NULL) : NULL;
    add_src(all, "Minecraft Launcher", mc);
    g_free(mc);
    char *cf = g_build_filename(home, "curseforge", "minecraft", "Instances", NULL);
    add_profiles(all, "CurseForge", cf, NULL, NULL);
    g_free(cf);
    char *mr = appdata ? g_build_filename(appdata, "ModrinthApp", "profiles", NULL) : NULL;
    if (mr)
        add_profiles(all, "Modrinth", mr, NULL, NULL);
    g_free(mr);
#else
    char *mc = g_build_filename(home, ".minecraft", NULL);
    add_src(all, "Minecraft Launcher", mc);
    g_free(mc);
    char *fp = g_build_filename(home, ".var", "app", "com.mojang.Minecraft", ".minecraft", NULL);
    add_src(all, "Minecraft Launcher (Flatpak)", fp);
    g_free(fp);
    char *mr = g_build_filename(g_get_user_data_dir(), "ModrinthApp", "profiles", NULL);
    add_profiles(all, "Modrinth", mr, NULL, NULL);
    g_free(mr);
    char *cf = g_build_filename(home, "curseforge", "minecraft", "Instances", NULL);
    add_profiles(all, "CurseForge", cf, NULL, NULL);
    g_free(cf);
#endif
    char *pi = g_build_filename(a->prism, "instances", NULL);
    add_profiles(all, "Prism", pi, "minecraft", a->instance);
    add_profiles(all, "Prism", pi, ".minecraft", a->instance);
    g_free(pi);
    return all;
}

/* ------------------------------------------------------------------ items -- */

static item_t *new_item(int kind, const char *path, const char *label)
{
    item_t *it = g_new0(item_t, 1);
    it->kind = kind;
    it->path = g_strdup(path);
    it->label = g_strdup(label);
    it->size = tree_size(path);
    char *sz = g_format_size(it->size), *w = when(path);
    it->detail = g_strdup_printf("%s  ·  %s", sz, w);
    g_free(sz);
    g_free(w);
    return it;
}

/* what one folder has; scanned in a thread (sizes of big worlds take a moment) */
static void scan_dir(GPtrArray *out, const char *game)
{
    static const struct { int kind; const char *sub; } WHERE[] = {
        {K_WORLD, "saves"},          {K_SCHEM, "schematics"},       {K_SCHEM, "config/worldedit/schematics"},
        {K_RPACK, "resourcepacks"},  {K_SPACK, "shaderpacks"},      {K_SHOT, "screenshots"},
    };
    for (size_t w = 0; w < G_N_ELEMENTS(WHERE); w++) {
        char *dir = g_build_filename(game, WHERE[w].sub, NULL);
        GDir *d = g_dir_open(dir, 0, NULL);
        GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
        for (const char *e; d && (e = g_dir_read_name(d));)
            if (*e != '.')
                g_ptr_array_add(names, g_strdup(e));
        if (d)
            g_dir_close(d);
        g_ptr_array_sort_values(names, (GCompareFunc)g_utf8_collate);
        for (guint i = 0; i < names->len; i++) {
            const char *e = names->pdata[i];
            char *p = g_build_filename(dir, e, NULL);
            int k = WHERE[w].kind;
            bool ok = k == K_WORLD ? is_dir(p) && has_level(p)
                    : k == K_SCHEM ? kind_of_file(e) == K_SCHEM
                    : k == K_SHOT ? kind_of_file(e) == K_SHOT
                    : (is_dir(p) || g_str_has_suffix(e, ".zip"));
            if (ok)
                g_ptr_array_add(out, new_item(k, p, e));
            g_free(p);
        }
        g_ptr_array_unref(names);
        g_free(dir);
    }
}

static void scan_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)item_free);
    scan_dir(out, data);
    g_task_return_pointer(t, out, (GDestroyNotify)g_ptr_array_unref);
}

static void update_go(void)
{
    int n = 0;
    guint64 bytes = 0;
    for (guint i = 0; I.items && i < I.items->len; i++) {
        item_t *it = I.items->pdata[i];
        if (it->check && gtk_check_button_get_active(GTK_CHECK_BUTTON(it->check))) {
            n++;
            bytes += it->size;
        }
    }
    char *sz = g_format_size(bytes);
    char *l = n ? g_strdup_printf("Copy %d item%s (%s) into “%s”", n, n == 1 ? "" : "s", sz, I.a->instance)
                : g_strdup("Tick what you want to bring over");
    gtk_button_set_label(GTK_BUTTON(I.go), l);
    gtk_widget_set_sensitive(I.go, n > 0 && !I.busy);
    g_free(l);
    g_free(sz);
}

static void on_check(GtkCheckButton *c, gpointer d)
{
    (void)c;
    (void)d;
    update_go();
}

static void on_all(GtkCheckButton *c, gpointer data)
{
    int kind = GPOINTER_TO_INT(data);
    bool on = gtk_check_button_get_active(c);
    for (guint i = 0; I.items && i < I.items->len; i++) {
        item_t *it = I.items->pdata[i];
        if (it->kind == kind && it->check)
            gtk_check_button_set_active(GTK_CHECK_BUTTON(it->check), on);
    }
}

static void show_items(void)
{
    for (GtkWidget *c; (c = gtk_widget_get_first_child(I.list));)
        gtk_box_remove(GTK_BOX(I.list), c);
    int shown = 0;
    for (int k = 0; k < NK; k++) {
        int n = 0;
        for (guint i = 0; i < I.items->len; i++)
            n += ((item_t *)I.items->pdata[i])->kind == k;
        if (!n)
            continue;
        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_top(h, shown ? 14 : 0);
        char *t = g_strdup_printf("%s  (%d)", KIND[k].title, n);
        GtkWidget *hl = mc_label(t, "mcl-h2");
        g_free(t);
        gtk_widget_set_hexpand(hl, TRUE);
        gtk_label_set_xalign(GTK_LABEL(hl), 0);
        GtkWidget *all = gtk_check_button_new_with_label("all");
        g_signal_connect(all, "toggled", G_CALLBACK(on_all), GINT_TO_POINTER(k));
        gtk_box_append(GTK_BOX(h), hl);
        gtk_box_append(GTK_BOX(h), all);
        gtk_box_append(GTK_BOX(I.list), h);
        for (guint i = 0; i < I.items->len; i++) {
            item_t *it = I.items->pdata[i];
            if (it->kind != k)
                continue;
            /* the whole row is the check button, so clicking anywhere on it ticks it */
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            GtkWidget *nl = mc_label(it->label, NULL);
            gtk_label_set_ellipsize(GTK_LABEL(nl), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(nl), 0);
            gtk_widget_set_hexpand(nl, TRUE);
            GtkWidget *dl = mc_label(it->detail, "dim");
            gtk_box_append(GTK_BOX(row), nl);
            gtk_box_append(GTK_BOX(row), dl);
            it->check = gtk_check_button_new();
            gtk_check_button_set_child(GTK_CHECK_BUTTON(it->check), row);
            gtk_widget_add_css_class(it->check, "mcl-row");
            g_signal_connect(it->check, "toggled", G_CALLBACK(on_check), NULL);
            gtk_box_append(GTK_BOX(I.list), it->check);
        }
        shown++;
    }
    if (!shown)
        gtk_box_append(GTK_BOX(I.list), mc_label("Nothing to bring over from there.", "dim"));
    update_go();
}

static void scan_done(GObject *s, GAsyncResult *r, gpointer d)
{
    (void)s;
    (void)d;
    GPtrArray *items = g_task_propagate_pointer(G_TASK(r), NULL);
    if (!I.list || !items) {
        if (items)
            g_ptr_array_unref(items);
        return;
    }
    if (I.items)
        g_ptr_array_unref(I.items);
    I.items = items;
    show_items();
}

static void scan_source(const char *dir)
{
    for (GtkWidget *c; (c = gtk_widget_get_first_child(I.list));)
        gtk_box_remove(GTK_BOX(I.list), c);
    gtk_box_append(GTK_BOX(I.list), mc_label("Looking…", "dim"));
    if (I.items)
        g_ptr_array_set_size(I.items, 0);
    update_go();
    GTask *t = g_task_new(NULL, NULL, scan_done, NULL);
    g_task_set_task_data(t, g_strdup(dir), g_free);
    g_task_run_in_thread(t, scan_thread);
    g_object_unref(t);
}

static void on_source(GObject *dd, GParamSpec *ps, gpointer d)
{
    (void)ps;
    (void)d;
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
    if (I.sources && i < I.sources->len)
        scan_source(((src_t *)I.sources->pdata[i])->dir);
}

/* ----------------------------------------------------------------- copying -- */

typedef struct {
    int kind;
    char *path;
} job_t;

typedef struct {
    app_t *a;
    char *mcdir;
    GPtrArray *jobs;                     /* job_t */
    GString *report;
    int ok, failed;
} work_t;

static void job_free(job_t *j)
{
    g_free(j->path);
    g_free(j);
}

static void work_free(work_t *w)
{
    g_free(w->mcdir);
    g_ptr_array_unref(w->jobs);
    g_string_free(w->report, TRUE);
    g_free(w);
}

static bool copy_file(const char *from, const char *to, char **err)
{
    GFile *a = g_file_new_for_path(from), *b = g_file_new_for_path(to);
    GError *e = NULL;
    bool ok = g_file_copy(a, b, G_FILE_COPY_NONE, NULL, NULL, NULL, &e);
    if (!ok)
        *err = g_strdup(e->message);
    g_clear_error(&e);
    g_object_unref(a);
    g_object_unref(b);
    return ok;
}

/* a folder, file by file; session.lock stays behind (the source game may hold it) */
static bool copy_tree(const char *from, const char *to, char **err)
{
    if (g_mkdir_with_parents(to, 0755) != 0) {
        *err = g_strdup_printf("can't make %s", to);
        return false;
    }
    GDir *d = g_dir_open(from, 0, NULL);
    if (!d) {
        *err = g_strdup_printf("can't read %s", from);
        return false;
    }
    bool ok = true;
    for (const char *e; ok && (e = g_dir_read_name(d));) {
        if (!strcmp(e, "session.lock"))
            continue;
        char *a = g_build_filename(from, e, NULL), *b = g_build_filename(to, e, NULL);
        if (g_file_test(a, G_FILE_TEST_IS_SYMLINK)) {
            /* never follow links out of the folder */
        } else if (is_dir(a)) {
            ok = copy_tree(a, b, err);
        } else {
            ok = copy_file(a, b, err);
            GStatBuf st;
            if (ok && g_stat(a, &st) == 0)
                g_atomic_pointer_add(&I.done, (gssize)st.st_size);
        }
        g_free(a);
        g_free(b);
    }
    g_dir_close(d);
    return ok;
}

static void rm_tree(const char *p)
{
    if (is_dir(p) && !g_file_test(p, G_FILE_TEST_IS_SYMLINK)) {
        GDir *d = g_dir_open(p, 0, NULL);
        for (const char *e; d && (e = g_dir_read_name(d));) {
            char *c = g_build_filename(p, e, NULL);
            rm_tree(c);
            g_free(c);
        }
        if (d)
            g_dir_close(d);
        g_rmdir(p);
    } else {
        g_remove(p);
    }
}

static char *zip_stem(const char *path)
{
    char *b = g_path_get_basename(path);
    if (g_str_has_suffix(b, ".zip") || g_str_has_suffix(b, ".ZIP"))
        b[strlen(b) - 4] = 0;
    return b;
}

/* one thing; *where gets the name it ended up as */
static bool import_one(work_t *w, job_t *j, char **where, char **err)
{
    bool zip = g_str_has_suffix(j->path, ".zip") || g_str_has_suffix(j->path, ".ZIP");
    char *prefix = NULL;
    int kind = j->kind;
    if (kind < 0 && zip)
        kind = kind_of_zip(j->path, &prefix);
    else if (kind < 0 && is_dir(j->path))
        kind = kind_of_dir(j->path);
    else if (kind < 0)
        kind = kind_of_file(j->path);
    if (kind < 0) {
        *err = g_strdup("not a world, schematic, pack, screenshot or mod");
        g_free(prefix);
        return false;
    }
    char *destdir = g_build_filename(w->mcdir, KIND[kind].dest, NULL);
    g_mkdir_with_parents(destdir, 0755);
    bool ok;
    if (kind == K_WORLD) {
        /* into a hidden folder first; the real name only once it's all there */
        char *name;
        if (zip) {
            if (!prefix)
                kind_of_zip(j->path, &prefix);
            char *pn = prefix && *prefix ? g_strndup(prefix, strlen(prefix) - 1) : NULL;
            const char *slash = pn ? strrchr(pn, '/') : NULL;
            name = pn ? g_strdup(slash ? slash + 1 : pn) : zip_stem(j->path);
            g_free(pn);
        } else {
            name = g_path_get_basename(j->path);
        }
        char *tmp = g_build_filename(destdir, ".importing-world", NULL);
        rm_tree(tmp);
        ok = zip ? mc_zip_extract(j->path, prefix, tmp, err) : copy_tree(j->path, tmp, err);
        if (ok && !has_level(tmp)) {
            ok = false;
            *err = g_strdup("no level.dat in it: not a world");
        }
        if (ok) {
            char *final = free_name(destdir, name, false), *fp = g_build_filename(destdir, final, NULL);
            ok = g_rename(tmp, fp) == 0;
            if (ok)
                *where = g_strdup_printf("%s/%s", KIND[kind].dest, final);
            else
                *err = g_strdup("couldn't name the world folder");
            g_free(final);
            g_free(fp);
        }
        if (!ok)
            rm_tree(tmp);
        g_free(tmp);
        g_free(name);
    } else {
        char *base = g_path_get_basename(j->path);
        char *final = free_name(destdir, base, !is_dir(j->path)), *fp = g_build_filename(destdir, final, NULL);
        char *tmp = g_strconcat(fp, ".importing", NULL);
        ok = is_dir(j->path) ? copy_tree(j->path, tmp, err) : copy_file(j->path, tmp, err);
        if (ok && !is_dir(j->path))
            g_atomic_pointer_add(&I.done, (gssize)tree_size(j->path));
        if (ok)
            ok = g_rename(tmp, fp) == 0;
        if (ok)
            *where = g_strdup_printf("%s/%s", KIND[kind].dest, final);
        else
            rm_tree(tmp);
        g_free(tmp);
        g_free(fp);
        g_free(final);
        g_free(base);
    }
    if (zip && kind == K_WORLD)
        g_atomic_pointer_add(&I.done, (gssize)tree_size(j->path));
    g_free(destdir);
    g_free(prefix);
    return ok;
}

static void work_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    work_t *w = data;
    /* enough room? (zips count twice: they unpack bigger) */
    guint64 need = 0;
    for (guint i = 0; i < w->jobs->len; i++) {
        job_t *j = w->jobs->pdata[i];
        guint64 n = tree_size(j->path);
        need += g_str_has_suffix(j->path, ".zip") ? n * 2 : n;
    }
    I.total = (gint64)need;
    GFile *f = g_file_new_for_path(w->mcdir);
    GFileInfo *fi = g_file_query_filesystem_info(f, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, NULL);
    guint64 free_b = fi ? g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_FILESYSTEM_FREE) : G_MAXUINT64;
    if (fi)
        g_object_unref(fi);
    g_object_unref(f);
    if (free_b != 0 && need + 200u * 1024 * 1024 > free_b) {
        char *nb = g_format_size(need), *fb = g_format_size(free_b);
        g_string_append_printf(w->report, "Not enough free space: this needs about %s, the disk has %s.", nb, fb);
        g_free(nb);
        g_free(fb);
        w->failed = (int)w->jobs->len;
        g_task_return_boolean(t, FALSE);
        return;
    }
    for (guint i = 0; i < w->jobs->len; i++) {
        job_t *j = w->jobs->pdata[i];
        char *where = NULL, *err = NULL, *base = g_path_get_basename(j->path);
        if (import_one(w, j, &where, &err)) {
            w->ok++;
            g_string_append_printf(w->report, "✓ %s → %s\n", base, where);
        } else {
            w->failed++;
            g_string_append_printf(w->report, "✗ %s: %s\n", base, err ? err : "failed");
        }
        g_free(where);
        g_free(err);
        g_free(base);
    }
    g_task_return_boolean(t, w->failed == 0);
}

static gboolean progress_tick(gpointer d)
{
    (void)d;
    if (!I.bar)
        return G_SOURCE_REMOVE;
    double f = I.total > 0 ? (double)g_atomic_pointer_get(&I.done) / (double)I.total : 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(I.bar), MIN(1.0, f));
    return G_SOURCE_CONTINUE;
}

static void work_done(GObject *s, GAsyncResult *r, gpointer d)
{
    (void)s;
    (void)d;
    work_t *w = g_task_get_task_data(G_TASK(r));
    g_task_propagate_boolean(G_TASK(r), NULL);
    I.busy = false;
    if (I.tick)
        g_source_remove(I.tick);
    I.tick = 0;
    if (I.bar)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(I.bar), w->failed ? 0 : 1);
    char *head = w->failed ? g_strdup_printf("%d copied, %d didn't work:\n", w->ok, w->failed)
                           : g_strdup_printf("Done: %d copied. Nothing was changed where they came from.\n", w->ok);
    char *all = g_strconcat(head, w->report->str, NULL);
    if (I.msg)
        gtk_label_set_text(GTK_LABEL(I.msg), all);
    mc_status(w->a, "%s", head);
    g_free(head);
    g_free(all);
    if (I.go)
        update_go();
}

static void start(app_t *a, GPtrArray *jobs)
{
    if (I.busy || !jobs->len) {
        g_ptr_array_unref(jobs);
        return;
    }
    I.busy = true;
    I.total = 0;
    g_atomic_pointer_set(&I.done, 0);
    work_t *w = g_new0(work_t, 1);
    w->a = a;
    w->mcdir = g_strdup(a->mcdir);
    w->jobs = jobs;
    w->report = g_string_new(NULL);
    gtk_widget_set_sensitive(I.go, FALSE);
    gtk_label_set_text(GTK_LABEL(I.msg), "Copying…");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(I.bar), 0);
    I.tick = g_timeout_add(150, progress_tick, NULL);
    GTask *t = g_task_new(NULL, NULL, work_done, NULL);
    g_task_set_task_data(t, w, (GDestroyNotify)work_free);
    g_task_run_in_thread(t, work_thread);
    g_object_unref(t);
}

static void on_go(GtkButton *b, gpointer data)
{
    (void)b;
    GPtrArray *jobs = g_ptr_array_new_with_free_func((GDestroyNotify)job_free);
    for (guint i = 0; I.items && i < I.items->len; i++) {
        item_t *it = I.items->pdata[i];
        if (it->check && gtk_check_button_get_active(GTK_CHECK_BUTTON(it->check))) {
            job_t *j = g_new0(job_t, 1);
            j->kind = it->kind;
            j->path = g_strdup(it->path);
            g_ptr_array_add(jobs, j);
        }
    }
    start(data, jobs);
}

/* files from the chooser or dropped: each finds its own place */
static void import_files(app_t *a, GListModel *files)
{
    GPtrArray *jobs = g_ptr_array_new_with_free_func((GDestroyNotify)job_free);
    for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
        GFile *f = g_list_model_get_item(files, i);
        char *p = g_file_get_path(f);
        if (p) {
            job_t *j = g_new0(job_t, 1);
            j->kind = -1;
            j->path = p;
            g_ptr_array_add(jobs, j);
        }
        g_object_unref(f);
    }
    start(a, jobs);
}

static void on_files_done(GObject *src, GAsyncResult *res, gpointer data)
{
    GListModel *files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (files) {
        import_files(data, files);
        g_object_unref(files);
    }
}

static void on_files(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Worlds (zip), schematics, packs, mods");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Worlds, schematics, packs");
    const char *pats[] = {"*.zip", "*.litematic", "*.schem", "*.schematic", "*.nbt", "*.jar", "*.png", NULL};
    for (int i = 0; pats[i]; i++)
        gtk_file_filter_add_pattern(ff, pats[i]);
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(fl));
    gtk_file_dialog_open_multiple(d, a->win, NULL, on_files_done, a);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(d);
}

static void on_folder_done(GObject *src, GAsyncResult *res, gpointer data)
{
    app_t *a = data;
    GFile *f = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f)
        return;
    char *p = g_file_get_path(f);
    g_object_unref(f);
    if (!p)
        return;
    /* a world folder itself: copy it straight away */
    if (has_level(p)) {
        GPtrArray *jobs = g_ptr_array_new_with_free_func((GDestroyNotify)job_free);
        job_t *j = g_new0(job_t, 1);
        j->kind = K_WORLD;
        j->path = p;
        g_ptr_array_add(jobs, j);
        start(a, jobs);
        return;
    }
    /* a game folder (or its .minecraft / minecraft inside): add it as a source */
    char *game = g_strdup(p);
    for (const char *sub[] = {".minecraft", "minecraft", NULL}, **s = sub; *s; s++) {
        char *in = g_build_filename(p, *s, NULL);
        if (is_dir(in)) {
            g_free(game);
            game = in;
            break;
        }
        g_free(in);
    }
    char *base = g_path_get_basename(p);
    src_t *ns = g_new0(src_t, 1);
    ns->name = g_strdup_printf("Folder: %s", base);
    ns->dir = game;
    g_ptr_array_add(I.sources, ns);
    gtk_string_list_append(GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(I.dd))), ns->name);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(I.dd), I.sources->len - 1);
    g_free(base);
    g_free(p);
}

static void on_folder(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "A .minecraft folder, an instance, or one world");
    gtk_file_dialog_select_folder(d, a->win, NULL, on_folder_done, a);
    g_object_unref(d);
}

static gboolean on_drop(GtkDropTarget *t, const GValue *v, double x, double y, gpointer data)
{
    (void)t;
    (void)x;
    (void)y;
    if (!G_VALUE_HOLDS(v, GDK_TYPE_FILE_LIST))
        return FALSE;
    GListStore *store = g_list_store_new(G_TYPE_FILE);
    for (GSList *l = g_value_get_boxed(v); l; l = l->next)
        g_list_store_append(store, l->data);
    import_files(data, G_LIST_MODEL(store));
    g_object_unref(store);
    return TRUE;
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    (void)w;
    (void)d;
    I.list = I.go = I.bar = I.msg = I.dd = I.page = NULL;
}

GtkWidget *mc_page_import(app_t *a)
{
    I.a = a;
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    I.page = page;
    gtk_widget_add_css_class(page, "mcl-page");
    g_signal_connect(page, "destroy", G_CALLBACK(on_destroy), NULL);
    gtk_box_append(GTK_BOX(page), mc_label("Import", "mcl-title"));

    GtkWidget *cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_vexpand(cols, TRUE);

    /* from another launcher */
    char *sub = g_strdup_printf("Copies worlds, schematics and packs into “%s”. Nothing is moved or deleted where "
                                "they come from, and nothing here is overwritten.", a->instance);
    GtkWidget *left = mc_card("From another launcher", sub);
    g_free(sub);
    gtk_widget_set_hexpand(left, TRUE);
    GtkWidget *sr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    if (I.sources)
        g_ptr_array_unref(I.sources);
    I.sources = find_sources(a);
    GtkStringList *names = gtk_string_list_new(NULL);
    for (guint i = 0; i < I.sources->len; i++)
        gtk_string_list_append(names, ((src_t *)I.sources->pdata[i])->name);
    I.dd = gtk_drop_down_new(G_LIST_MODEL(names), NULL);
    gtk_widget_set_hexpand(I.dd, TRUE);
    g_signal_connect(I.dd, "notify::selected", G_CALLBACK(on_source), NULL);
    GtkWidget *fb = gtk_button_new_with_label("Choose a folder…");
    g_signal_connect(fb, "clicked", G_CALLBACK(on_folder), a);
    gtk_box_append(GTK_BOX(sr), I.dd);
    gtk_box_append(GTK_BOX(sr), fb);
    gtk_box_append(GTK_BOX(left), sr);

    I.list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), I.list);
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_box_append(GTK_BOX(left), sc);
    I.go = gtk_button_new_with_label("");
    gtk_widget_add_css_class(I.go, "suggested-action");
    g_signal_connect(I.go, "clicked", G_CALLBACK(on_go), a);
    gtk_box_append(GTK_BOX(left), I.go);
    gtk_box_append(GTK_BOX(cols), left);

    /* from files */
    GtkWidget *right = mc_card("From files",
                               "World zips, .litematic / .schem / .schematic / .nbt, resource and shader pack "
                               "zips, mods. Drop them anywhere on this page, or:");
    gtk_widget_set_size_request(right, 340, -1);
    gtk_widget_set_valign(right, GTK_ALIGN_START);
    GtkWidget *ch = gtk_button_new_with_label("Choose files…");
    g_signal_connect(ch, "clicked", G_CALLBACK(on_files), a);
    gtk_box_append(GTK_BOX(right), ch);
    GtkWidget *wf = gtk_button_new_with_label("Choose a world folder…");
    g_signal_connect(wf, "clicked", G_CALLBACK(on_folder), a);
    gtk_box_append(GTK_BOX(right), wf);
    I.bar = gtk_progress_bar_new();
    gtk_widget_set_margin_top(I.bar, 12);
    gtk_box_append(GTK_BOX(right), I.bar);
    I.msg = mc_label("", "dim");
    gtk_label_set_wrap(GTK_LABEL(I.msg), TRUE);
    gtk_label_set_xalign(GTK_LABEL(I.msg), 0);
    gtk_label_set_selectable(GTK_LABEL(I.msg), TRUE);
    gtk_box_append(GTK_BOX(right), I.msg);
    gtk_box_append(GTK_BOX(cols), right);
    gtk_box_append(GTK_BOX(page), cols);

    GtkDropTarget *dt = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(dt, "drop", G_CALLBACK(on_drop), a);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(dt));

    if (!I.items)
        I.items = g_ptr_array_new_with_free_func((GDestroyNotify)item_free);
    const char *test = g_getenv("TUX_MC_IMPORT");         /* tests: as if these were dropped */
    if (test) {
        GListStore *st = g_list_store_new(G_TYPE_FILE);
        char **paths = g_strsplit(test, G_SEARCHPATH_SEPARATOR_S, -1);
        for (char **q = paths; *q; q++)
            if (**q) {
                GFile *f = g_file_new_for_path(*q);
                g_list_store_append(st, f);
                g_object_unref(f);
            }
        g_strfreev(paths);
        import_files(a, G_LIST_MODEL(st));
        g_object_unref(st);
    }
    if (I.sources->len)
        scan_source(((src_t *)I.sources->pdata[0])->dir);
    else {
        gtk_box_append(GTK_BOX(I.list), mc_label("No other launcher found here. Choose its folder, or use the files "
                                                 "on the right.", "dim"));
        update_go();
    }
    return page;
}
