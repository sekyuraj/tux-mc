/* mc.h — shared state of tux-mc, the Minecraft launcher (see mc.c). */
#pragma once
#include "mc3d.h"

#include <gtk/gtk.h>
#include <stdbool.h>

#define MAXCAPE 32

enum { PAL_FG, PAL_DIM, PAL_ACCENT, PAL_ACCENT2, PAL_BG, PAL_WARN, PAL_SURFACE, PAL_BRIGHT, PAL_N };

typedef struct app app_t;

typedef struct {
    char *id, *alias, *url;
    cairo_surface_t *tex;
    GtkWidget *tile, *area;
    app_t *a;
} cape_t;

struct app {
    GtkApplication *gapp;
    GtkWindow *win;
    GtkCssProvider *css;
    GFileMonitor *css_monitor;
    GtkWidget *pal[PAL_N];

    /* Prism */
    char *prism, *instance, *instdir, *mcdir, *log_path;

    /* account (accounts.json) */
    char *name, *uuid;
    char *token;                         /* Minecraft access token */
    gint64 token_exp;
    char *xsts, *uhs;                    /* XSTS token for api.minecraftservices.com */
    gint64 xsts_exp;
    char *msa_refresh, *msa_client;
    cairo_surface_t *skin;
    bool slim;
    cape_t capes[MAXCAPE];
    int ncapes;
    char *active_cape;
    bool busy;

    char *tux_cape;
    cairo_surface_t *tux_cape_tex;       /* the client cape, painted by libtuxmc */

    /* looks (Settings → Launcher), see mc_apply_looks */
    int bg_blur;
    bool bg_motion, bg_dust;

    /* chrome */
    GtkWidget *bg, *stack, *nav[8], *status;
    int npages;

    /* home */
    mc3d_t view;
    GtkWidget *stage, *start, *start_label, *none_tile, *tux_chips[32], *sub;
    double drag_x, drag_y, drag_vx;
    gint64 drag_t;
    gint64 last;
    double glow, progress, progress_target;
    bool launching;
    guint watch_src;
    off_t log_seen;

    /* skins page */
    mc3d_t skin_view;
    GtkWidget *skin_stage, *skin_flow, *skin_slim, *skin_name_entry, *skin_apply, *skin_remove;
    cairo_surface_t *skin_pick;          /* the skin being previewed */
    char *skin_pick_path;

    /* mods (scanned once, shared by the Mods and Configs pages) */
    GPtrArray *mods;                     /* mc_mod_t */
    /* mods page */
    GtkWidget *mods_list, *mods_search, *mods_stack, *mr_list, *mr_entry, *mr_spinner;
    /* configs page */
    GtkWidget *cfg_stack, *cfg_grid, *cfg_files, *cfg_view, *cfg_title, *cfg_save, *cfg_gutter, *cfg_search,
        *cfg_searchbar, *cfg_mod_title, *cfg_mod_icon, *cfg_info, *cfg_tidy;
    char *cfg_path, *cfg_mod;
    bool cfg_dirty, cfg_loading;
    /* settings page */
    GtkWidget *set_mem, *set_mem_label, *set_glthread, *set_gamemode, *set_zgc, *set_wayland, *set_close,
        *set_fullscreen;
};

typedef struct {
    char *file, *id, *name, *version, *desc;
    GBytes *icon;
    bool enabled;
} mc_mod_t;

/* mc.c */
GtkWidget *mc_label(const char *text, const char *cls);
GtkWidget *mc_card(const char *title, const char *subtitle);
void mc_status(app_t *a, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void mc_pal(app_t *a, int i, GdkRGBA *c);
cairo_surface_t *mc_png_bytes(GBytes *b);
bool mc_prism_running(void);

/* mc-sys.c: what differs between Linux and Windows */
#ifdef _WIN32
#define MC_LINUX 0
#else
#define MC_LINUX 1
#endif
char *mc_sys_own_prism_dir(void);
char *mc_sys_prism_exe(void);
char *mc_sys_prism_dir(void);
bool mc_sys_prism_running(void);
void mc_sys_stop_prism(void);
bool mc_sys_game_running(const char *instdir);
int mc_sys_ram_gb(void);
bool mc_sys_run_prism(const char *const *args, GError **err);

/* mc-setup.c: first run without Prism, an account or the instance */
bool mc_setup_needed(app_t *a);
void mc_setup_show(app_t *a);
void mc_paths(app_t *a);
void mc_open_main(app_t *a);
bool mc_game_running(app_t *a);
/* open a folder or file in the default app (no portal: it can't open folders here) */
void mc_open_path(app_t *a, const char *path);
/* config/tuxmc.properties, the mod's settings */
char *mc_tuxmc_get(app_t *a, const char *key);
void mc_tuxmc_set(app_t *a, const char *key, const char *value);
GdkTexture *mc_mod_icon(const mc_mod_t *m);

GtkWidget *mc_stage(app_t *a, mc3d_t *v, GtkDrawingAreaDrawFunc draw);
/* launcher settings: ~/.config/tux-mc/settings.ini [launcher] */
bool mc_cfg_bool(const char *key, bool def);
int mc_cfg_int(const char *key, int def);
void mc_cfg_set_bool(const char *key, bool v);
void mc_cfg_set_int(const char *key, int v);
char *mc_cfg_str(const char *key, const char *def);
void mc_cfg_set_str(const char *key, const char *v);

/* mc-net.c: blocking, call from threads (GTask) */
long mc_http(const char *method, const char *url, const char *token, const char *ctype, const char *body,
             GByteArray *out);
bool mc_http_download(const char *url, const char *path, char **err);
long mc_http_upload_skin(const char *token, const char *variant, const char *path, GByteArray *out);
/* a fresh Minecraft token (refreshing through Xbox if needed); NULL on failure. Blocking. */
char *mc_auth_token(app_t *a, char **err);
void mc_load_account(app_t *a);

/* pages */
GtkWidget *mc_page_home(app_t *a);
GtkWidget *mc_page_skins(app_t *a);
GtkWidget *mc_page_mods(app_t *a);
GtkWidget *mc_page_configs(app_t *a);
GtkWidget *mc_page_settings(app_t *a);
void mc_home_refresh_capes(app_t *a);
void mc_settings_apply(app_t *a);        /* write instance.cfg (Prism must not be running) */
void mc_skins_reload(app_t *a);
void mc_mods_scan(app_t *a);              /* async; refreshes the Mods and Configs pages */
void mc_configs_refresh(app_t *a);
void mc_apply_looks(app_t *a);            /* launcher look settings changed */
