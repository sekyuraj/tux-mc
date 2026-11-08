/* mc-menubg.c — Settings → Title screen: your picture behind the game's
 * menus instead of the spinning world, with blur, darkening and a slight
 * follow-the-mouse drift. The mod draws it (MenuBg.java); this writes
 * menuBg / menuBlur / menuDim / menuParallax into tuxmc.properties and keeps
 * a copy of the picture in <instance>/config/tuxmc/menu-bg.png.
 */
#include "mc.h"

#include <math.h>
#include <string.h>

static struct {
    app_t *a;
    GtkWidget *area, *blur, *dim, *drift, *remove, *msg;
    cairo_surface_t *pic;                /* the picture, at most 1280 wide */
    bool loading;
} M;

static char *bg_file(app_t *a) { return g_build_filename(a->mcdir, "config", "tuxmc", "menu-bg.png", NULL); }

static double prop(const char *key, double def)
{
    char *v = mc_tuxmc_get(M.a, key);
    double d = v && *v ? g_ascii_strtod(v, NULL) : def;
    g_free(v);
    return d;
}

static void load_pic(void)
{
    g_clear_pointer(&M.pic, cairo_surface_destroy);
    char *v = g_getenv("TUX_MC_MENUBG") ? g_strdup(g_getenv("TUX_MC_MENUBG")) : mc_tuxmc_get(M.a, "menuBg");
    GdkTexture *t = v && *v ? gdk_texture_new_from_filename(v, NULL) : NULL;
    g_free(v);
    if (t) {
        int w = gdk_texture_get_width(t), h = gdk_texture_get_height(t);
        cairo_surface_t *full = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        gdk_texture_download(t, cairo_image_surface_get_data(full), cairo_image_surface_get_stride(full));
        cairo_surface_mark_dirty(full);
        double k = fmin(1, 1280.0 / w);
        int sw = MAX(1, (int)(w * k)), sh = MAX(1, (int)(h * k));
        M.pic = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
        cairo_t *cr = cairo_create(M.pic);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, full, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(full);
        g_object_unref(t);
    }
    gtk_widget_set_sensitive(M.remove, M.pic != NULL);
}

/* the same running-sum box blur as the mod (3 passes each way) */
static void box_blur(guint32 *px, int w, int h, int stride, int r)
{
    for (int pass = 0; pass < 6; pass++) {
        bool rows = !(pass & 1);
        int len = rows ? w : h, lines = rows ? h : w;
        guint32 *line = g_new(guint32, len);
        for (int l = 0; l < lines; l++) {
            for (int i = 0; i < len; i++)
                line[i] = px[rows ? l * stride + i : i * stride + l];
            int s[3] = {0}, n = 2 * r + 1;
            for (int i = -r; i <= r; i++) {
                guint32 c = line[CLAMP(i, 0, len - 1)];
                s[0] += c >> 16 & 255; s[1] += c >> 8 & 255; s[2] += c & 255;
            }
            for (int i = 0; i < len; i++) {
                px[rows ? l * stride + i : i * stride + l] = 0xFF000000u | (s[0] / n) << 16 | (s[1] / n) << 8 | (s[2] / n);
                guint32 a = line[MIN(len - 1, i + r + 1)], b = line[MAX(0, i - r)];
                s[0] += (int)(a >> 16 & 255) - (int)(b >> 16 & 255);
                s[1] += (int)(a >> 8 & 255) - (int)(b >> 8 & 255);
                s[2] += (int)(a & 255) - (int)(b & 255);
            }
        }
        g_free(line);
    }
}

static void mock_button(cairo_t *cr, double x, double y, double w, double h, const char *label)
{
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.85);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
    cairo_set_font_size(cr, h * 0.48);
    cairo_text_extents_t e;
    cairo_text_extents(cr, label, &e);
    cairo_move_to(cr, x + (w - e.width) / 2 - e.x_bearing, y + h * 0.68);
    cairo_show_text(cr, label);
}

static void draw(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    (void)data;
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_clip(cr);
    if (!M.pic) {
        GdkRGBA c;
        mc_pal(M.a, PAL_SURFACE, &c);
        gdk_cairo_set_source_rgba(cr, &c);
        cairo_paint(cr);
        mc_pal(M.a, PAL_DIM, &c);
        gdk_cairo_set_source_rgba(cr, &c);
        cairo_set_font_size(cr, 14);
        const char *t = "No picture: the game shows its usual panorama";
        cairo_text_extents_t e;
        cairo_text_extents(cr, t, &e);
        cairo_move_to(cr, (w - e.width) / 2, h / 2.0);
        cairo_show_text(cr, t);
        return;
    }
    /* cover-fit into a w x h surface, blur it at the preview's scale, darken */
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *c2 = cairo_create(s);
    int pw = cairo_image_surface_get_width(M.pic), ph = cairo_image_surface_get_height(M.pic);
    double zoom = gtk_switch_get_active(GTK_SWITCH(M.drift)) ? 1.05 : 1;
    double k = fmax((double)w / pw, (double)h / ph) * zoom;
    cairo_translate(c2, (w - pw * k) / 2, (h - ph * k) / 2);
    cairo_scale(c2, k, k);
    cairo_set_source_surface(c2, M.pic, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(c2), CAIRO_FILTER_GOOD);
    cairo_paint(c2);
    cairo_destroy(c2);
    cairo_surface_flush(s);
    int r = (int)lround(gtk_range_get_value(GTK_RANGE(M.blur)) * h / 1080.0);
    if (r > 0)
        box_blur((guint32 *)cairo_image_surface_get_data(s), w, h, cairo_image_surface_get_stride(s) / 4, r);
    cairo_surface_mark_dirty(s);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(s);
    cairo_set_source_rgba(cr, 0, 0, 0, gtk_range_get_value(GTK_RANGE(M.dim)) / 100.0);
    cairo_paint(cr);
    /* roughly where the game's buttons go, so you can judge contrast */
    double bw = w * 0.42, bh = h * 0.075, x = (w - bw) / 2;
    mock_button(cr, x, h * 0.42, bw, bh, "Singleplayer");
    mock_button(cr, x, h * 0.42 + bh * 1.35, bw, bh, "Multiplayer");
    mock_button(cr, x, h * 0.42 + bh * 2.7, bw, bh, "Options");
}

static void on_blur(GtkRange *r, gpointer d)
{
    (void)d;
    char v[16];
    g_snprintf(v, sizeof v, "%d", (int)gtk_range_get_value(r));
    mc_tuxmc_set(M.a, "menuBlur", v);
    gtk_widget_queue_draw(M.area);
}

static void on_dim(GtkRange *r, gpointer d)
{
    (void)d;
    char v[16];
    g_ascii_formatd(v, sizeof v, "%.2f", gtk_range_get_value(r) / 100.0);
    mc_tuxmc_set(M.a, "menuDim", v);
    gtk_widget_queue_draw(M.area);
}

static void on_drift(GObject *s, GParamSpec *ps, gpointer d)
{
    (void)ps;
    (void)d;
    mc_tuxmc_set(M.a, "menuParallax", gtk_switch_get_active(GTK_SWITCH(s)) ? "true" : "false");
    gtk_widget_queue_draw(M.area);
}

static void on_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (!f)
        return;
    char *p = g_file_get_path(f), *out = bg_file(M.a), *dir = g_path_get_dirname(out);
    GError *e = NULL;
    GdkTexture *t = gdk_texture_new_from_filename(p, &e);
    g_mkdir_with_parents(dir, 0755);
    if (t && gdk_texture_save_to_png(t, out)) {
        mc_tuxmc_set(M.a, "menuBg", out);
        load_pic();
        gtk_label_set_text(GTK_LABEL(M.msg), mc_game_running(M.a) ? "Set. The game picks it up on its next menu."
                                                                    : "Set. You'll see it when the game starts.");
    } else {
        gtk_label_set_text(GTK_LABEL(M.msg), "That picture couldn't be read.");
    }
    gtk_widget_queue_draw(M.area);
    if (t)
        g_object_unref(t);
    g_clear_error(&e);
    g_free(p);
    g_free(out);
    g_free(dir);
    g_object_unref(f);
}

static void on_choose(GtkButton *b, gpointer data)
{
    (void)b;
    app_t *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Title screen picture");
    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Pictures");
    gtk_file_filter_add_mime_type(ff, "image/png");
    gtk_file_filter_add_mime_type(ff, "image/jpeg");
    gtk_file_filter_add_mime_type(ff, "image/webp");
    GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(fl, ff);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(fl));
    const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    if (pics) {
        GFile *pf = g_file_new_for_path(pics);
        gtk_file_dialog_set_initial_folder(d, pf);
        g_object_unref(pf);
    }
    gtk_file_dialog_open(d, a->win, NULL, on_chosen, a);
    g_object_unref(fl);
    g_object_unref(ff);
    g_object_unref(d);
}

static void on_remove(GtkButton *b, gpointer data)
{
    (void)b;
    (void)data;
    mc_tuxmc_set(M.a, "menuBg", "");
    load_pic();
    gtk_label_set_text(GTK_LABEL(M.msg), "Back to the usual panorama.");
    gtk_widget_queue_draw(M.area);
}

static GtkWidget *slider(GtkWidget *card, const char *label, double max, double val, GCallback cb, GtkWidget **out)
{
    GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *l = mc_label(label, NULL);
    gtk_label_set_width_chars(GTK_LABEL(l), 8);
    gtk_box_append(GTK_BOX(r), l);
    *out = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, max, 1);
    gtk_scale_set_draw_value(GTK_SCALE(*out), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(*out), GTK_POS_RIGHT);
    gtk_widget_set_hexpand(*out, TRUE);
    gtk_range_set_value(GTK_RANGE(*out), val);
    g_signal_connect(*out, "value-changed", cb, NULL);
    gtk_box_append(GTK_BOX(r), *out);
    gtk_box_append(GTK_BOX(card), r);
    return r;
}

GtkWidget *mc_menubg_card(app_t *a)
{
    M.a = a;
    GtkWidget *card = mc_card("Title screen", "A picture of your own behind the game's menus, instead of the "
                                              "spinning world.");
    M.area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(M.area), 480);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(M.area), 270);
    gtk_widget_set_halign(M.area, GTK_ALIGN_START);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(M.area), draw, NULL, NULL);
    gtk_widget_add_css_class(M.area, "mcl-canvas");
    gtk_box_append(GTK_BOX(card), M.area);

    GtkWidget *br = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *ch = gtk_button_new_with_label("Choose a picture…");
    g_signal_connect(ch, "clicked", G_CALLBACK(on_choose), a);
    M.remove = gtk_button_new_with_label("Use the panorama");
    g_signal_connect(M.remove, "clicked", G_CALLBACK(on_remove), NULL);
    gtk_box_append(GTK_BOX(br), ch);
    gtk_box_append(GTK_BOX(br), M.remove);
    gtk_box_append(GTK_BOX(card), br);

    slider(card, "Blur", 30, prop("menuBlur", 0), G_CALLBACK(on_blur), &M.blur);
    slider(card, "Darken", 80, prop("menuDim", 0.25) * 100, G_CALLBACK(on_dim), &M.dim);
    GtkWidget *dr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *dl = mc_label("Drifts a little with the mouse", NULL);
    gtk_widget_set_hexpand(dl, TRUE);
    gtk_box_append(GTK_BOX(dr), dl);
    M.drift = gtk_switch_new();
    char *pv = mc_tuxmc_get(a, "menuParallax");
    gtk_switch_set_active(GTK_SWITCH(M.drift), !pv || strcmp(pv, "false"));
    g_free(pv);
    g_signal_connect(M.drift, "notify::active", G_CALLBACK(on_drift), NULL);
    gtk_box_append(GTK_BOX(dr), M.drift);
    gtk_box_append(GTK_BOX(card), dr);
    M.msg = mc_label("", "dim");
    gtk_box_append(GTK_BOX(card), M.msg);
    load_pic();
    return card;
}
