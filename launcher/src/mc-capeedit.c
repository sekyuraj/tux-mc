/* mc-capeedit.c — the picture cape editor. Every picture cape is a preset
 * in <data>/tux-mc/capes/: <id>.png (the cape, 320 x 512) and <id>.ini (its
 * name, the original picture and every setting here), so Edit opens it again
 * just as it was.
 *
 * The editor itself. After you pick a picture it opens
 * on the cape's shape (10 x 16, the outside of the cape): drag to move it,
 * zoom, stretch or squeeze it on each axis, rotate, flip, pick what fills the
 * empty space. "Use as cape" saves a 320 x 512 png in
 * <data>/tux-mc/capes/ and hands its path back. */
#include "mc.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define OW 320
#define OH 512

typedef enum { FIT_COVER, FIT_INSIDE, FIT_STRETCH } fit_t;

typedef struct {
    app_t *a;
    void (*done)(app_t *, const char *);
    GtkWindow *win;
    GtkWidget *area, *zoom, *wide, *tall, *bg_btn, *fit_btn[3];
    cairo_surface_t *src;                /* the picture, rotated */
    int rot;                             /* quarter turns */
    bool flip;
    fit_t fit;
    double ox, oy;                       /* offset, in output pixels */
    double drag_ox, drag_oy;
    GdkRGBA bg;
    char *id;                            /* the preset being edited, or NULL for a new one */
    char *source;                        /* the original picture */
    GtkWidget *name;
} ed_t;

char *mc_capes_dir(void) { return g_build_filename(g_get_user_data_dir(), "tux-mc", "capes", NULL); }

static cairo_surface_t *from_pixbuf(GdkPixbuf *pb)
{
    GdkPixbuf *p = gdk_pixbuf_get_has_alpha(pb) ? g_object_ref(pb) : gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
    int w = gdk_pixbuf_get_width(p), h = gdk_pixbuf_get_height(p), rs = gdk_pixbuf_get_rowstride(p);
    const guchar *px = gdk_pixbuf_read_pixels(p);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    guint32 *out = (guint32 *)cairo_image_surface_get_data(s);
    int os = cairo_image_surface_get_stride(s) / 4;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const guchar *c = px + y * rs + x * 4;
            guint32 al = c[3];
            out[y * os + x] = al << 24 | (c[0] * al / 255) << 16 | (c[1] * al / 255) << 8 | (c[2] * al / 255);
        }
    cairo_surface_mark_dirty(s);
    g_object_unref(p);
    return s;
}

static cairo_surface_t *rotate90(cairo_surface_t *s)
{
    int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    cairo_surface_t *r = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, h, w);
    cairo_t *cr = cairo_create(r);
    cairo_translate(cr, h, 0);
    cairo_rotate(cr, M_PI / 2);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return r;
}

/* draw the cape face (OW x OH) into cr */
static void compose(ed_t *e, cairo_t *cr)
{
    gdk_cairo_set_source_rgba(cr, &e->bg);
    cairo_rectangle(cr, 0, 0, OW, OH);
    cairo_fill(cr);
    int iw = cairo_image_surface_get_width(e->src), ih = cairo_image_surface_get_height(e->src);
    double sx = (double)OW / iw, sy = (double)OH / ih;
    if (e->fit == FIT_COVER)
        sx = sy = fmax(sx, sy);
    else if (e->fit == FIT_INSIDE)
        sx = sy = fmin(sx, sy);
    double z = gtk_range_get_value(GTK_RANGE(e->zoom));
    sx *= z * gtk_range_get_value(GTK_RANGE(e->wide));
    sy *= z * gtk_range_get_value(GTK_RANGE(e->tall));
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, OW, OH);
    cairo_clip(cr);
    cairo_translate(cr, OW / 2.0 + e->ox, OH / 2.0 + e->oy);
    cairo_scale(cr, e->flip ? -sx : sx, sy);
    cairo_translate(cr, -iw / 2.0, -ih / 2.0);
    cairo_set_source_surface(cr, e->src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void draw(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    ed_t *e = data;
    double k = fmin((w - 20.0) / OW, (h - 20.0) / OH);
    double x0 = (w - OW * k) / 2, y0 = (h - OH * k) / 2;
    cairo_save(cr);
    cairo_translate(cr, x0, y0);
    cairo_scale(cr, k, k);
    compose(e, cr);
    cairo_restore(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, x0 - 0.5, y0 - 0.5, OW * k + 1, OH * k + 1);
    cairo_stroke(cr);
}

static void redraw(ed_t *e) { gtk_widget_queue_draw(e->area); }

static double view_scale(ed_t *e)
{
    int w = gtk_widget_get_width(e->area), h = gtk_widget_get_height(e->area);
    return fmin((w - 20.0) / OW, (h - 20.0) / OH);
}

static void on_drag_begin(GtkGestureDrag *g, double x, double y, gpointer data)
{
    (void)g;
    (void)x;
    (void)y;
    ed_t *e = data;
    e->drag_ox = e->ox;
    e->drag_oy = e->oy;
}

static void on_drag_update(GtkGestureDrag *g, double dx, double dy, gpointer data)
{
    (void)g;
    ed_t *e = data;
    double k = view_scale(e);
    e->ox = e->drag_ox + dx / k;
    e->oy = e->drag_oy + dy / k;
    redraw(e);
}

static gboolean on_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer data)
{
    (void)c;
    (void)dx;
    ed_t *e = data;
    double z = gtk_range_get_value(GTK_RANGE(e->zoom));
    gtk_range_set_value(GTK_RANGE(e->zoom), z * (dy < 0 ? 1.08 : 1 / 1.08));
    return TRUE;
}

static void on_value(GtkRange *r, gpointer data)
{
    (void)r;
    redraw(data);
}

static void set_fit(ed_t *e, fit_t f)
{
    e->fit = f;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->fit_btn[f]), TRUE);
    redraw(e);
}

static void on_fit(GtkToggleButton *b, gpointer data)
{
    if (gtk_toggle_button_get_active(b)) {
        ((ed_t *)data)->fit = (fit_t)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "fit"));
        redraw(data);
    }
}

static void on_rotate(GtkButton *b, gpointer data)
{
    (void)b;
    ed_t *e = data;
    cairo_surface_t *r = rotate90(e->src);
    cairo_surface_destroy(e->src);
    e->src = r;
    e->rot = (e->rot + 1) % 4;
    double t = e->ox;
    e->ox = -e->oy;
    e->oy = t;
    redraw(e);
}

static void on_flip(GtkButton *b, gpointer data)
{
    (void)b;
    ed_t *e = data;
    e->flip = !e->flip;
    redraw(e);
}

static void on_bg(GtkWidget *o, gpointer data)
{
    ed_t *e = data;
    mc_colour_get(o, &e->bg);
    e->bg.alpha = 1;
    redraw(e);
}

static void reset(ed_t *e)
{
    e->ox = e->oy = 0;
    e->flip = false;
    gtk_range_set_value(GTK_RANGE(e->zoom), 1);
    gtk_range_set_value(GTK_RANGE(e->wide), 1);
    gtk_range_set_value(GTK_RANGE(e->tall), 1);
    set_fit(e, FIT_COVER);
}

static void on_reset(GtkButton *b, gpointer data)
{
    (void)b;
    reset(data);
}

static void on_cancel(GtkButton *b, gpointer data)
{
    (void)b;
    gtk_window_destroy(((ed_t *)data)->win);
}

static void on_use(GtkButton *b, gpointer data)
{
    (void)b;
    ed_t *e = data;
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, OW, OH);
    cairo_t *cr = cairo_create(out);
    compose(e, cr);
    cairo_destroy(cr);
    char *dir = mc_capes_dir();
    g_mkdir_with_parents(dir, 0755);
    char *id = e->id ? g_strdup(e->id) : g_strdup_printf("picture-%" G_GINT64_FORMAT, g_get_real_time() / 1000000);
    char *png = g_strconcat(id, ".png", NULL), *path = g_build_filename(dir, png, NULL);
    char *ini = g_strconcat(id, ".ini", NULL), *ipath = g_build_filename(dir, ini, NULL);
    const char *nm = gtk_editable_get_text(GTK_EDITABLE(e->name));
    GKeyFile *k = g_key_file_new();
    g_key_file_set_string(k, "cape", "name", *nm ? nm : "Picture");
    if (e->source)
        g_key_file_set_string(k, "cape", "source", e->source);
    g_key_file_set_integer(k, "cape", "fit", e->fit);
    g_key_file_set_double(k, "cape", "zoom", gtk_range_get_value(GTK_RANGE(e->zoom)));
    g_key_file_set_double(k, "cape", "wide", gtk_range_get_value(GTK_RANGE(e->wide)));
    g_key_file_set_double(k, "cape", "tall", gtk_range_get_value(GTK_RANGE(e->tall)));
    g_key_file_set_double(k, "cape", "ox", e->ox);
    g_key_file_set_double(k, "cape", "oy", e->oy);
    g_key_file_set_integer(k, "cape", "rot", e->rot);
    g_key_file_set_boolean(k, "cape", "flip", e->flip);
    char *bg = gdk_rgba_to_string(&e->bg);
    g_key_file_set_string(k, "cape", "bg", bg);
    g_free(bg);
    if (cairo_surface_write_to_png(out, path) == CAIRO_STATUS_SUCCESS && g_key_file_save_to_file(k, ipath, NULL)) {
        e->done(e->a, path);
        gtk_window_destroy(e->win);
    } else {
        mc_status(e->a, "Couldn't save the cape picture to %s", dir);
    }
    g_key_file_unref(k);
    cairo_surface_destroy(out);
    g_free(path);
    g_free(png);
    g_free(ipath);
    g_free(ini);
    g_free(id);
    g_free(dir);
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    ed_t *e = data;
    cairo_surface_destroy(e->src);
    g_free(e->id);
    g_free(e->source);
    g_free(e);
}

static GtkWidget *slider(GtkWidget *box, const char *title, double lo, double hi, ed_t *e)
{
    gtk_box_append(GTK_BOX(box), mc_label(title, "dim"));
    GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, 0.01);
    gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
    gtk_scale_add_mark(GTK_SCALE(s), 1, GTK_POS_BOTTOM, NULL);
    gtk_range_set_value(GTK_RANGE(s), 1);
    g_signal_connect(s, "value-changed", G_CALLBACK(on_value), e);
    gtk_box_append(GTK_BOX(box), s);
    return s;
}

void mc_cape_editor(app_t *a, const char *path, const char *edit_id, void (*done)(app_t *a, const char *saved))
{
    /* editing a preset: its original picture (or, if that's gone, the cape itself) */
    GKeyFile *k = NULL;
    char *src_path = g_strdup(path), *keep_name = NULL;
    if (edit_id) {
        char *dir = mc_capes_dir(), *ini = g_strconcat(edit_id, ".ini", NULL), *ip = g_build_filename(dir, ini, NULL);
        k = g_key_file_new();
        if (!g_key_file_load_from_file(k, ip, G_KEY_FILE_NONE, NULL))
            g_clear_pointer(&k, g_key_file_unref);
        char *s = k ? g_key_file_get_string(k, "cape", "source", NULL) : NULL;
        g_free(src_path);
        if (s && g_file_test(s, G_FILE_TEST_IS_REGULAR)) {
            src_path = s;
        } else {
            g_free(s);
            char *png = g_strconcat(edit_id, ".png", NULL);
            src_path = g_build_filename(dir, png, NULL);
            g_free(png);
            /* the settings belong to the lost original; the name stays */
            if (k)
                keep_name = g_key_file_get_string(k, "cape", "name", NULL);
            g_clear_pointer(&k, g_key_file_unref);
        }
        g_free(ip);
        g_free(ini);
        g_free(dir);
    }
    GError *err = NULL;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(src_path, &err);
    if (!pb) {
        mc_status(a, "Couldn't read that picture: %s", err->message);
        g_error_free(err);
        g_free(src_path);
        g_free(keep_name);
        if (k)
            g_key_file_unref(k);
        return;
    }
    ed_t *e = g_new0(ed_t, 1);
    e->a = a;
    e->done = done;
    e->id = g_strdup(edit_id);
    e->source = src_path;
    e->src = from_pixbuf(pb);
    g_object_unref(pb);
    gdk_rgba_parse(&e->bg, "#000000");

    e->win = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(e->win, "Picture cape");
    gtk_window_set_transient_for(e->win, a->win);
    gtk_window_set_modal(e->win, TRUE);
    gtk_window_set_default_size(e->win, 820, 620);
    gtk_widget_add_css_class(GTK_WIDGET(e->win), "mcl-window");
    g_signal_connect(e->win, "destroy", G_CALLBACK(on_destroy), e);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_margin_start(row, 18);
    gtk_widget_set_margin_end(row, 18);
    gtk_widget_set_margin_top(row, 18);
    gtk_widget_set_margin_bottom(row, 18);
    gtk_window_set_child(e->win, row);

    e->area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(e->area, TRUE);
    gtk_widget_set_vexpand(e->area, TRUE);
    gtk_widget_set_size_request(e->area, 340, 520);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(e->area), draw, e, NULL);
    GtkGesture *g = gtk_gesture_drag_new();
    g_signal_connect(g, "drag-begin", G_CALLBACK(on_drag_begin), e);
    g_signal_connect(g, "drag-update", G_CALLBACK(on_drag_update), e);
    gtk_widget_add_controller(e->area, GTK_EVENT_CONTROLLER(g));
    GtkEventController *sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(sc, "scroll", G_CALLBACK(on_scroll), e);
    gtk_widget_add_controller(e->area, sc);
    gtk_widget_set_cursor_from_name(e->area, "grab");
    gtk_box_append(GTK_BOX(row), e->area);

    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(side, 280, -1);
    gtk_box_append(GTK_BOX(side), mc_label("Picture cape", "mcl-h2"));
    GtkWidget *hint = mc_label("This is the outside of the cape. Drag the picture to move it, scroll to zoom.",
                               "dim");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_box_append(GTK_BOX(side), hint);
    gtk_box_append(GTK_BOX(side), mc_label("Name", "dim"));
    e->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->name), "e.g. Sunset");
    gtk_entry_set_max_length(GTK_ENTRY(e->name), 24);
    if (k) {
        char *n = g_key_file_get_string(k, "cape", "name", NULL);
        gtk_editable_set_text(GTK_EDITABLE(e->name), n ? n : "");
        g_free(n);
    } else if (keep_name) {
        gtk_editable_set_text(GTK_EDITABLE(e->name), keep_name);
    } else if (!edit_id) {                 /* a new one: the picture's file name to start with */
        char *b = g_path_get_basename(path), *dot = strrchr(b, '.');
        if (dot)
            *dot = 0;
        gtk_editable_set_text(GTK_EDITABLE(e->name), b);
        g_free(b);
    }
    gtk_box_append(GTK_BOX(side), e->name);
    g_free(keep_name);

    GtkWidget *fr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(fr), TRUE);
    gtk_widget_add_css_class(fr, "mcl-seg");
    const char *fits[3] = {"Fill", "Fit", "Stretch"};
    for (int i = 0; i < 3; i++) {
        e->fit_btn[i] = gtk_toggle_button_new_with_label(fits[i]);
        if (i)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(e->fit_btn[i]), GTK_TOGGLE_BUTTON(e->fit_btn[0]));
        g_object_set_data(G_OBJECT(e->fit_btn[i]), "fit", GINT_TO_POINTER(i));
        g_signal_connect(e->fit_btn[i], "toggled", G_CALLBACK(on_fit), e);
        gtk_box_append(GTK_BOX(fr), e->fit_btn[i]);
    }
    gtk_widget_set_margin_top(fr, 8);
    gtk_box_append(GTK_BOX(side), fr);

    e->zoom = slider(side, "Zoom", 0.2, 5, e);
    e->wide = slider(side, "Width (squeeze ↔ stretch)", 0.25, 3, e);
    e->tall = slider(side, "Height (squeeze ↕ stretch)", 0.25, 3, e);

    GtkWidget *tr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(tr), TRUE);
    GtkWidget *rot = gtk_button_new_with_label("Rotate");
    g_signal_connect(rot, "clicked", G_CALLBACK(on_rotate), e);
    GtkWidget *flip = gtk_button_new_with_label("Flip");
    g_signal_connect(flip, "clicked", G_CALLBACK(on_flip), e);
    GtkWidget *rs = gtk_button_new_with_label("Reset");
    g_signal_connect(rs, "clicked", G_CALLBACK(on_reset), e);
    gtk_box_append(GTK_BOX(tr), rot);
    gtk_box_append(GTK_BOX(tr), flip);
    gtk_box_append(GTK_BOX(tr), rs);
    gtk_widget_set_margin_top(tr, 6);
    gtk_box_append(GTK_BOX(side), tr);

    GtkWidget *bgr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(bgr, 6);
    GtkWidget *bl = mc_label("Empty space", "dim");
    gtk_widget_set_hexpand(bl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(bl), 0);
    e->bg_btn = mc_colour_button_new(false);
    mc_colour_set(e->bg_btn, &e->bg);
    mc_colour_on_change(e->bg_btn, on_bg, e);
    gtk_box_append(GTK_BOX(bgr), bl);
    gtk_box_append(GTK_BOX(bgr), e->bg_btn);
    gtk_box_append(GTK_BOX(side), bgr);

    GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(sp, TRUE);
    gtk_box_append(GTK_BOX(side), sp);
    GtkWidget *br = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(br), TRUE);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), e);
    GtkWidget *use = gtk_button_new_with_label("Use as cape");
    gtk_widget_add_css_class(use, "suggested-action");
    g_signal_connect(use, "clicked", G_CALLBACK(on_use), e);
    gtk_box_append(GTK_BOX(br), cancel);
    gtk_box_append(GTK_BOX(br), use);
    gtk_box_append(GTK_BOX(side), br);
    gtk_box_append(GTK_BOX(row), side);

    reset(e);
    if (k) {                               /* back to how it was saved */
        for (int r = g_key_file_get_integer(k, "cape", "rot", NULL) % 4; r > 0; r--)
            on_rotate(NULL, e);
        e->flip = g_key_file_get_boolean(k, "cape", "flip", NULL);
        e->ox = g_key_file_get_double(k, "cape", "ox", NULL);
        e->oy = g_key_file_get_double(k, "cape", "oy", NULL);
        gtk_range_set_value(GTK_RANGE(e->zoom), g_key_file_get_double(k, "cape", "zoom", NULL));
        gtk_range_set_value(GTK_RANGE(e->wide), g_key_file_get_double(k, "cape", "wide", NULL));
        gtk_range_set_value(GTK_RANGE(e->tall), g_key_file_get_double(k, "cape", "tall", NULL));
        char *bg = g_key_file_get_string(k, "cape", "bg", NULL);
        if (bg && gdk_rgba_parse(&e->bg, bg))
            mc_colour_set(e->bg_btn, &e->bg);
        g_free(bg);
        set_fit(e, (fit_t)CLAMP(g_key_file_get_integer(k, "cape", "fit", NULL), 0, 2));
        g_key_file_unref(k);
    }
    gtk_window_present(e->win);
}
