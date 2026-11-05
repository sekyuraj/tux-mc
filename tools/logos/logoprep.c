/* logoprep — turns a rendered logo into a cape image.
 *   logoprep in.png out.png SIZE [ops...]
 * ops, applied in order:
 *   color RRGGBB         paint every pixel this colour, keeping its alpha
 *   erase X Y W H        make a rectangle transparent (a ® mark, say)
 *   fillin RRGGBB        fill the inside of a line drawing: transparent pixels
 *                        that can't be reached from the border get the colour
 * then the image is fitted into SIZE x SIZE, centred, on transparency.
 * Build: cc -O2 -o logoprep logoprep.c $(pkg-config --cflags --libs gdk-pixbuf-2.0) */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdlib.h>
#include <string.h>

static void fillin(GdkPixbuf *p, long fc)
{
    int W = gdk_pixbuf_get_width(p), H = gdk_pixbuf_get_height(p), rs = gdk_pixbuf_get_rowstride(p);
    guchar *px = gdk_pixbuf_get_pixels(p);
    int *st = malloc(sizeof(int) * (size_t)W * H * 4), sp = 0;
    char *out = calloc((size_t)W * H, 1);
    for (int x = 0; x < W; x++) {
        st[sp++] = x;
        st[sp++] = (H - 1) * W + x;
    }
    for (int y = 0; y < H; y++) {
        st[sp++] = y * W;
        st[sp++] = y * W + W - 1;
    }
    while (sp) {
        int k = st[--sp];
        if (out[k] || px[(k / W) * rs + (k % W) * 4 + 3] >= 128)
            continue;
        out[k] = 1;
        int x = k % W, y = k / W;
        if (x > 0) st[sp++] = k - 1;
        if (x < W - 1) st[sp++] = k + 1;
        if (y > 0) st[sp++] = k - W;
        if (y < H - 1) st[sp++] = k + W;
    }
    for (int k = 0; k < W * H; k++) {
        guchar *q = px + (k / W) * rs + (k % W) * 4;
        if (out[k] || q[3] == 255)
            continue;
        int a = q[3];                     /* the fill goes under the anti-aliased line */
        q[0] = (q[0] * a + (fc >> 16 & 255) * (255 - a)) / 255;
        q[1] = (q[1] * a + (fc >> 8 & 255) * (255 - a)) / 255;
        q[2] = (q[2] * a + (fc & 255) * (255 - a)) / 255;
        q[3] = 255;
    }
    free(st);
    free(out);
}

int main(int c, char **v)
{
    if (c < 4)
        return 1;
    GdkPixbuf *p = gdk_pixbuf_new_from_file(v[1], NULL);
    if (!p)
        return 2;
    if (!gdk_pixbuf_get_has_alpha(p))
        p = gdk_pixbuf_add_alpha(p, FALSE, 0, 0, 0);
    int size = atoi(v[3]);
    for (int i = 4; i < c; i++) {
        int W = gdk_pixbuf_get_width(p), H = gdk_pixbuf_get_height(p), rs = gdk_pixbuf_get_rowstride(p);
        guchar *px = gdk_pixbuf_get_pixels(p);
        if (!strcmp(v[i], "color") && i + 1 < c) {
            long col = strtol(v[++i], NULL, 16);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    guchar *q = px + y * rs + x * 4;
                    q[0] = col >> 16;
                    q[1] = col >> 8;
                    q[2] = col;
                }
        } else if (!strcmp(v[i], "erase") && i + 4 < c) {
            int x0 = atoi(v[i + 1]), y0 = atoi(v[i + 2]), w = atoi(v[i + 3]), h = atoi(v[i + 4]);
            for (int y = y0; y < y0 + h && y < H; y++)
                for (int x = x0; x < x0 + w && x < W; x++)
                    px[y * rs + x * 4 + 3] = 0;
            i += 4;
        } else if (!strcmp(v[i], "fillin") && i + 1 < c) {
            fillin(p, strtol(v[++i], NULL, 16));
        }
    }
    int w = gdk_pixbuf_get_width(p), h = gdk_pixbuf_get_height(p);
    double k = (double)size / (w > h ? w : h);
    int nw = (int)(w * k + 0.5), nh = (int)(h * k + 0.5);
    GdkPixbuf *s = gdk_pixbuf_scale_simple(p, nw, nh, GDK_INTERP_HYPER);
    GdkPixbuf *out = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);
    gdk_pixbuf_fill(out, 0);
    gdk_pixbuf_copy_area(s, 0, 0, nw, nh, out, (size - nw) / 2, (size - nh) / 2);
    return gdk_pixbuf_save(out, v[2], "png", NULL, NULL) ? 0 : 3;
}
