/* mc-colour.c — the colour picker used everywhere you choose a colour:
 * a hue ring around a shade square (saturation left to right, brightness top
 * to bottom), a hex box, and an opacity slider where see-through makes sense.
 *
 * mc_colour_wheel_new() is the picker itself, for pages with room for it;
 * mc_colour_button_new() is a swatch that opens the same picker in a popover.
 * Both take mc_colour_get / mc_colour_set / mc_colour_on_change. Setting a
 * colour calls the change callback, like GTK's own colour button does.
 */
#include "mc.h"

#include <math.h>
#include <string.h>

typedef struct {
    double h, s, v, a;                   /* hue 0..360, the rest 0..1 */
    bool alpha;
    void (*cb)(GtkWidget *w, gpointer data);
    gpointer data;
    GtkWidget *self;                     /* what callers hold: the wheel box or the button */
    GtkWidget *area, *hex, *alpha_scale, *swatch;
    cairo_surface_t *square;             /* the shade square for square_h */
    double square_h;
    int square_px;
    int drag;                            /* 0 none, 1 ring, 2 square */
    bool quiet;
} wheel_t;

static wheel_t *W(GtkWidget *w) { return g_object_get_data(G_OBJECT(w), "mc-colour"); }

static void hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b)
{
    h = fmod(fmod(h, 360) + 360, 360) / 60;
    double c = v * s, x = c * (1 - fabs(fmod(h, 2) - 1)), m = v - c;
    double rr = 0, gg = 0, bb = 0;
    if (h < 1) { rr = c; gg = x; }
    else if (h < 2) { rr = x; gg = c; }
    else if (h < 3) { gg = c; bb = x; }
    else if (h < 4) { gg = x; bb = c; }
    else if (h < 5) { rr = x; bb = c; }
    else { rr = c; bb = x; }
    *r = rr + m;
    *g = gg + m;
    *b = bb + m;
}

static void rgb_to_hsv(double r, double g, double b, double *h, double *s, double *v)
{
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0)
        return;                          /* grey: keep the hue it had */
    if (mx == r)
        *h = fmod((g - b) / d + 6, 6) * 60;
    else if (mx == g)
        *h = ((b - r) / d + 2) * 60;
    else
        *h = ((r - g) / d + 4) * 60;
}

static void current(const wheel_t *k, GdkRGBA *c)
{
    double r, g, b;
    hsv_to_rgb(k->h, k->s, k->v, &r, &g, &b);
    *c = (GdkRGBA){r, g, b, k->alpha ? k->a : 1};
}

static void show_hex(wheel_t *k)
{
    if (!k->hex)
        return;
    GdkRGBA c;
    current(k, &c);
    char t[16];
    if (k->alpha && k->a < 0.999)
        g_snprintf(t, sizeof t, "#%02x%02x%02x%02x", (int)lround(c.red * 255), (int)lround(c.green * 255),
                   (int)lround(c.blue * 255), (int)lround(c.alpha * 255));
    else
        g_snprintf(t, sizeof t, "#%02x%02x%02x", (int)lround(c.red * 255), (int)lround(c.green * 255),
                   (int)lround(c.blue * 255));
    k->quiet = true;
    gtk_editable_set_text(GTK_EDITABLE(k->hex), t);
    k->quiet = false;
}

static void changed(wheel_t *k, bool from_hex)
{
    if (!from_hex)
        show_hex(k);
    if (k->alpha_scale) {
        k->quiet = true;
        gtk_range_set_value(GTK_RANGE(k->alpha_scale), k->a * 100);
        k->quiet = false;
    }
    gtk_widget_queue_draw(k->area);
    if (k->swatch)
        gtk_widget_queue_draw(k->swatch);
    if (k->cb)
        k->cb(k->self, k->data);
}

/* --------------------------------------------------------------- drawing -- */

typedef struct { double cx, cy, ro, ri, half; } geo_t;

static geo_t geometry(int w, int h)
{
    geo_t g;
    g.cx = w / 2.0;
    g.cy = h / 2.0;
    g.ro = fmin(w, h) / 2.0 - 2;
    g.ri = g.ro * 0.8;
    g.half = g.ri * 0.68;               /* the square sits inside the ring */
    return g;
}

static void build_square(wheel_t *k, int px)
{
    if (k->square && k->square_h == k->h && k->square_px == px)
        return;
    if (k->square)
        cairo_surface_destroy(k->square);
    k->square = cairo_image_surface_create(CAIRO_FORMAT_RGB24, px, px);
    guint32 *d = (guint32 *)cairo_image_surface_get_data(k->square);
    int st = cairo_image_surface_get_stride(k->square) / 4;
    for (int y = 0; y < px; y++)
        for (int x = 0; x < px; x++) {
            double r, g, b;
            hsv_to_rgb(k->h, x / (px - 1.0), 1 - y / (px - 1.0), &r, &g, &b);
            d[y * st + x] = (guint32)lround(r * 255) << 16 | (guint32)lround(g * 255) << 8 | (guint32)lround(b * 255);
        }
    cairo_surface_mark_dirty(k->square);
    k->square_h = k->h;
    k->square_px = px;
}

static void draw_wheel(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    wheel_t *k = data;
    geo_t g = geometry(w, h);
    /* the hue ring, one thin wedge per degree */
    for (int i = 0; i < 360; i++) {
        double r, gg, b;
        hsv_to_rgb(i, 1, 1, &r, &gg, &b);
        cairo_set_source_rgb(cr, r, gg, b);
        double a0 = (i - 90.6) * G_PI / 180, a1 = (i - 89.4) * G_PI / 180;
        cairo_arc(cr, g.cx, g.cy, g.ro, a0, a1);
        cairo_arc_negative(cr, g.cx, g.cy, g.ri, a1, a0);
        cairo_close_path(cr);
        cairo_fill(cr);
    }
    /* the shade square */
    int sc = gtk_widget_get_scale_factor(k->area);
    int px = MAX(8, (int)(g.half * 2 * sc));
    build_square(k, px);
    cairo_save(cr);
    cairo_translate(cr, g.cx - g.half, g.cy - g.half);
    cairo_scale(cr, g.half * 2 / px, g.half * 2 / px);
    cairo_set_source_surface(cr, k->square, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
    /* markers */
    double ang = (k->h - 90) * G_PI / 180, rm = (g.ro + g.ri) / 2;
    double mx = g.cx + cos(ang) * rm, my = g.cy + sin(ang) * rm;
    cairo_set_line_width(cr, 2.5);
    cairo_arc(cr, mx, my, (g.ro - g.ri) / 2 + 1, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_stroke_preserve(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
    double sx = g.cx - g.half + k->s * g.half * 2, sy = g.cy - g.half + (1 - k->v) * g.half * 2;
    cairo_arc(cr, sx, sy, 6, 0, 2 * G_PI);
    GdkRGBA c;
    current(k, &c);
    cairo_set_source_rgb(cr, c.red, c.green, c.blue);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 2.5);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_stroke_preserve(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
}

static void pointer(wheel_t *k, double x, double y, bool begin)
{
    geo_t g = geometry(gtk_widget_get_width(k->area), gtk_widget_get_height(k->area));
    double dx = x - g.cx, dy = y - g.cy, d = hypot(dx, dy);
    if (begin)
        k->drag = d >= g.ri - 4 && d <= g.ro + 6 ? 1 : fabs(dx) <= g.half + 6 && fabs(dy) <= g.half + 6 ? 2 : 0;
    if (k->drag == 1) {
        k->h = fmod(atan2(dy, dx) * 180 / G_PI + 90 + 360, 360);
        if (k->s < 0.02 && begin)        /* a grey picks up colour when you choose a hue */
            k->s = 0.75, k->v = fmax(k->v, 0.6);
    } else if (k->drag == 2) {
        k->s = CLAMP((dx + g.half) / (2 * g.half), 0, 1);
        k->v = CLAMP(1 - (dy + g.half) / (2 * g.half), 0, 1);
    } else
        return;
    changed(k, false);
}

static void on_begin(GtkGestureDrag *gd, double x, double y, gpointer data)
{
    (void)gd;
    pointer(data, x, y, true);
}

static void on_update(GtkGestureDrag *gd, double ox, double oy, gpointer data)
{
    double sx, sy;
    gtk_gesture_drag_get_start_point(gd, &sx, &sy);
    pointer(data, sx + ox, sy + oy, false);
}

static void on_hex(GtkEditable *e, gpointer data)
{
    wheel_t *k = data;
    if (k->quiet)
        return;
    const char *t = gtk_editable_get_text(e);
    if (*t != '#')
        return;
    size_t n = strlen(t);
    if (n != 4 && n != 7 && n != 9)
        return;
    for (size_t i = 1; i < n; i++)
        if (!g_ascii_isxdigit(t[i]))
            return;
    GdkRGBA c;
    if (n == 9) {                        /* #rrggbbaa: GTK reads #rgba differently, do it by hand */
        unsigned v = (unsigned)strtoul(t + 1, NULL, 16);
        c = (GdkRGBA){(v >> 24) / 255.0, (v >> 16 & 255) / 255.0, (v >> 8 & 255) / 255.0, (v & 255) / 255.0};
    } else if (!gdk_rgba_parse(&c, t))
        return;
    rgb_to_hsv(c.red, c.green, c.blue, &k->h, &k->s, &k->v);
    if (k->alpha)
        k->a = c.alpha;
    changed(k, true);
}

static void on_alpha(GtkRange *r, gpointer data)
{
    wheel_t *k = data;
    if (k->quiet)
        return;
    k->a = gtk_range_get_value(r) / 100;
    changed(k, false);
}

static void wheel_free(gpointer p)
{
    wheel_t *k = p;
    if (k->square)
        cairo_surface_destroy(k->square);
    g_free(k);
}

static GtkWidget *picker(wheel_t *k, int size)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    k->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(k->area, size, size);
    gtk_widget_set_halign(k->area, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(k->area), draw_wheel, k, NULL);
    GtkGesture *g = gtk_gesture_drag_new();
    g_signal_connect(g, "drag-begin", G_CALLBACK(on_begin), k);
    g_signal_connect(g, "drag-update", G_CALLBACK(on_update), k);
    gtk_widget_add_controller(k->area, GTK_EVENT_CONTROLLER(g));
    gtk_box_append(GTK_BOX(box), k->area);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(row), mc_label("Hex", "dim"));
    k->hex = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(k->hex), 9);
    gtk_editable_set_width_chars(GTK_EDITABLE(k->hex), 9);
    gtk_widget_set_hexpand(k->hex, TRUE);
    gtk_widget_set_tooltip_text(k->hex, "type or paste a colour like #3f86d9");
    g_signal_connect(k->hex, "changed", G_CALLBACK(on_hex), k);
    gtk_box_append(GTK_BOX(row), k->hex);
    gtk_box_append(GTK_BOX(box), row);
    if (k->alpha) {
        GtkWidget *ar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(ar), mc_label("Opacity", "dim"));
        k->alpha_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
        gtk_widget_set_hexpand(k->alpha_scale, TRUE);
        gtk_range_set_value(GTK_RANGE(k->alpha_scale), 100);
        g_signal_connect(k->alpha_scale, "value-changed", G_CALLBACK(on_alpha), k);
        gtk_box_append(GTK_BOX(ar), k->alpha_scale);
        gtk_box_append(GTK_BOX(box), ar);
    }
    show_hex(k);
    return box;
}

GtkWidget *mc_colour_wheel_new(bool alpha, int size)
{
    wheel_t *k = g_new0(wheel_t, 1);
    k->alpha = alpha;
    k->s = 0.7;
    k->v = 0.85;
    k->a = 1;
    GtkWidget *box = picker(k, size);
    k->self = box;
    g_object_set_data_full(G_OBJECT(box), "mc-colour", k, wheel_free);
    return box;
}

static void draw_swatch(GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer data)
{
    (void)da;
    wheel_t *k = data;
    for (int y = 0; y < h; y += 6)       /* checks show through see-through colours */
        for (int x = 0; x < w; x += 6) {
            double c = ((x + y) / 6) & 1 ? 0.75 : 0.95;
            cairo_set_source_rgb(cr, c, c, c);
            cairo_rectangle(cr, x, y, 6, 6);
            cairo_fill(cr);
        }
    GdkRGBA c;
    current(k, &c);
    gdk_cairo_set_source_rgba(cr, &c);
    cairo_paint(cr);
}

GtkWidget *mc_colour_button_new(bool alpha)
{
    wheel_t *k = g_new0(wheel_t, 1);
    k->alpha = alpha;
    k->a = 1;
    k->v = 1;
    GtkWidget *b = gtk_menu_button_new();
    gtk_widget_add_css_class(b, "mcl-colour-btn");
    k->swatch = gtk_drawing_area_new();
    gtk_widget_set_size_request(k->swatch, 44, 18);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(k->swatch), draw_swatch, k, NULL);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(b), k->swatch);
    GtkWidget *pop = gtk_popover_new();
    GtkWidget *p = picker(k, 220);
    gtk_widget_set_margin_top(p, 6);
    gtk_widget_set_margin_bottom(p, 6);
    gtk_widget_set_margin_start(p, 6);
    gtk_widget_set_margin_end(p, 6);
    gtk_popover_set_child(GTK_POPOVER(pop), p);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(b), pop);
    k->self = b;
    g_object_set_data_full(G_OBJECT(b), "mc-colour", k, wheel_free);
    return b;
}

void mc_colour_get(GtkWidget *w, GdkRGBA *c)
{
    wheel_t *k = W(w);
    if (k)
        current(k, c);
}

void mc_colour_set(GtkWidget *w, const GdkRGBA *c)
{
    wheel_t *k = W(w);
    if (!k)
        return;
    rgb_to_hsv(c->red, c->green, c->blue, &k->h, &k->s, &k->v);
    k->a = k->alpha ? c->alpha : 1;
    changed(k, false);
}

void mc_colour_on_change(GtkWidget *w, void (*cb)(GtkWidget *w, gpointer data), gpointer data)
{
    wheel_t *k = W(w);
    if (k) {
        k->cb = cb;
        k->data = data;
    }
}
