/* mc-sys.c — the parts of tux-mc that differ between Linux and Windows:
 * finding Prism and its data folder, looking for running processes, RAM. */
#include "mc.h"

#include <json-glib/json-glib.h>

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#else
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------ Prism paths -- */

/* where tux-mc puts a Prism it installed itself */
char *mc_sys_own_prism_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), "tux-mc", "prism", NULL);
}

static char *exe_if(char *p)
{
    if (p && g_file_test(p, G_FILE_TEST_IS_EXECUTABLE))
        return p;
    g_free(p);
    return NULL;
}

/* the Prism executable, or NULL when Prism isn't installed */
char *mc_sys_prism_exe(void)
{
    const char *env = g_getenv("TUX_MC_PRISM");
    if (env && *env)
        return g_strdup(env);
    char *own = mc_sys_own_prism_dir(), *p;
#ifdef _WIN32
    p = exe_if(g_build_filename(own, "prismlauncher.exe", NULL));
    if (!p) {                            /* the zip may unpack into a folder of its own */
        GDir *d = g_dir_open(own, 0, NULL);
        const char *f;
        while (d && !p && (f = g_dir_read_name(d)))
            p = exe_if(g_build_filename(own, f, "prismlauncher.exe", NULL));
        if (d)
            g_dir_close(d);
    }
    if (!p) {
        const char *roots[] = {g_getenv("LOCALAPPDATA"), g_getenv("ProgramFiles"), g_getenv("ProgramFiles(x86)")};
        const char *subs[] = {"Programs\\PrismLauncher", "PrismLauncher", "PrismLauncher"};
        for (int i = 0; i < 3 && !p; i++)
            if (roots[i])
                p = exe_if(g_build_filename(roots[i], subs[i], "prismlauncher.exe", NULL));
    }
    if (!p)
        p = g_find_program_in_path("prismlauncher.exe");
#else
    p = g_find_program_in_path("prismlauncher");
    if (!p)
        p = exe_if(g_build_filename(own, "squashfs-root", "AppRun", NULL));
#endif
    g_free(own);
    return p;
}

/* Prism's data folder (accounts.json, instances/, prismlauncher.cfg) */
char *mc_sys_prism_dir(void)
{
    const char *env = g_getenv("PRISM_DIR");
    if (env && *env)
        return g_strdup(env);
    /* a portable Prism keeps its data next to the executable */
    char *exe = mc_sys_prism_exe();
    if (exe) {
        char *dir = g_path_get_dirname(exe), *marker = g_build_filename(dir, "portable.txt", NULL);
        bool portable = g_file_test(marker, G_FILE_TEST_EXISTS);
        g_free(marker);
        g_free(exe);
        if (portable)
            return dir;
        g_free(dir);
    }
#ifdef _WIN32
    const char *appdata = g_getenv("APPDATA");
    return g_build_filename(appdata ? appdata : g_get_user_config_dir(), "PrismLauncher", NULL);
#else
    char *p = g_build_filename(g_get_user_data_dir(), "PrismLauncher", NULL);
    char *flat = g_build_filename(g_get_home_dir(), ".var", "app", "org.prismlauncher.PrismLauncher", "data",
                                  "PrismLauncher", NULL);
    if (!g_file_test(p, G_FILE_TEST_IS_DIR) && g_file_test(flat, G_FILE_TEST_IS_DIR)) {
        g_free(p);
        return flat;
    }
    g_free(flat);
    return p;
#endif
}

/* ------------------------------------------------------------- processes -- */

#ifdef _WIN32
typedef NTSTATUS(NTAPI *query_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);

/* a process's command line (Windows 8.1+), UTF-8, or NULL */
static char *cmdline_of(DWORD pid)
{
    static query_fn q;
    if (!q)
        q = (query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h || !q)
        return NULL;
    ULONG len = 0;
    char *res = NULL;
    q(h, 60 /* ProcessCommandLineInformation */, NULL, 0, &len);
    if (len) {
        UNICODE_STRING *u = g_malloc0(len + 2);
        if (q(h, 60, u, len, &len) >= 0 && u->Buffer)
            res = g_utf16_to_utf8((gunichar2 *)u->Buffer, u->Length / 2, NULL, NULL, NULL);
        g_free(u);
    }
    CloseHandle(h);
    return res;
}

/* calls fn for every process; stops when fn returns true */
static bool each_process(bool (*fn)(PROCESSENTRY32W *, void *), void *data)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W e = {.dwSize = sizeof e};
    bool hit = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok && !hit; ok = Process32NextW(snap, &e))
        hit = fn(&e, data);
    CloseHandle(snap);
    return hit;
}

static bool is_prism(PROCESSENTRY32W *e, void *d)
{
    (void)d;
    return _wcsicmp(e->szExeFile, L"prismlauncher.exe") == 0;
}

static bool kill_prism(PROCESSENTRY32W *e, void *d)
{
    (void)d;
    if (is_prism(e, NULL)) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, e->th32ProcessID);
        if (h) {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }
    return false;
}

static DWORD found_pid;

static bool java_with(PROCESSENTRY32W *e, void *needle)
{
    if (_wcsicmp(e->szExeFile, L"javaw.exe") && _wcsicmp(e->szExeFile, L"java.exe"))
        return false;
    char *cl = cmdline_of(e->th32ProcessID);
    bool hit = false;
    if (cl) {
        /* Prism may write either slash */
        g_strdelimit(cl, "/", '\\');
        char *n = g_strdup(needle);
        g_strdelimit(n, "/", '\\');
        char *lc = g_utf8_strdown(cl, -1), *ln = g_utf8_strdown(n, -1);
        hit = strstr(lc, ln) != NULL;
        if (hit)
            found_pid = e->th32ProcessID;
        g_free(lc);
        g_free(ln);
        g_free(n);
        g_free(cl);
    }
    return hit;
}
#else
/* any process whose command line contains needle (reads /proc, no pgrep) */
static int found_pid;

static bool cmdline_has(const char *needle, const char *exact_comm)
{
    DIR *d = opendir("/proc");
    if (!d)
        return false;
    bool hit = false;
    struct dirent *e;
    while (!hit && (e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        char path[64], *txt = NULL;
        gsize len = 0;
        if (exact_comm) {
            g_snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
            if (g_file_get_contents(path, &txt, &len, NULL)) {
                g_strchomp(txt);
                hit = strcmp(txt, exact_comm) == 0;
            }
        } else {
            g_snprintf(path, sizeof path, "/proc/%s/cmdline", e->d_name);
            if (g_file_get_contents(path, &txt, &len, NULL) && len) {
                for (gsize i = 0; i + 1 < len; i++)
                    if (!txt[i])
                        txt[i] = ' ';
                hit = strstr(txt, needle) != NULL;
                if (hit)
                    found_pid = atoi(e->d_name);
            }
        }
        g_free(txt);
    }
    closedir(d);
    return hit;
}
#endif

bool mc_sys_prism_running(void)
{
#ifdef _WIN32
    return each_process(is_prism, NULL);
#else
    /* the AppImage runs as "prismlauncher" too; comm is cut at 15 chars */
    return cmdline_has(NULL, "prismlauncher");
#endif
}

void mc_sys_stop_prism(void)
{
#ifdef _WIN32
    each_process(kill_prism, NULL);
#else
    g_spawn_command_line_sync("pkill -x prismlauncher", NULL, NULL, NULL, NULL);
#endif
    for (int i = 0; i < 30 && mc_sys_prism_running(); i++)
        g_usleep(100000);
}

/* is Minecraft from this instance running? Prism passes the instance's
 * natives folder to java, which no other game has */
bool mc_sys_game_running(const char *instdir)
{
    char *needle = g_strdup_printf("-Djava.library.path=%s", instdir);
#ifdef _WIN32
    bool r = each_process(java_with, needle);
#else
    bool r = cmdline_has(needle, NULL);
#endif
    g_free(needle);
    return r;
}

/* -------------------------------------------------------------- focusing -- */

#ifdef _WIN32
typedef struct { DWORD pid; HWND best; } win_find_t;

static BOOL CALLBACK find_window(HWND h, LPARAM lp)
{
    win_find_t *f = (win_find_t *)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == f->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && GetWindowTextLengthW(h) > 0) {
        f->best = h;
        return FALSE;
    }
    return TRUE;
}
#endif

/* bring this instance's game window to the front; false when there's no way
 * to on this desktop (the caller then gets out of the way instead) */
bool mc_sys_focus_game(const char *instdir)
{
    char *needle = g_strdup_printf("-Djava.library.path=%s", instdir);
    found_pid = 0;
#ifdef _WIN32
    bool running = each_process(java_with, needle);
    g_free(needle);
    if (!running || !found_pid)
        return false;
    win_find_t f = {found_pid, NULL};
    EnumWindows(find_window, (LPARAM)&f);
    if (!f.best)
        return false;
    if (IsIconic(f.best))
        ShowWindow(f.best, SW_RESTORE);
    /* Windows only lets the foreground app hand focus over: borrow its input queue */
    HWND fg = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), them = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    if (them && them != me)
        AttachThreadInput(them, me, TRUE);
    BringWindowToTop(f.best);
    bool ok = SetForegroundWindow(f.best);
    if (them && them != me)
        AttachThreadInput(them, me, FALSE);
    return ok;
#else
    bool running = cmdline_has(needle, NULL);
    g_free(needle);
    if (!running || !found_pid)
        return false;
    char *cmd = NULL;
    if (g_getenv("NIRI_SOCKET")) {
        /* niri: the window whose pid is the game's */
        char *out = NULL;
        if (g_spawn_command_line_sync("niri msg -j windows", &out, NULL, NULL, NULL) && out) {
            JsonParser *p = json_parser_new();
            if (json_parser_load_from_data(p, out, -1, NULL) && JSON_NODE_HOLDS_ARRAY(json_parser_get_root(p))) {
                JsonArray *ws = json_node_get_array(json_parser_get_root(p));
                for (guint i = 0; i < json_array_get_length(ws) && !cmd; i++) {
                    JsonObject *w = json_array_get_object_element(ws, i);
                    if (json_object_get_int_member_with_default(w, "pid", -1) == found_pid)
                        cmd = g_strdup_printf("niri msg action focus-window --id %" G_GINT64_FORMAT,
                                              json_object_get_int_member(w, "id"));
                }
            }
            g_object_unref(p);
        }
        g_free(out);
    } else if (g_getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
        cmd = g_strdup_printf("hyprctl dispatch focuswindow pid:%d", found_pid);
    } else if (g_getenv("SWAYSOCK")) {
        cmd = g_strdup_printf("swaymsg [pid=%d] focus", found_pid);
    } else if (g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) {
        char *x = g_find_program_in_path("xdotool");
        if (x)
            cmd = g_strdup_printf("%s search --pid %d --onlyvisible windowactivate", x, found_pid);
        g_free(x);
    }
    if (!cmd)
        return false;
    int status = 0;
    bool ok = g_spawn_command_line_sync(cmd, NULL, NULL, &status, NULL) && g_spawn_check_wait_status(status, NULL);
    g_free(cmd);
    return ok;
#endif
}

/* --------------------------------------------------------------------- RAM -- */
int mc_sys_ram_gb(void)
{
#ifdef _WIN32
    MEMORYSTATUSEX m = {.dwLength = sizeof m};
    if (GlobalMemoryStatusEx(&m))
        return (int)MAX(2, m.ullTotalPhys / (1024ull * 1024 * 1024));
    return 16;
#else
    char *txt = NULL;
    long kb = 0;
    if (g_file_get_contents("/proc/meminfo", &txt, NULL, NULL)) {
        char *m = strstr(txt, "MemTotal:");
        if (m)
            kb = atol(m + 9);
        g_free(txt);
    }
    return kb > 0 ? (int)MAX(2, kb / (1024 * 1024)) : 16;
#endif
}

/* default heap for this machine (MB): a quarter of the RAM, 3 to 8 GB, never over half.
 * The mod stack with Sodium sits around 1.5-3 GB at normal render distances; ZGC
 * wants room on top of that, but more than 8 GB only helps huge render distances. */
int mc_sys_default_heap_mb(void)
{
    int ram = mc_sys_ram_gb() * 1024;
    int mb = CLAMP(ram / 4, 3072, 8192);
    return MIN(mb, MAX(1024, ram / 2));
}

#ifndef _WIN32
static char *sysfs(const char *dev, const char *file)
{
    char *p = g_build_filename(dev, file, NULL), *txt = NULL;
    g_file_get_contents(p, &txt, NULL, NULL);
    g_free(p);
    return txt ? g_strstrip(txt) : NULL;
}

static char *driver_of(const char *dev)
{
    char *p = g_build_filename(dev, "driver", NULL);
    char *l = g_file_read_link(p, NULL);
    g_free(p);
    char *b = l ? g_path_get_basename(l) : NULL;
    g_free(l);
    return b;
}
#endif

/* hybrid graphics (laptops): when the screen hangs off an integrated GPU and a second,
 * dedicated one is there, the environment that makes the game render on the second.
 * NULL = nothing to do (one GPU, or the dedicated one already drives the screen). */
char *mc_sys_gpu_offload_env(void)
{
#ifdef _WIN32
    return NULL;
#else
    GDir *d = g_dir_open("/sys/class/drm", 0, NULL);
    char *prim_vendor = NULL, *other_vendor = NULL, *other_driver = NULL;
    gint64 prim_vram = -1;
    for (const char *e; d && (e = g_dir_read_name(d));) {
        if (strncmp(e, "card", 4) || strchr(e, '-'))
            continue;
        char *dev = g_build_filename("/sys/class/drm", e, "device", NULL);
        char *vendor = sysfs(dev, "vendor"), *boot = sysfs(dev, "boot_vga");
        if (vendor && boot && !strcmp(boot, "1") && !prim_vendor) {
            prim_vendor = g_strdup(vendor);
            char *v = sysfs(dev, "mem_info_vram_total");
            prim_vram = v ? g_ascii_strtoll(v, NULL, 10) : -1;
            g_free(v);
        } else if (vendor && !other_vendor) {
            other_vendor = g_strdup(vendor);
            other_driver = driver_of(dev);
        }
        g_free(vendor);
        g_free(boot);
        g_free(dev);
    }
    if (d)
        g_dir_close(d);
    char *r = NULL;
    /* integrated: Intel, or AMD with a small carve-out instead of real VRAM */
    bool integrated = prim_vendor && (!strcmp(prim_vendor, "0x8086")
                                      || (!strcmp(prim_vendor, "0x1002") && prim_vram >= 0
                                          && prim_vram < 4LL * 1024 * 1024 * 1024));
    if (integrated && other_vendor && other_driver) {
        if (!strcmp(other_vendor, "0x10de") && !strcmp(other_driver, "nvidia"))
            r = g_strdup("__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia __VK_LAYER_NV_optimus=NVIDIA_only");
        else if (!strcmp(other_driver, "amdgpu") || !strcmp(other_driver, "nouveau")
                 || !strcmp(other_driver, "i915") || !strcmp(other_driver, "xe"))
            r = g_strdup("DRI_PRIME=1");
    }
    g_free(prim_vendor);
    g_free(other_vendor);
    g_free(other_driver);
    return r;
#endif
}

/* ------------------------------------------------------------- launching -- */

/* starts Prism with args (NULL-terminated); false + err when that failed */
bool mc_sys_run_prism(const char *const *args, GError **err)
{
    char *exe = mc_sys_prism_exe();
    if (!exe) {
        g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Prism Launcher isn't installed");
        return false;
    }
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, exe);
    for (int i = 0; args && args[i]; i++)
        g_ptr_array_add(argv, (char *)args[i]);
    g_ptr_array_add(argv, NULL);
    bool ok = g_spawn_async(NULL, (char **)argv->pdata, NULL,
                            G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, err);
    g_ptr_array_free(argv, TRUE);
    g_free(exe);
    return ok;
}
