/* Fast start (Linux): the game without Prism in between.
 *
 * A launch through Prism goes via tux-mc-wrap (wrap.c), which keeps the java
 * command and the launch script Prism wrote, minus the access token, in
 * <cache>/tux-mc/fast/<instance>.launch. The next START runs that command
 * itself with the token from accounts.json, which skips Prism's start-up and
 * its sign-in round trips (about 2.5 s here).
 *
 * Prism is used again whenever anything is in doubt: the instance or Prism
 * settings changed since the capture (key file), a library or Java is gone,
 * another account is active, or the token has under 10 minutes left (Prism
 * refreshes it, and the capture is taken again on the way).
 */
#include "mc.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

#ifndef _WIN32

#define fast_log(...) g_message(__VA_ARGS__)

static char *str_replace(const char *s, const char *from, const char *to)
{
    char **v = g_strsplit(s, from, -1);
    char *r = g_strjoinv(to, v);
    g_strfreev(v);
    return r;
}

static char *safe_name(const char *inst)
{
    char *n = g_strdup(inst);
    for (char *c = n; *c; c++)
        if (!g_ascii_isalnum(*c) && *c != '.' && *c != '-' && *c != '_')
            *c = '_';
    return n;
}

static char *fast_path(app_t *a, const char *ext)
{
    char *n = safe_name(a->instance);
    char *f = g_strdup_printf("%s.%s", n, ext);
    char *p = g_build_filename(g_get_user_cache_dir(), "tux-mc", "fast", f, NULL);
    g_free(f);
    g_free(n);
    return p;
}

static char *wrap_exe(void)
{
    char *self = g_file_read_link("/proc/self/exe", NULL);
    if (!self)
        return NULL;
    char *dir = g_path_get_dirname(self);
    char *w = g_build_filename(dir, "tux-mc-wrap", NULL);
    g_free(dir);
    g_free(self);
    if (!g_file_test(w, G_FILE_TEST_IS_EXECUTABLE)) {
        g_free(w);
        return NULL;
    }
    return w;
}

bool mc_fast_enabled(void) { return mc_cfg_bool("fast_start", true); }

char *mc_fast_wrap_prefix(app_t *a)
{
    if (!mc_fast_enabled())
        return NULL;
    char *w = wrap_exe(), *cap = fast_path(a, "launch");
    char *r = NULL;
    /* Prism splits the wrapper at spaces: paths with spaces would need quoting it may not keep */
    if (w && !strchr(w, ' ') && !strchr(cap, ' '))
        r = g_strdup_printf("%s %s", w, cap);
    g_free(w);
    g_free(cap);
    return r;
}

/* what the capture depends on: the instance settings (not play times or Prism's
 * UI state), its components, Prism's Java choice. Mods don't matter: Fabric
 * finds those itself. */
static char *cache_key(app_t *a)
{
    GString *s = g_string_new(NULL);
    char *txt = NULL;
    char *p = g_build_filename(a->instdir, "instance.cfg", NULL);
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **lines = g_strsplit(txt, "\n", -1);
        bool general = false;
        for (char **l = lines; *l; l++) {
            if (**l == '[')
                general = !strncmp(*l, "[General]", 9);
            else if (general && !g_str_has_prefix(*l, "lastLaunchTime=") && !g_str_has_prefix(*l, "lastTimePlayed=")
                     && !g_str_has_prefix(*l, "totalTimePlayed="))
                g_string_append_printf(s, "%s\n", *l);
        }
        g_strfreev(lines);
        g_free(txt);
    }
    g_free(p);
    /* mmc-pack.json: Prism rewrites it on every launch, so only uid=version of each component */
    p = g_build_filename(a->instdir, "mmc-pack.json", NULL);
    JsonParser *jp = json_parser_new();
    if (json_parser_load_from_file(jp, p, NULL) && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(jp))) {
        JsonObject *o = json_node_get_object(json_parser_get_root(jp));
        JsonArray *c = json_object_has_member(o, "components") ? json_object_get_array_member(o, "components") : NULL;
        for (guint i = 0; c && i < json_array_get_length(c); i++) {
            JsonObject *e = json_array_get_object_element(c, i);
            g_string_append_printf(s, "%s=%s\n", json_object_get_string_member_with_default(e, "uid", ""),
                                   json_object_get_string_member_with_default(e, "version", ""));
        }
    } else {
        g_string_append(s, "no pack\n");
    }
    g_object_unref(jp);
    g_free(p);
    /* patches/ (custom components) */
    p = g_build_filename(a->instdir, "patches", NULL);
    GDir *d = g_dir_open(p, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        char *f = g_build_filename(p, e, NULL);
        if (g_file_get_contents(f, &txt, NULL, NULL)) {
            g_string_append_printf(s, "%s\n%s", e, txt);
            g_free(txt);
        }
        g_free(f);
    }
    if (d)
        g_dir_close(d);
    g_free(p);
    p = g_build_filename(a->prism, "prismlauncher.cfg", NULL);
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **lines = g_strsplit(txt, "\n", -1);
        for (char **l = lines; *l; l++)
            if (g_str_has_prefix(*l, "JavaPath=") || g_str_has_prefix(*l, "JvmArgs="))
                g_string_append_printf(s, "%s\n", *l);
        g_strfreev(lines);
        g_free(txt);
    }
    g_free(p);
    char *exe = mc_sys_prism_exe();
    GStatBuf st;
    if (exe && g_stat(exe, &st) == 0)                     /* a Prism update changes its launch code */
        g_string_append_printf(s, "prism %s %ld %ld\n", exe, (long)st.st_size, (long)st.st_mtime);
    g_free(exe);
    if (g_getenv("TUX_MC_FAST_DEBUG"))
        fast_log("fast start key input:\n%s", s->str);
    char *k = g_compute_checksum_for_string(G_CHECKSUM_SHA256, s->str, s->len);
    g_string_free(s, TRUE);
    return k;
}

/* before a launch through Prism: the capture that comes out belongs to these settings */
void mc_fast_before_prism(app_t *a)
{
    char *cap = fast_path(a, "launch"), *kp = fast_path(a, "key"), *k = cache_key(a);
    char *dir = g_path_get_dirname(cap);
    g_mkdir_with_parents(dir, 0700);
    g_unlink(cap);
    g_file_set_contents(kp, k, -1, NULL);
    g_free(dir);
    g_free(k);
    g_free(kp);
    g_free(cap);
}

/* the active account's Minecraft token, fresh from accounts.json, if it has >= 10 minutes */
static char *account_token(app_t *a, char **uuid)
{
    char *path = g_build_filename(a->prism, "accounts.json", NULL);
    JsonParser *p = json_parser_new();
    char *tok = NULL;
    if (json_parser_load_from_file(p, path, NULL)) {
        JsonNode *root = json_parser_get_root(p);
        JsonArray *accs = root && JSON_NODE_HOLDS_OBJECT(root)
                              ? json_object_get_array_member(json_node_get_object(root), "accounts") : NULL;
        JsonObject *acc = NULL;
        for (guint i = 0; accs && i < json_array_get_length(accs); i++) {
            JsonObject *o = json_array_get_object_element(accs, i);
            if (!acc || (json_object_has_member(o, "active") && json_object_get_boolean_member(o, "active")))
                acc = o;
        }
        if (acc && json_object_has_member(acc, "ygg") && json_object_has_member(acc, "profile")) {
            JsonObject *y = json_object_get_object_member(acc, "ygg");
            gint64 exp = json_object_get_int_member_with_default(y, "exp", 0);
            const char *t = json_object_get_string_member_with_default(y, "token", "");
            if (*t && exp - g_get_real_time() / G_USEC_PER_SEC > 600) {
                tok = g_strdup(t);
                *uuid = g_strdup(json_object_get_string_member_with_default(
                    json_object_get_object_member(acc, "profile"), "id", ""));
            }
        }
    }
    g_object_unref(p);
    g_free(path);
    return tok;
}

/* -XX:AOTCache=<file> when there is one for this key, else -XX:AOTCacheOutput=<file> to make it
 * (at most once a day, in case that session never ends cleanly); NULL when Java is too old */
/* the feature version of the JDK a java binary belongs to (its release file), 0 if unknown */
static int java_feature(const char *java)
{
    char *bin = g_path_get_dirname(java), *home = g_path_get_dirname(bin);
    char *rel = g_build_filename(home, "release", NULL), *txt = NULL;
    int v = 0;
    if (g_file_get_contents(rel, &txt, NULL, NULL)) {
        char *at = strstr(txt, "JAVA_VERSION=\"");
        if (at)
            v = atoi(at + 14);
        g_free(txt);
    }
    g_free(rel);
    g_free(home);
    g_free(bin);
    return v;
}

static char *aot_arg(app_t *a, const char *key)
{
    char *n = safe_name(a->instance);
    char *dir = g_build_filename(g_get_user_cache_dir(), "tux-mc", "aot", NULL);
    char *name = g_strdup_printf("%s-%.16s.aot", n, key);
    char *f = g_build_filename(dir, name, NULL);
    char *mark = g_strconcat(f, ".training", NULL);
    char *r = NULL;
    g_mkdir_with_parents(dir, 0700);
    /* older caches of this instance belong to other settings: drop them */
    char *prefix = g_strdup_printf("%s-", n);
    GDir *d = g_dir_open(dir, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));)
        if (g_str_has_prefix(e, prefix) && strncmp(e, name, strlen(name))) {
            char *old = g_build_filename(dir, e, NULL);
            g_unlink(old);
            g_free(old);
        }
    if (d)
        g_dir_close(d);
    GStatBuf st;
    if (g_stat(f, &st) == 0 && st.st_size > 0) {
        r = g_strdup_printf("-XX:AOTCache=%s", f);
    } else if (g_stat(mark, &st) != 0 || g_get_real_time() / G_USEC_PER_SEC - st.st_mtime > 86400) {
        g_file_set_contents(mark, "", 0, NULL);
        r = g_strdup_printf("-XX:AOTCacheOutput=%s", f);
    }
    g_free(prefix);
    g_free(mark);
    g_free(f);
    g_free(name);
    g_free(dir);
    g_free(n);
    return r;
}

typedef struct {
    app_t *a;
    char *instdir;
    gint64 started;
} run_t;

/* Prism counts play time in instance.cfg; keep doing that when it wasn't there */
static void on_game_exit(GObject *src, GAsyncResult *res, gpointer data)
{
    run_t *r = data;
    g_subprocess_wait_finish(G_SUBPROCESS(src), res, NULL);
    gint64 secs = (g_get_real_time() - r->started) / G_USEC_PER_SEC;
    char *p = g_build_filename(r->instdir, "instance.cfg", NULL), *txt = NULL;
    if (!mc_sys_prism_running() && g_file_get_contents(p, &txt, NULL, NULL)) {
        GString *s = g_string_new("\n");
        g_string_append(s, txt);
        gint64 total = 0;
        char *t = strstr(s->str, "\ntotalTimePlayed=");
        if (t)
            total = g_ascii_strtoll(t + 17, NULL, 10);
        char buf[32];
        g_snprintf(buf, sizeof buf, "%" G_GINT64_FORMAT, total + secs);
        mc_ini_set(s, "totalTimePlayed", buf);
        g_snprintf(buf, sizeof buf, "%" G_GINT64_FORMAT, secs);
        mc_ini_set(s, "lastTimePlayed", buf);
        g_snprintf(buf, sizeof buf, "%" G_GINT64_FORMAT, r->started / 1000);
        mc_ini_set(s, "lastLaunchTime", buf);
        g_file_set_contents(p, s->str + 1, s->len - 1, NULL);
        g_string_free(s, TRUE);
        g_free(txt);
    }
    g_free(p);
    g_free(r->instdir);
    g_free(r);
    g_object_unref(src);
}

static void on_script_written(GObject *src, GAsyncResult *res, gpointer data)
{
    g_output_stream_write_all_finish(G_OUTPUT_STREAM(src), res, NULL, NULL);
    g_output_stream_close(G_OUTPUT_STREAM(src), NULL, NULL);
    memset(data, 0, strlen(data));            /* it holds the token */
    g_free(data);
}

/* every path in a -cp / -Djava.library.path argument exists */
static bool paths_exist(const char *list)
{
    char **v = g_strsplit(list, ":", -1);
    bool ok = true;
    for (char **e = v; *e && ok; e++)
        ok = !**e || g_file_test(*e, G_FILE_TEST_EXISTS);
    g_strfreev(v);
    return ok;
}

bool mc_fast_launch(app_t *a, const char *world, const char *server, gint64 t0)
{
    if (!mc_fast_enabled())
        return false;
    char *cap = fast_path(a, "launch"), *kp = fast_path(a, "key");
    char *txt = NULL, *key = NULL, *uuid = NULL, *tok = NULL, *now = NULL;
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *env = g_ptr_array_new_with_free_func(g_free);
    GString *script = g_string_new(NULL);
    char *cwd = NULL, *why = NULL;
    bool ok = false;

    if (!g_file_get_contents(kp, &key, NULL, NULL) || !g_file_get_contents(cap, &txt, NULL, NULL)) {
        why = "no capture yet";
        goto out;
    }
    now = cache_key(a);
    if (strcmp(g_strstrip(key), now)) {
        why = "settings changed";
        goto out;
    }
    if (!g_str_has_prefix(txt, "TUXMC-FAST 1\n")) {
        why = "unknown capture";
        goto out;
    }
    char *s = strstr(txt, "\nscript\n");
    if (!s) {
        why = "broken capture";
        goto out;
    }
    *s = 0;
    char **lines = g_strsplit(txt, "\n", -1);
    for (char **l = lines + 1; *l; l++) {
        if (g_str_has_prefix(*l, "cwd "))
            cwd = g_strdup(*l + 4);
        else if (g_str_has_prefix(*l, "env "))
            g_ptr_array_add(env, g_strdup(*l + 4));
        else if (g_str_has_prefix(*l, "arg "))
            g_ptr_array_add(argv, g_strdup(*l + 4));
    }
    g_strfreev(lines);
    /* java and its classpath / natives still there? */
    bool files = argv->len > 0 && cwd && g_file_test(cwd, G_FILE_TEST_IS_DIR);
    for (guint i = 0; files && i < argv->len; i++) {
        const char *v = argv->pdata[i];
        if (g_str_has_suffix(v, "/java") && *v == '/')
            files = g_file_test(v, G_FILE_TEST_IS_EXECUTABLE);
        else if ((!strcmp(v, "-cp") || !strcmp(v, "-classpath")) && i + 1 < argv->len)
            files = paths_exist(argv->pdata[i + 1]);
        else if (g_str_has_prefix(v, "-Djava.library.path=") && !strchr(v, ':'))
            g_mkdir_with_parents(v + 20, 0755);    /* Prism makes it per launch and removes it after */
    }
    if (!files) {
        why = "files moved";
        goto out;
    }
    tok = account_token(a, &uuid);
    if (!tok) {
        why = "token needs a refresh";
        goto out;
    }
    /* the script, with this token and this world / server */
    char *body = s + 8;
    char *want = g_strdup_printf("\nparam --uuid\nparam %s\n", uuid);
    bool same_account = strstr(body, want) != NULL;
    g_free(want);
    if (!same_account) {
        why = "another account";
        goto out;
    }
    char **sl = g_strsplit(body, "\n", -1);
    bool launch = false;
    for (char **l = sl; *l; l++) {
        if (g_str_has_prefix(*l, "worldName ") || g_str_has_prefix(*l, "serverAddress ")
            || g_str_has_prefix(*l, "serverPort "))
            continue;
        if (!strcmp(*l, "launch")) {
            if (world && *world)
                g_string_append_printf(script, "worldName %s\n", world);
            if (server && *server) {
                const char *colon = strrchr(server, ':');
                if (colon && !strchr(colon, ']'))
                    g_string_append_printf(script, "serverAddress %.*s\nserverPort %s\n", (int)(colon - server),
                                           server, colon + 1);
                else
                    g_string_append_printf(script, "serverAddress %s\nserverPort 25565\n", server);
            }
            g_string_append(script, "launch\n");
            launch = true;
            break;
        }
        char *t = str_replace(*l, "@TUXMC_TOKEN@", tok);
        g_string_append_printf(script, "%s\n", t);
        g_free(t);
    }
    g_strfreev(sl);
    if (!launch) {
        why = "broken capture";
        goto out;
    }

    GSubprocessLauncher *l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE);
    g_subprocess_launcher_set_cwd(l, cwd);
    for (guint i = 0; i < env->len; i++) {
        char *e = env->pdata[i], *eq = strchr(e, '=');
        if (eq) {
            *eq = 0;
            g_subprocess_launcher_setenv(l, e, eq + 1, TRUE);
            *eq = '=';
        }
    }
    char t0s[32];
    g_snprintf(t0s, sizeof t0s, "%" G_GINT64_FORMAT, t0);
    g_subprocess_launcher_setenv(l, "TUXMC_T0", t0s, TRUE);
    char *logf = g_build_filename(cwd, "logs", "tux-mc-console.log", NULL);
    g_subprocess_launcher_set_stdout_file_path(l, logf);
    g_subprocess_launcher_set_flags(l, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_free(logf);
    /* Java's AOT cache (JDK 25+): the classes Fabric, Mixin and the libraries load at every start,
     * kept ready-parsed. One game session writes it (on exit), the next ones read it: about 0.9 s
     * less to the title screen here. Tied to this launch (key), so a stale one never gets used;
     * Java itself also ignores a cache that doesn't match. */
    guint java_at = 0;
    for (guint i = 0; i < argv->len && !java_at; i++)
        if (g_str_has_suffix(argv->pdata[i], "/java"))
            java_at = i + 1;
    char *aot = java_at && mc_cfg_bool("aot_cache", true) && java_feature(argv->pdata[java_at - 1]) >= 25
                    ? aot_arg(a, now) : NULL;
    if (aot)
        g_ptr_array_insert(argv, java_at, aot);
    /* test hook: TUX_MC_JVM_EXTRA="-Xfoo -Xbar" goes in right after java (benchmarks, profiling) */
    const char *extra = g_getenv("TUX_MC_JVM_EXTRA");
    for (guint i = 0; extra && *extra && i < argv->len; i++)
        if (g_str_has_suffix(argv->pdata[i], "/java")) {
            char **xs = g_strsplit_set(extra, " ", -1);
            guint at = i + 1;
            for (char **x = xs; *x; x++)
                if (**x)
                    g_ptr_array_insert(argv, at++, g_strdup(*x));
            g_strfreev(xs);
            break;
        }
    g_ptr_array_add(argv, NULL);
    GError *err = NULL;
    GSubprocess *p = g_subprocess_launcher_spawnv(l, (const char *const *)argv->pdata, &err);
    g_object_unref(l);
    if (!p) {
        why = err ? err->message : "spawn failed";
        fast_log("fast start: %s", why);
        g_clear_error(&err);
        why = NULL;
        goto out;
    }
    gsize n = script->len;
    char *data = g_string_free(script, FALSE);
    script = NULL;
    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(p), data, n, G_PRIORITY_HIGH, NULL,
                                    on_script_written, data);
    run_t *r = g_new0(run_t, 1);
    r->a = a;
    r->instdir = g_strdup(a->instdir);
    r->started = g_get_real_time();
    g_subprocess_wait_async(p, NULL, on_game_exit, r);
    ok = true;
out:
    if (why)
        fast_log("fast start skipped: %s", why);
    if (script)
        g_string_free(script, TRUE);
    if (tok) {
        memset(tok, 0, strlen(tok));
        g_free(tok);
    }
    g_free(uuid);
    g_free(cwd);
    g_free(now);
    g_free(key);
    g_free(txt);
    g_free(cap);
    g_free(kp);
    g_ptr_array_free(argv, TRUE);
    g_ptr_array_free(env, TRUE);
    return ok;
}

#else /* Windows: Prism every time */

bool mc_fast_enabled(void) { return false; }
char *mc_fast_wrap_prefix(app_t *a) { (void)a; return NULL; }
void mc_fast_before_prism(app_t *a) { (void)a; }
bool mc_fast_launch(app_t *a, const char *world, const char *server, gint64 t0)
{
    (void)a; (void)world; (void)server; (void)t0;
    return false;
}

#endif
