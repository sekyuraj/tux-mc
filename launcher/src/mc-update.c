/* mc-update.c — updates from the GitHub releases, no compiling.
 *
 * The newest release is found through the releases/latest redirect (GitHub's
 * API is rate limited). Every release carries ready builds next to a .sha256
 * file each:
 *   tux-mc-linux-x86_64.tar.gz   tux-mc/bin/tux-mc + tux-mc/share/tux-mc/data
 *   tux-mc-windows.zip           tux-mc-windows/tux-mc/{bin,lib,share}
 *   tuxmc.jar                    the mod
 * Linux: the new program is tried (--version) and then renamed over the old
 * one; the running launcher keeps the old file open until it restarts.
 * Windows can't overwrite a running .exe or loaded DLLs, but it can rename
 * them: those become *.old-update and go away on the next start.
 * The mod is swapped in the current instance when the game isn't running.
 */
#include "mc.h"

#include <glib/gstdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define REPO "sekyuraj/tux-mc"
#define RELEASES "https://github.com/" REPO "/releases"
#define OLD_SUFFIX ".old-update"

static char *installed_exe;              /* where the new build went (for Restart) */

/* TUX_MC_UPDATE_URL points the updater at a test server instead of GitHub */
static const char *releases(void)
{
    const char *t = g_getenv("TUX_MC_UPDATE_URL");
    return t && *t ? t : RELEASES;
}

/* ---------------------------------------------------------------- helpers -- */

static void ver_parse(const char *v, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (*v == 'v' || *v == 'V')
        v++;
    for (int i = 0; i < 3 && *v; i++) {
        out[i] = atoi(v);
        while (*v && *v != '.')
            v++;
        if (*v == '.')
            v++;
    }
}

static bool newer(const char *tag, const char *cur)
{
    int t[3], c[3];
    ver_parse(tag, t);
    ver_parse(cur, c);
    for (int i = 0; i < 3; i++)
        if (t[i] != c[i])
            return t[i] > c[i];
    return false;
}

static void rm_rf(const char *path)
{
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GDir *d = g_dir_open(path, 0, NULL);
        for (const char *e; d && (e = g_dir_read_name(d));) {
            char *p = g_build_filename(path, e, NULL);
            rm_rf(p);
            g_free(p);
        }
        if (d)
            g_dir_close(d);
        g_rmdir(path);
    } else {
        g_remove(path);
    }
}

static char *file_sha256(const char *path)
{
    FILE *f = g_fopen(path, "rb");
    if (!f)
        return NULL;
    GChecksum *c = g_checksum_new(G_CHECKSUM_SHA256);
    guchar buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        g_checksum_update(c, buf, n);
    fclose(f);
    char *s = g_strdup(g_checksum_get_string(c));
    g_checksum_free(c);
    return s;
}

/* download url/name and name.sha256 into dir, check the sum. 404 → *missing */
static char *fetch_checked(const char *base, const char *name, const char *dir, bool *missing, char **err)
{
    char *url = g_strdup_printf("%s/%s.sha256", base, name);
    GByteArray *b = g_byte_array_new();
    long code = mc_http(NULL, url, NULL, NULL, NULL, b);
    g_free(url);
    if (code != 200) {
        *missing = code == 404;
        *err = g_strdup_printf("no checksum for %s (HTTP %ld)", name, code);
        g_byte_array_unref(b);
        return NULL;
    }
    char *want = g_strndup((char *)b->data, MIN(b->len, 64));
    g_byte_array_unref(b);
    char *path = g_build_filename(dir, name, NULL);
    url = g_strdup_printf("%s/%s", base, name);
    bool ok = mc_http_download(url, path, err);
    g_free(url);
    if (ok) {
        char *got = file_sha256(path);
        ok = got && strlen(want) == 64 && !g_ascii_strcasecmp(got, want);
        if (!ok)
            *err = g_strdup_printf("%s arrived damaged (checksum mismatch)", name);
        g_free(got);
    }
    g_free(want);
    if (!ok) {
        g_remove(path);
        g_clear_pointer(&path, g_free);
    }
    return path;
}

static bool copy_file(const char *from, const char *to, GError **e)
{
    GFile *a = g_file_new_for_path(from), *b = g_file_new_for_path(to);
    bool ok = g_file_copy(a, b, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, e);
    g_object_unref(a);
    g_object_unref(b);
    return ok;
}

static bool untar(const char *file, const char *dir, char **err)
{
    g_mkdir_with_parents(dir, 0755);
    char *argv[] = {MC_LINUX ? "tar" : "tar.exe", "-xf", (char *)file, "-C", (char *)dir, NULL};
    int status = 0;
    GError *e = NULL;
    bool ok = g_spawn_sync(dir, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                           G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &status, &e) &&
              g_spawn_check_wait_status(status, NULL);
    if (!ok && !MC_LINUX) {              /* Windows before 10 1803 has no tar.exe */
        g_clear_error(&e);
        char *cmd = g_strdup_printf("Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s'", file, dir);
        char *ps[] = {"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", cmd, NULL};
        ok = g_spawn_sync(dir, ps, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                          G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &status, &e) &&
             g_spawn_check_wait_status(status, NULL);
        g_free(cmd);
    }
    if (!ok)
        *err = g_strdup_printf("couldn't unpack the update%s%s", e ? ": " : "", e ? e->message : "");
    g_clear_error(&e);
    return ok;
}

/* ------------------------------------------------------------ installing -- */

#ifndef _WIN32
static char *own_exe(void)
{
    char *p = g_file_read_link("/proc/self/exe", NULL);
    if (p && g_str_has_suffix(p, " (deleted)"))   /* replaced already by an earlier update */
        p[strlen(p) - 10] = 0;
    return p;
}

static bool install_linux(const char *x, const char *tag, char **err)
{
    char *new = g_build_filename(x, "tux-mc", "bin", "tux-mc", NULL);
    g_chmod(new, 0755);
    /* does it run here at all (libraries)? */
    char *argv[] = {new, "--version", NULL}, *out = NULL;
    int status = 0;
    bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &status, NULL) &&
              g_spawn_check_wait_status(status, NULL) && out && strstr(out, tag + (*tag == 'v'));
    g_free(out);
    if (!ok) {
        *err = g_strdup("the new build doesn't run on this system (missing libraries?). Update from source: "
                        "git pull && ./install.sh");
        g_free(new);
        return false;
    }
    char *exe = own_exe();
    char *tmp = exe ? g_strconcat(exe, ".new", NULL) : NULL;
    GError *e = NULL;
    ok = exe && copy_file(new, tmp, &e) && g_chmod(tmp, 0755) == 0 && g_rename(tmp, exe) == 0;
    if (!ok) {
        *err = g_strdup_printf("couldn't replace %s%s%s", exe ? exe : "the launcher", e ? ": " : "",
                               e ? e->message : "");
        if (tmp)
            g_remove(tmp);
    } else {
        g_free(installed_exe);
        installed_exe = g_strdup(exe);
    }
    g_clear_error(&e);
    g_free(tmp);
    g_free(exe);
    g_free(new);
    if (!ok)
        return false;

    /* cape pictures */
    char *src = g_build_filename(x, "tux-mc", "share", "tux-mc", "data", NULL);
    char *dst = g_build_filename(g_get_user_data_dir(), "tux-mc", "data", NULL);
    g_mkdir_with_parents(dst, 0755);
    GDir *d = g_dir_open(src, 0, NULL);
    for (const char *f; d && (f = g_dir_read_name(d));) {
        char *a = g_build_filename(src, f, NULL), *b = g_build_filename(dst, f, NULL);
        copy_file(a, b, NULL);
        g_free(a);
        g_free(b);
    }
    if (d)
        g_dir_close(d);
    g_free(src);
    g_free(dst);
    return true;
}
#else
/* copy the tree src over dst; files in use are renamed out of the way first */
static bool over(const char *src, const char *dst, GString *olds, char **err)
{
    g_mkdir_with_parents(dst, 0755);
    GDir *d = g_dir_open(src, 0, NULL);
    bool ok = d != NULL;
    for (const char *f; ok && (f = g_dir_read_name(d));) {
        char *a = g_build_filename(src, f, NULL), *b = g_build_filename(dst, f, NULL);
        if (g_file_test(a, G_FILE_TEST_IS_DIR)) {
            ok = over(a, b, olds, err);
        } else {
            GError *e = NULL;
            if (!copy_file(a, b, NULL)) {          /* in use: move it aside */
                char *old = g_strconcat(b, OLD_SUFFIX, NULL);
                g_remove(old);
                if (g_rename(b, old) == 0)
                    g_string_append_printf(olds, "%s\n", old);
                g_free(old);
                ok = copy_file(a, b, &e);
                if (!ok)
                    *err = g_strdup_printf("couldn't replace %s: %s", b, e ? e->message : "?");
                g_clear_error(&e);
            }
        }
        g_free(a);
        g_free(b);
    }
    if (d)
        g_dir_close(d);
    return ok;
}

static char *root_dir(void) { return g_win32_get_package_installation_directory_of_module(NULL); }

static bool install_windows(const char *x, char **err)
{
    char *root = root_dir();
    char *src = g_build_filename(x, "tux-mc-windows", "tux-mc", NULL);
    GString *olds = g_string_new(NULL);
    bool ok = g_file_test(src, G_FILE_TEST_IS_DIR) && over(src, root, olds, err);
    if (!ok && !*err)
        *err = g_strdup("the update has an unexpected layout");
    char *list = g_build_filename(root, "update-old.txt", NULL);
    if (olds->len) {
        char *prev = NULL;
        g_file_get_contents(list, &prev, NULL, NULL);
        if (prev)
            g_string_prepend(olds, prev);
        g_file_set_contents(list, olds->str, -1, NULL);
        g_free(prev);
    }
    if (ok) {
        g_free(installed_exe);
        installed_exe = g_build_filename(root, "bin", "tux-mc.exe", NULL);
    }
    g_free(list);
    g_string_free(olds, TRUE);
    g_free(src);
    g_free(root);
    return ok;
}
#endif

/* the mod in the current instance, if it has tuxmc and the game is closed */
static char *update_mod(app_t *a, const char *base, const char *dir)
{
    char *mods = g_build_filename(a->mcdir, "mods", NULL);
    GDir *d = g_dir_open(mods, 0, NULL);
    GPtrArray *old = g_ptr_array_new_with_free_func(g_free);
    for (const char *f; d && (f = g_dir_read_name(d));)
        if (g_str_has_prefix(f, "tuxmc") && g_str_has_suffix(f, ".jar"))
            g_ptr_array_add(old, g_build_filename(mods, f, NULL));
    if (d)
        g_dir_close(d);
    char *note = NULL;
    if (!old->len)
        goto out;
    if (mc_game_running(a)) {
        note = g_strdup("The tuxmc mod wasn't updated: the game is running. Check again after closing it.");
        goto out;
    }
    bool missing = false;
    char *err = NULL, *jar = fetch_checked(base, "tuxmc.jar", dir, &missing, &err);
    if (!jar) {
        note = g_strdup_printf("The tuxmc mod wasn't updated: %s.", err);
        g_free(err);
        goto out;
    }
    char *part = g_build_filename(mods, "tuxmc.jar.part", NULL), *dst = g_build_filename(mods, "tuxmc.jar", NULL);
    if (copy_file(jar, part, NULL)) {
        for (guint i = 0; i < old->len; i++)
            g_remove(old->pdata[i]);
        if (g_rename(part, dst) != 0)
            note = g_strdup("The tuxmc mod couldn't be put in place.");
    } else {
        note = g_strdup("The tuxmc mod couldn't be copied.");
    }
    g_remove(part);
    g_free(part);
    g_free(dst);
    g_free(jar);
out:
    g_ptr_array_unref(old);
    g_free(mods);
    return note;
}

/* --------------------------------------------------------------- the job -- */

typedef struct {
    app_t *a;
    bool quiet;                          /* the check on start: only speak up when there is news */
    bool installed, up_to_date;
    bool building;                       /* released, but GitHub is still making its build */
    char *tag, *msg;
} job_t;

static void job_free(job_t *j)
{
    g_free(j->tag);
    g_free(j->msg);
    g_free(j);
}

static void job_thread(GTask *t, gpointer s, gpointer data, GCancellable *c)
{
    (void)s;
    (void)c;
    job_t *j = data;
    char *latest = g_strconcat(releases(), "/latest", NULL), *loc = mc_http_location(latest);
    g_free(latest);
    const char *tag = loc ? strstr(loc, "/tag/") : NULL;
    if (!tag) {
        j->msg = g_strdup("Couldn't reach GitHub to look for updates.");
        g_free(loc);
        g_task_return_boolean(t, FALSE);
        return;
    }
    j->tag = g_uri_unescape_string(tag + 5, NULL);
    g_free(loc);
    if (!newer(j->tag, TUX_MC_VERSION)) {
        j->up_to_date = true;
        j->msg = g_strdup_printf("Up to date (%s is the newest).", j->tag);
        g_task_return_boolean(t, TRUE);
        return;
    }

    char *dir = g_build_filename(g_get_user_cache_dir(), "tux-mc", "update", NULL);
    rm_rf(dir);
    g_mkdir_with_parents(dir, 0755);
    char *base = g_strdup_printf("%s/download/%s", releases(), j->tag);
    const char *asset = MC_LINUX ? "tux-mc-linux-x86_64.tar.gz" : "tux-mc-windows.zip";
    bool missing = false;
    char *err = NULL, *file = fetch_checked(base, asset, dir, &missing, &err);
    bool ok = file != NULL;
    if (!ok && missing) {                /* published a minute ago: the builds take a few */
        j->building = true;
        j->msg = g_strdup_printf("%s is out and GitHub is still building it. It installs by itself in a few "
                                 "minutes.", j->tag);
        g_free(err);
        g_free(dir);
        g_free(base);
        g_task_return_boolean(t, FALSE);
        return;
    }
    char *x = g_build_filename(dir, "x", NULL);
    if (ok)
        ok = untar(file, x, &err);
#ifndef _WIN32
    if (ok)
        ok = install_linux(x, j->tag, &err);
#else
    if (ok)
        ok = install_windows(x, &err);
#endif
    char *note = ok ? update_mod(j->a, base, dir) : NULL;
    if (ok) {
        j->installed = true;
        j->msg = g_strdup_printf("Updated to %s. Restart the launcher to use it.%s%s", j->tag, note ? " " : "",
                                 note ? note : "");
    } else {
        j->msg = g_strdup_printf("Update to %s failed: %s", j->tag, err ? err : "?");
    }
    g_free(note);
    g_free(err);
    g_free(file);
    g_free(x);
    rm_rf(dir);
    g_free(dir);
    g_free(base);
    g_task_return_boolean(t, ok);
}

static void set_state(app_t *a, const char *text)
{
    if (a->upd_state)
        gtk_label_set_text(GTK_LABEL(a->upd_state), text);
}

static void run_job(app_t *a, bool quiet);
static int build_tries;

static gboolean retry_later(gpointer data)
{
    run_job(data, true);
    return G_SOURCE_REMOVE;
}

static void job_done(GObject *s, GAsyncResult *r, gpointer data)
{
    (void)s;
    (void)data;
    job_t *j = g_task_get_task_data(G_TASK(r));
    app_t *a = j->a;
    g_task_propagate_boolean(G_TASK(r), NULL);
    a->upd_busy = false;
    a->upd_installed |= j->installed;
    if (a->upd_spin)
        gtk_spinner_set_spinning(GTK_SPINNER(a->upd_spin), FALSE);
    if (a->upd_button) {
        gtk_widget_set_sensitive(a->upd_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(a->upd_button), a->upd_installed ? "Restart now" : "Check for updates");
    }
    set_state(a, j->msg);
    if (!j->quiet || j->installed || (!j->up_to_date && j->tag))
        if (a->win)
            mc_status(a, "%s", j->msg);
    /* look again every 2 minutes while the build is being made (for up to 40 min) */
    if (j->building && build_tries++ < 20)
        g_timeout_add_seconds(120, retry_later, a);
    else if (!j->building)
        build_tries = 0;
}

static void run_job(app_t *a, bool quiet)
{
    if (a->upd_busy)
        return;
    a->upd_busy = true;
    if (a->upd_button)
        gtk_widget_set_sensitive(a->upd_button, FALSE);
    if (a->upd_spin)
        gtk_spinner_set_spinning(GTK_SPINNER(a->upd_spin), TRUE);
    set_state(a, "Looking for updates…");
    job_t *j = g_new0(job_t, 1);
    j->a = a;
    j->quiet = quiet;
    GTask *t = g_task_new(NULL, NULL, job_done, NULL);
    g_task_set_task_data(t, j, (GDestroyNotify)job_free);
    g_task_run_in_thread(t, job_thread);
    g_object_unref(t);
}

/* ------------------------------------------------------------------- UI -- */

static void on_button(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    if (a->upd_installed) {
        a->restart = true;
        g_application_quit(G_APPLICATION(a->gapp));
        return;
    }
    run_job(a, false);
}

static void on_auto(GObject *sw, GParamSpec *ps, gpointer data)
{
    (void)ps;
    (void)data;
    mc_cfg_set_bool("auto_update", gtk_switch_get_active(GTK_SWITCH(sw)));
}

static void on_card_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    app_t *a = data;
    a->upd_state = a->upd_button = a->upd_spin = NULL;
}

GtkWidget *mc_update_card(app_t *a)
{
    char *sub = g_strdup_printf("This is version %s. New ones come from the GitHub releases.", TUX_MC_VERSION);
    GtkWidget *card = mc_card("Updates", sub);
    g_free(sub);
    g_signal_connect(card, "destroy", G_CALLBACK(on_card_destroy), a);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "mcl-setrow");
    GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_box_append(GTK_BOX(t), mc_label("Update by itself", NULL));
    GtkWidget *s = mc_label("looks on every start and installs what's new; the mod too", "dim");
    gtk_label_set_wrap(GTK_LABEL(s), TRUE);
    gtk_box_append(GTK_BOX(t), s);
    gtk_box_append(GTK_BOX(row), t);
    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(sw), mc_cfg_bool("auto_update", true));
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_auto), a);
    gtk_box_append(GTK_BOX(row), sw);
    gtk_box_append(GTK_BOX(card), row);

    GtkWidget *br = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    a->upd_button = gtk_button_new_with_label(a->upd_installed ? "Restart now" : "Check for updates");
    g_signal_connect(a->upd_button, "clicked", G_CALLBACK(on_button), a);
    a->upd_spin = gtk_spinner_new();
    gtk_box_append(GTK_BOX(br), a->upd_button);
    gtk_box_append(GTK_BOX(br), a->upd_spin);
    gtk_box_append(GTK_BOX(card), br);
    a->upd_state = mc_label("", "dim");
    gtk_label_set_wrap(GTK_LABEL(a->upd_state), TRUE);
    gtk_label_set_xalign(GTK_LABEL(a->upd_state), 0);
    gtk_box_append(GTK_BOX(card), a->upd_state);
    if (a->upd_busy) {
        gtk_widget_set_sensitive(a->upd_button, FALSE);
        gtk_spinner_set_spinning(GTK_SPINNER(a->upd_spin), TRUE);
    }
    return card;
}

static gboolean startup_check(gpointer data)
{
    run_job(data, true);
    return G_SOURCE_REMOVE;
}

void mc_update_startup(app_t *a)
{
#ifdef _WIN32
    /* files the last update moved aside are free now */
    char *root = root_dir(), *list = g_build_filename(root, "update-old.txt", NULL), *txt = NULL;
    if (g_file_get_contents(list, &txt, NULL, NULL)) {
        GString *left = g_string_new(NULL);
        char **l = g_strsplit(txt, "\n", -1);
        for (char **q = l; *q; q++)
            if (**q && g_str_has_suffix(*q, OLD_SUFFIX) && g_remove(*q) != 0 && g_file_test(*q, G_FILE_TEST_EXISTS))
                g_string_append_printf(left, "%s\n", *q);
        g_strfreev(l);
        if (left->len)
            g_file_set_contents(list, left->str, -1, NULL);
        else
            g_remove(list);
        g_string_free(left, TRUE);
        g_free(txt);
    }
    g_free(list);
    g_free(root);
#endif
    static bool once;
    if (once || g_getenv("TUX_MC_NO_UPDATE") || !mc_cfg_bool("auto_update", true))
        return;
    once = true;
    g_timeout_add_seconds(3, startup_check, a);
}

void mc_update_restart(const char *argv0)
{
    const char *exe = installed_exe ? installed_exe : argv0;
#ifndef _WIN32
    execl(exe, exe, (char *)NULL);
#else
    char *argv[] = {(char *)exe, NULL};
    g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL);
#endif
}
