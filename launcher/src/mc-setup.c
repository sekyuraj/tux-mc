/* mc-setup.c — the first-run screen. tux-mc needs three things: Prism
 * Launcher, a Microsoft account signed in to it, and the 26.3 Fabric instance
 * with Fabric API and tuxmc. Whatever is missing, "Set up" gets it:
 *   1. Prism: the portable build on Windows, the AppImage on Linux (unpacked,
 *      so no FUSE is needed), both into <data>/tux-mc/prism/
 *   2. Prism's settings: download the right Java on its own
 *   3. the instance, Fabric API (sha512-checked) and tuxmc
 *   4. sign-in: Prism opens, and this screen carries on once accounts.json
 *      has an account
 */
#include "mc.h"
#include "theme-black.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define REPO "sekyuraj/tux-mc"
#define MC_VERSION "26.3"
#define PACK "{\"components\":[{\"cachedName\":\"LWJGL 3\",\"cachedVersion\":\"3.4.3\",\"dependencyOnly\":true," \
    "\"uid\":\"org.lwjgl3\",\"version\":\"3.4.3\"},{\"cachedName\":\"Minecraft\",\"cachedRequires\":[{\"suggests\":" \
    "\"3.4.3\",\"uid\":\"org.lwjgl3\"}],\"cachedVersion\":\"26.3\",\"important\":true,\"uid\":\"net.minecraft\"," \
    "\"version\":\"26.3\"},{\"cachedName\":\"Intermediary Mappings\",\"cachedRequires\":[{\"equals\":\"26.3\",\"uid\":" \
    "\"net.minecraft\"}],\"cachedVersion\":\"26.3\",\"dependencyOnly\":true,\"uid\":\"net.fabricmc.intermediary\"," \
    "\"version\":\"26.3\"},{\"cachedName\":\"Fabric Loader\",\"cachedRequires\":[{\"uid\":\"net.fabricmc.intermediary\"}]," \
    "\"cachedVersion\":\"0.20.1\",\"uid\":\"net.fabricmc.fabric-loader\",\"version\":\"0.20.1\"}],\"formatVersion\":1}\n"

enum { ROW_PRISM, ROW_ACCOUNT, ROW_INSTANCE, NROWS };

static struct {
    app_t *a;
    GtkWindow *win;
    GtkWidget *state[NROWS], *go, *go_label, *msg;
    bool busy, waiting_account;
    guint poll;
} S;

/* -------------------------------------------------------------- checks -- */

static bool have_prism(void)
{
    char *e = mc_sys_prism_exe();
    g_free(e);
    return e != NULL;
}

/* the first account's name in accounts.json, or NULL */
static char *account_name(app_t *a)
{
    char *path = g_build_filename(a->prism, "accounts.json", NULL);
    JsonParser *p = json_parser_new();
    char *name = NULL;
    if (json_parser_load_from_file(p, path, NULL)) {
        JsonNode *root = json_parser_get_root(p);
        JsonObject *o = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
        JsonArray *acc = o && json_object_has_member(o, "accounts") ? json_object_get_array_member(o, "accounts") : NULL;
        for (guint i = 0; acc && i < json_array_get_length(acc) && !name; i++) {
            JsonObject *e = json_array_get_object_element(acc, i);
            if (e && json_object_has_member(e, "profile")) {
                JsonObject *pr = json_object_get_object_member(e, "profile");
                const char *n = pr ? json_object_get_string_member_with_default(pr, "name", NULL) : NULL;
                if (n && *n)
                    name = g_strdup(n);
            }
        }
    }
    g_object_unref(p);
    g_free(path);
    return name;
}

static bool has_jar(const char *mods, const char *prefix)
{
    GDir *d = g_dir_open(mods, 0, NULL);
    bool hit = false;
    const char *f;
    while (d && !hit && (f = g_dir_read_name(d)))
        hit = g_str_has_prefix(f, prefix) && g_str_has_suffix(f, ".jar");
    if (d)
        g_dir_close(d);
    return hit;
}

static bool have_instance(app_t *a)
{
    char *pack = g_build_filename(a->instdir, "mmc-pack.json", NULL);
    char *mods = g_build_filename(a->mcdir, "mods", NULL);
    /* the 26.3 instance setup makes needs Fabric API and tuxmc; one picked on
     * the Instances page just has to be there */
    bool ok = g_file_test(pack, G_FILE_TEST_EXISTS) &&
              (strcmp(a->instance, MC_VERSION) || (has_jar(mods, "fabric-api") && has_jar(mods, "tuxmc")));
    g_free(pack);
    g_free(mods);
    return ok;
}

bool mc_setup_needed(app_t *a)
{
    if (g_getenv("TUX_MC_SETUP"))        /* show it anyway (screenshots, tests) */
        return true;
    char *n = account_name(a);
    bool ok = have_prism() && n && have_instance(a);
    g_free(n);
    return !ok;
}

/* ------------------------------------------------------------------ UI -- */

static void row_state(int row, bool ok, const char *text)
{
    gtk_label_set_text(GTK_LABEL(S.state[row]), text);
    if (ok)
        gtk_widget_remove_css_class(S.state[row], "dim");
    else
        gtk_widget_add_css_class(S.state[row], "dim");
}

/* refresh the three rows; true when everything is there */
static bool refresh(void)
{
    app_t *a = S.a;
    mc_paths(a);
    bool p = have_prism();
    char *n = account_name(a);
    bool i = have_instance(a);
    row_state(ROW_PRISM, p, p ? "installed" : "not installed");
    char *t = n ? g_strdup_printf("signed in as %s", n) : g_strdup("not signed in");
    row_state(ROW_ACCOUNT, n != NULL, t);
    g_free(t);
    row_state(ROW_INSTANCE, i, i ? "ready" : "not set up");
    bool all = p && n && i;
    g_free(n);
    if (!S.busy)
        gtk_label_set_text(GTK_LABEL(S.go_label), all ? "START" : "SET UP");
    return all;
}

static void say(const char *text)
{
    gtk_label_set_text(GTK_LABEL(S.msg), text);
}

static gboolean say_idle(gpointer text)
{
    if (S.msg)
        say(text);
    g_free(text);
    return G_SOURCE_REMOVE;
}

/* from the worker thread */
static void say_bg(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void say_bg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_idle_add(say_idle, g_strdup_vprintf(fmt, ap));
    va_end(ap);
}

/* ----------------------------------------------------------- the steps -- */

static JsonParser *get_json(const char *url, JsonNode **root)
{
    GByteArray *b = g_byte_array_new();
    long code = mc_http(NULL, url, NULL, NULL, NULL, b);
    JsonParser *p = json_parser_new();
    *root = NULL;
    if (code == 200 && json_parser_load_from_data(p, (char *)b->data, b->len, NULL))
        *root = json_parser_get_root(p);
    g_byte_array_unref(b);
    return p;
}

/* Prism's newest release for this system, without GitHub's API (it's rate
 * limited): the AppImage has a fixed name under latest/download; the Windows
 * zips carry the version, which the releases/latest redirect gives away.
 * Windows: the MinGW build first (brings its own runtime), MSVC second. */
static char **prism_urls(char **err)
{
    const char *base = "https://github.com/PrismLauncher/PrismLauncher/releases";
    char **urls = g_new0(char *, 3);
    if (MC_LINUX) {
        urls[0] = g_strdup_printf("%s/latest/download/PrismLauncher-Linux-x86_64.AppImage", base);
        return urls;
    }
    char *latest = g_strdup_printf("%s/latest", base), *loc = mc_http_location(latest);
    g_free(latest);
    const char *tag = loc ? strstr(loc, "/tag/") : NULL;
    if (!tag) {
        g_free(loc);
        g_free(urls);
        *err = g_strdup("couldn't find Prism's newest version on GitHub");
        return NULL;
    }
    tag += 5;
    urls[0] = g_strdup_printf("%s/download/%s/PrismLauncher-Windows-MinGW-w64-Portable-%s.zip", base, tag, tag);
    urls[1] = g_strdup_printf("%s/download/%s/PrismLauncher-Windows-MSVC-Portable-%s.zip", base, tag, tag);
    g_free(loc);
    return urls;
}

static bool install_prism(char **err)
{
    say_bg("Downloading Prism Launcher…");
    char **urls = prism_urls(err);
    if (!urls)
        return false;
    char *dir = mc_sys_own_prism_dir();
    g_mkdir_with_parents(dir, 0755);
    char *file = g_build_filename(dir, MC_LINUX ? "PrismLauncher.AppImage" : "PrismLauncher.zip", NULL);
    bool ok = false;
    for (int i = 0; urls[i] && !ok; i++) {
        g_clear_pointer(err, g_free);
        ok = mc_http_download(urls[i], file, err);
    }
    g_strfreev(urls);
    if (ok) {
        say_bg("Unpacking Prism Launcher…");
        int status = 0;
        GError *e = NULL;
        if (MC_LINUX) {
            /* --appimage-extract works without FUSE; Prism then runs from squashfs-root/AppRun */
            g_chmod(file, 0755);
            char *argv[] = {file, "--appimage-extract", NULL};
            ok = g_spawn_sync(dir, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
                              NULL, NULL, &status, &e) && g_spawn_check_wait_status(status, NULL);
        } else {
            /* tar.exe unpacks zips (Windows 10 1803 and up); before that, PowerShell */
            char *argv[] = {"tar.exe", "-xf", file, "-C", dir, NULL};
            ok = g_spawn_sync(dir, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                              G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &status, &e) &&
                 g_spawn_check_wait_status(status, NULL);
            if (!ok) {
                g_clear_error(&e);
                char *cmd = g_strdup_printf("Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s'", file, dir);
                char *ps[] = {"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", cmd, NULL};
                ok = g_spawn_sync(dir, ps, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                                  G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &status, &e) &&
                     g_spawn_check_wait_status(status, NULL);
                g_free(cmd);
            }
        }
        if (!ok)
            *err = g_strdup_printf("couldn't unpack Prism%s%s", e ? ": " : "", e ? e->message : "");
        g_clear_error(&e);
        g_remove(file);
    }
    g_free(file);
    g_free(dir);
    if (ok && !have_prism()) {
        *err = g_strdup("Prism was unpacked but its program wasn't found");
        ok = false;
    }
    return ok;
}

/* key=value in [General] of prismlauncher.cfg, only where it isn't set yet */
static void cfg_default(GString *s, const char *key, const char *val)
{
    char *pre = g_strdup_printf("\n%s=", key);
    if (!strstr(s->str, pre)) {
        const char *gen = strstr(s->str, "[General]\n");
        gsize at = gen ? (gsize)(gen - s->str) + 10 : s->len;
        char *line = g_strdup_printf("%s=%s\n", key, val);
        g_string_insert(s, at, line);
        g_free(line);
    }
    g_free(pre);
}

/* Prism should fetch the Java 26.3 needs by itself, and skip its first-run wizard */
static void prism_settings(app_t *a)
{
    if (mc_sys_prism_running())          /* it writes the file back on exit */
        return;
    g_mkdir_with_parents(a->prism, 0755);
    char *path = g_build_filename(a->prism, "prismlauncher.cfg", NULL), *txt = NULL;
    GString *s = g_string_new("\n");
    if (g_file_get_contents(path, &txt, NULL, NULL))
        g_string_append(s, txt);
    g_free(txt);
    if (!strstr(s->str, "[General]"))
        g_string_insert(s, 1, "[General]\n");
    cfg_default(s, "AutomaticJavaDownload", "true");
    cfg_default(s, "AutomaticJavaSwitch", "true");
    cfg_default(s, "IgnoreJavaWizard", "true");
    cfg_default(s, "Language", "en_US");
    /* Prism in black, like tux-mc: the "tux" theme, unless one is there already */
    char *dir = g_build_filename(a->prism, "themes", "tux", NULL);
    char *json = g_build_filename(dir, "theme.json", NULL), *qss = g_build_filename(dir, "themeStyle.css", NULL);
    if (!g_file_test(json, G_FILE_TEST_EXISTS)) {
        g_mkdir_with_parents(dir, 0755);
        g_file_set_contents(json, PRISM_THEME_JSON, -1, NULL);
        g_file_set_contents(qss, PRISM_THEME_CSS, -1, NULL);
    }
    cfg_default(s, "ApplicationTheme", "tux");
    cfg_default(s, "IconTheme", "flat_white");
    g_free(dir);
    g_free(json);
    g_free(qss);
    g_file_set_contents(path, s->str + 1, -1, NULL);
    g_string_free(s, TRUE);
    g_free(path);
}

/* the newest Fabric API for Minecraft mc into mods (sha512-checked); the
 * Instances page uses it for new instances too */
bool mc_fabric_api(const char *mods, const char *mc, char **err)
{
    if (has_jar(mods, "fabric-api"))
        return true;
    say_bg("Downloading Fabric API…");
    JsonNode *root;
    char *esc = g_uri_escape_string(mc, NULL, FALSE);
    char *q = g_strdup_printf("https://api.modrinth.com/v2/project/fabric-api/version?loaders=%%5B%%22fabric%%22%%5D"
                              "&game_versions=%%5B%%22%s%%22%%5D", esc);
    JsonParser *p = get_json(q, &root);
    g_free(q);
    g_free(esc);
    JsonArray *vs = root && JSON_NODE_HOLDS_ARRAY(root) ? json_node_get_array(root) : NULL;
    JsonObject *file = NULL;
    if (vs && json_array_get_length(vs)) {
        JsonArray *fs = json_object_get_array_member(json_array_get_object_element(vs, 0), "files");
        for (guint i = 0; fs && i < json_array_get_length(fs) && !file; i++) {
            JsonObject *f = json_array_get_object_element(fs, i);
            if (i == 0 || json_object_get_boolean_member_with_default(f, "primary", false))
                file = f;
        }
    }
    if (!file) {
        g_object_unref(p);
        *err = g_strdup_printf("no Fabric API for %s on Modrinth", mc);
        return false;
    }
    const char *url = json_object_get_string_member(file, "url");
    const char *name = json_object_get_string_member(file, "filename");
    JsonObject *h = json_object_get_object_member(file, "hashes");
    const char *want = h ? json_object_get_string_member_with_default(h, "sha512", "") : "";
    char *path = g_build_filename(mods, name, NULL);
    bool ok = mc_http_download(url, path, err);
    if (ok) {
        char *data = NULL;
        gsize len = 0;
        char *sum = g_file_get_contents(path, &data, &len, NULL)
                        ? g_compute_checksum_for_data(G_CHECKSUM_SHA512, (guchar *)data, len) : NULL;
        if (!sum || g_ascii_strcasecmp(sum, want)) {
            *err = g_strdup("the Fabric API download is damaged");
            g_remove(path);
            ok = false;
        }
        g_free(sum);
        g_free(data);
    }
    g_free(path);
    g_object_unref(p);
    return ok;
}

static bool make_instance(app_t *a, char **err)
{
    say_bg("Creating the Minecraft " MC_VERSION " instance…");
    char *mods = g_build_filename(a->mcdir, "mods", NULL);
    g_mkdir_with_parents(mods, 0755);
    char *cfg = g_build_filename(a->instdir, "instance.cfg", NULL);
    if (!g_file_test(cfg, G_FILE_TEST_EXISTS)) {
        char *t = g_strdup_printf("[General]\nConfigVersion=1.2\nInstanceType=OneSix\nname=%s\niconKey=default\n",
                                  a->instance);
        g_file_set_contents(cfg, t, -1, NULL);
        g_free(t);
    }
    g_free(cfg);
    char *pack = g_build_filename(a->instdir, "mmc-pack.json", NULL);
    if (!g_file_test(pack, G_FILE_TEST_EXISTS))
        g_file_set_contents(pack, PACK, -1, NULL);
    g_free(pack);

    bool ok = mc_fabric_api(mods, MC_VERSION, err);
    if (ok && !has_jar(mods, "tuxmc")) {
        say_bg("Downloading tuxmc…");
        char *jar = g_build_filename(mods, "tuxmc.jar", NULL);
        ok = mc_http_download("https://github.com/" REPO "/releases/latest/download/tuxmc.jar", jar, err);
        g_free(jar);
    }
    g_free(mods);
    return ok;
}

static void work(GTask *t, gpointer src, gpointer data, GCancellable *c)
{
    (void)src;
    (void)data;
    (void)c;
    app_t *a = S.a;
    char *err = NULL;
    bool ok = true;
    if (!have_prism())
        ok = install_prism(&err);
    if (ok) {
        mc_paths(a);                     /* a portable Prism keeps its data next to it */
        prism_settings(a);
        if (!have_instance(a))
            ok = make_instance(a, &err);
    }
    if (ok)
        g_task_return_boolean(t, TRUE);
    else
        g_task_return_new_error(t, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", err ? err : "something went wrong");
    g_free(err);
}

static gboolean poll_account(gpointer d)
{
    (void)d;
    if (!S.win)
        return G_SOURCE_REMOVE;
    if (!refresh())
        return G_SOURCE_CONTINUE;
    S.poll = 0;
    S.waiting_account = false;
    say("All set. Press Start.");
    return G_SOURCE_REMOVE;
}

static void sign_in(void)
{
    const char *args[] = {"--show-window", NULL};
    GError *e = NULL;
    if (!mc_sys_run_prism(args, &e)) {
        say(e->message);
        g_error_free(e);
        return;
    }
    S.waiting_account = true;
    say("Prism is open: click Add Microsoft account and sign in with the account that owns Minecraft. "
        "(If Prism doesn't ask: Accounts at the top right, Manage Accounts, Add Microsoft.) "
        "This window carries on by itself once you're signed in.");
    if (!S.poll)
        S.poll = g_timeout_add_seconds(2, poll_account, NULL);
}

static void done(GObject *src, GAsyncResult *res, gpointer d)
{
    (void)src;
    (void)d;
    GError *e = NULL;
    S.busy = false;
    gtk_widget_set_sensitive(S.go, TRUE);
    gtk_widget_remove_css_class(S.go, "busy");
    if (!g_task_propagate_boolean(G_TASK(res), &e)) {
        char *t = g_strdup_printf("Setup stopped: %s. Press Set up to try again.", e->message);
        say(t);
        g_free(t);
        g_error_free(e);
        refresh();
        return;
    }
    if (refresh()) {
        say("All set. Press Start.");
        return;
    }
    sign_in();                           /* only the account is missing now */
}

static void on_go(GtkButton *b, gpointer d)
{
    (void)b;
    (void)d;
    if (S.busy)
        return;
    if (refresh()) {                     /* everything's there: on to the launcher */
        if (S.poll)
            g_source_remove(S.poll);
        S.poll = 0;
        GtkWindow *w = S.win;
        S.win = NULL;
        S.msg = NULL;
        mc_open_main(S.a);
        gtk_window_destroy(w);
        return;
    }
    if (S.waiting_account && have_prism() && have_instance(S.a)) {
        sign_in();                       /* Prism was closed before signing in: open it again */
        return;
    }
    S.busy = true;
    gtk_widget_set_sensitive(S.go, FALSE);
    gtk_widget_add_css_class(S.go, "busy");
    gtk_label_set_text(GTK_LABEL(S.go_label), "SETTING UP");
    GTask *t = g_task_new(NULL, NULL, done, NULL);
    g_task_run_in_thread(t, work);
    g_object_unref(t);
}

static GtkWidget *row(int i, const char *title, const char *what)
{
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(r, "mcl-row");
    GtkWidget *txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(txt, TRUE);
    GtkWidget *t = mc_label(title, "mcl-h2");
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    GtkWidget *w = mc_label(what, "dim");
    gtk_label_set_xalign(GTK_LABEL(w), 0);
    gtk_label_set_wrap(GTK_LABEL(w), TRUE);
    gtk_box_append(GTK_BOX(txt), t);
    gtk_box_append(GTK_BOX(txt), w);
    gtk_box_append(GTK_BOX(r), txt);
    S.state[i] = mc_label("", NULL);
    gtk_widget_set_valign(S.state[i], GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(r), S.state[i]);
    return r;
}

static void on_setup_destroy(GtkWidget *w, gpointer d)
{
    (void)w;
    (void)d;
    if (S.poll)
        g_source_remove(S.poll);
    S.poll = 0;
    S.win = NULL;
    S.msg = NULL;
}

void mc_setup_show(app_t *a)
{
    S.a = a;
    S.win = GTK_WINDOW(gtk_application_window_new(a->gapp));
    gtk_window_set_title(S.win, "Minecraft");
    gtk_window_set_default_size(S.win, 720, 560);
    gtk_widget_add_css_class(GTK_WIDGET(S.win), "mcl-window");
    g_signal_connect(S.win, "destroy", G_CALLBACK(on_setup_destroy), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(box, "mcl-root");
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(box, 620, -1);
    gtk_window_set_child(S.win, box);

    GtkWidget *t = mc_label("First things first", "mcl-title");
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    gtk_box_append(GTK_BOX(box), t);
    GtkWidget *sub = mc_label("tux-mc runs Minecraft through Prism Launcher. A few things are missing; "
                              "Set up gets them for you.", "mcl-sub");
    gtk_label_set_xalign(GTK_LABEL(sub), 0);
    gtk_label_set_wrap(GTK_LABEL(sub), TRUE);
    gtk_box_append(GTK_BOX(box), sub);

    gtk_box_append(GTK_BOX(box), row(ROW_PRISM, "Prism Launcher",
                                     "downloads and starts Minecraft; installed into tux-mc's own folder"));
    gtk_box_append(GTK_BOX(box), row(ROW_ACCOUNT, "Microsoft account",
                                     "the account that owns Minecraft, signed in to Prism"));
    gtk_box_append(GTK_BOX(box), row(ROW_INSTANCE, "Minecraft " MC_VERSION,
                                     "with Fabric, Fabric API and the tuxmc mod"));

    S.go = gtk_button_new();
    gtk_widget_add_css_class(S.go, "mcl-start");
    gtk_widget_set_margin_top(S.go, 14);
    S.go_label = mc_label("SET UP", NULL);
    gtk_widget_set_hexpand(S.go_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(S.go_label), 0.5);
    gtk_button_set_child(GTK_BUTTON(S.go), S.go_label);
    g_signal_connect(S.go, "clicked", G_CALLBACK(on_go), NULL);
    gtk_box_append(GTK_BOX(box), S.go);

    S.msg = mc_label("", "mcl-status");
    gtk_label_set_wrap(GTK_LABEL(S.msg), TRUE);
    gtk_label_set_xalign(GTK_LABEL(S.msg), 0);
    gtk_box_append(GTK_BOX(box), S.msg);

    if (refresh())
        say("All set. Press Start.");
    gtk_window_present(S.win);
}
