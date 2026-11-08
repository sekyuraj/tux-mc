/* mc.h — shared state of tux-mc, the Minecraft launcher (see mc.c). */
#pragma once
#include "mc3d.h"

#include <gtk/gtk.h>
#include <stdbool.h>

#define MAXCAPE 32

enum { PAL_FG, PAL_DIM, PAL_ACCENT, PAL_ACCENT2, PAL_BG, PAL_WARN, PAL_SURFACE, PAL_BRIGHT, PAL_N };

typedef struct app app_t;

/* a head model, see mc-hat.c */
typedef struct {
    int n;                               /* 16 or 32 cells a side */
    guint32 *c;                          /* n^3 ARGB, 0 = empty; index (y * n + z) * n + x */
    char name[64];
} mc_hat_t;

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
    char *mc_version, *loader;           /* of the instance: "26.3", "fabric" ("" = vanilla) */

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
    mc_hat_t hat;                        /* the one you wear (n = 0: none) */

    /* looks (Settings → Launcher), see mc_apply_looks */
    int bg_blur;
    bool bg_motion, bg_dust;

    /* chrome */
    GtkWidget *bg, *stack, *nav[12], *status;
    int npages;

    /* home */
    mc3d_t view;
    GtkWidget *stage, *start, *start_label, *none_tile, *tux_chips[32], *sub, *cape_flow;
    GPtrArray *pic_chips;                /* the saved picture capes' chips (not owned) */
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
    /* instances page */
    GtkWidget *inst_list, *inst_name, *inst_ver, *inst_snaps, *inst_loader, *inst_fapi, *inst_create, *inst_spin,
        *inst_msg;
    GPtrArray *mc_versions;              /* "r:1.21.1" / "s:24w14a", newest first */
    /* updates (Settings) */
    GtkWidget *upd_state, *upd_button, *upd_spin;
    bool upd_busy, upd_installed, restart;
    const char *open_page;               /* page to show when the window is (re)built */
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
/* bring the game window of this instance to the front (after it started) */
bool mc_sys_focus_game(const char *instdir);

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
char *mc_http_location(const char *url);
long mc_http_upload_skin(const char *token, const char *variant, const char *path, GByteArray *out);
/* a fresh Minecraft token (refreshing through Xbox if needed); NULL on failure. Blocking. */
char *mc_auth_token(app_t *a, char **err);
void mc_load_account(app_t *a);

/* mc-instances.c */
GtkWidget *mc_page_instances(app_t *a);
void mc_instances_refresh(app_t *a);
void mc_inst_read_pack(const char *instdir, char **mc, char **loader, char **loader_ver);
const char *mc_loader_name(const char *slug);
const char *mc_loader_slug(app_t *a);     /* Modrinth loader of the instance (fabric for vanilla) */
/* mc-setup.c: newest Fabric API for Minecraft mc into the mods folder. Blocking. */
bool mc_fabric_api(const char *mods, const char *mc, char **err);
bool mc_modrinth_mod(const char *mods, const char *slug, const char *title, const char *prefix, const char *mc,
                     char **err);

/* mc-zip.c: zips without an unzip program. read: the first entry matching a
 * glob (shaders/ANY.properties). extract: entries under prefix (may be NULL)
 * into dest, refusing names that would leave it */
GBytes *mc_zip_read(const char *zip, const char *pattern);
char **mc_zip_list(const char *zip);
bool mc_zip_extract(const char *zip, const char *prefix, const char *dest, char **err);

/* mc-import.c: the Import page (worlds, schematics, packs from other launchers or files) */
GtkWidget *mc_page_import(app_t *a);

/* mc-themes.c: colour presets for the launcher (and Prism) */
GtkWidget *mc_themes_card(app_t *a);

/* mc-account.c: the account box in the sidebar opens this */
GtkWidget *mc_account_button(app_t *a);

/* mc-capeedit.c: crop/stretch a picture into a cape, then use it */
void mc_cape_editor(app_t *a, const char *path, const char *edit_id, void (*done)(app_t *a, const char *saved));
char *mc_capes_dir(void);                /* saved picture capes: <data>/tux-mc/capes */

/* mc-update.c: new builds from the GitHub releases */
GtkWidget *mc_update_card(app_t *a);
void mc_update_startup(app_t *a);        /* clean up after the last update, check if asked to */
void mc_update_restart(const char *argv0);   /* after the app quit: start the new build */

/* mc.c: rebuild the window (another instance was picked), showing page */
void mc_reload_window(app_t *a, const char *page);

/* mc-colour.c: hue ring + shade square + hex (+ opacity); the button opens it in a popover */
GtkWidget *mc_colour_wheel_new(bool alpha, int size);
GtkWidget *mc_colour_button_new(bool alpha);
void mc_colour_get(GtkWidget *w, GdkRGBA *c);
void mc_colour_set(GtkWidget *w, const GdkRGBA *c);   /* calls the change callback */
void mc_colour_on_change(GtkWidget *w, void (*cb)(GtkWidget *w, gpointer data), gpointer data);

/* mc-hat.c */
void mc_hat_new(mc_hat_t *h, int n);
void mc_hat_free(mc_hat_t *h);
void mc_hat_copy(mc_hat_t *to, const mc_hat_t *from);
int mc_hat_count(const mc_hat_t *h);
bool mc_hat_is_head(int n, int x, int y, int z);
GBytes *mc_hat_encode(const mc_hat_t *h);
bool mc_hat_decode(const guint8 *data, gsize len, mc_hat_t *h);
bool mc_hat_load(const char *path, mc_hat_t *h);
bool mc_hat_save(const char *path, const mc_hat_t *h);
char *mc_hats_dir(void);                  /* <data>/tux-mc/hats */
int mc_hat_presets(void);
const char *mc_hat_preset_name(int i);
void mc_hat_preset(mc_hat_t *h, int i);
bool mc_hat_import_vox(const char *path, mc_hat_t *h, char **err);
void mc_hat_resize(mc_hat_t *h, int n);
/* mc-hats.c: the Hats page (editor); the worn hat is in <instance>/config/tuxmc/hat.tuxhat */
GtkWidget *mc_page_hats(app_t *a);
void mc_hat_load_worn(app_t *a);

/* mc-packs.c: the Packs page (resource pack maker) */
GtkWidget *mc_page_packs(app_t *a);

/* mc-zip.c: write a folder as a zip; open one zip for many reads by exact name */
bool mc_zip_write_dir(const char *dir, const char *zip, char **err);
typedef struct mc_zip mc_zip_t;
mc_zip_t *mc_zip_open(const char *zip);
GBytes *mc_zip_get(mc_zip_t *z, const char *name);
void mc_zip_names(mc_zip_t *z, void (*each)(const char *name, void *data), void *data);
void mc_zip_close(mc_zip_t *z);

/* mc-menubg.c: Settings → Title screen (the game's main menu background) */
GtkWidget *mc_menubg_card(app_t *a);

/* pages */
GtkWidget *mc_page_home(app_t *a);
GtkWidget *mc_page_skins(app_t *a);
GtkWidget *mc_page_mods(app_t *a);
GtkWidget *mc_page_configs(app_t *a);
GtkWidget *mc_page_settings(app_t *a);
void mc_home_refresh_capes(app_t *a);
void mc_settings_apply(app_t *a);        /* write instance.cfg (Prism must not be running) */
void mc_skins_reload(app_t *a);
void mc_skin_upload(app_t *a, const char *path, bool slim);   /* async, reports in the status line */
void mc_mods_scan(app_t *a);              /* async; refreshes the Mods and Configs pages */
void mc_configs_refresh(app_t *a);
void mc_apply_looks(app_t *a);            /* launcher look settings changed */
