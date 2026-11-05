/* sheet — paints capes with the real cape code (mod/native/capes.c) and puts
 * their outside faces side by side, to look at them before a build.
 *   sheet out.png SCALE assets-dir id...
 * Build: cc -O2 -I../../mod/native -o sheet sheet.c ../../mod/native/capes.c \
 *        $(pkg-config --cflags --libs gdk-pixbuf-2.0) -lm */
#include "tuxmc.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t *load(const char *p, int *w, int *h)
{
    GdkPixbuf *b = gdk_pixbuf_new_from_file(p, NULL);
    if (!b)
        return NULL;
    if (!gdk_pixbuf_get_has_alpha(b))
        b = gdk_pixbuf_add_alpha(b, FALSE, 0, 0, 0);
    *w = gdk_pixbuf_get_width(b);
    *h = gdk_pixbuf_get_height(b);
    int rs = gdk_pixbuf_get_rowstride(b);
    guchar *s = gdk_pixbuf_get_pixels(b);
    uint32_t *o = malloc(4 * (size_t)*w * *h);
    for (int y = 0; y < *h; y++)
        for (int x = 0; x < *w; x++) {
            guchar *q = s + y * rs + x * 4;
            o[y * *w + x] = (uint32_t)q[3] << 24 | q[0] << 16 | q[1] << 8 | q[2];
        }
    return o;
}

int main(int c, char **v)
{
    if (c < 5)
        return 1;
    const int S = atoi(v[2]), n = c - 4, fw = 10 * S, fh = 16 * S, pad = S;
    tuxmc_pal_t pal = {0xFFFFFFFF, 0xFF333333, 0xFF000000, 0xFFFFFFFF, 0xFFD9D9D9};
    GdkPixbuf *out = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, n * (fw + pad) + pad, fh + 2 * pad);
    gdk_pixbuf_fill(out, 0x808080ff);
    uint32_t *buf = malloc(4 * (size_t)64 * S * 32 * S);
    for (int i = 0; i < n; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s.png", v[3], v[i + 4]);
        tuxmc_img_t img = {0};
        img.px = load(p, &img.w, &img.h);
        tuxmc_cape(v[i + 4], S, &pal, &img, buf);
        int rs = gdk_pixbuf_get_rowstride(out);
        guchar *d = gdk_pixbuf_get_pixels(out);
        for (int y = 0; y < fh; y++)
            for (int x = 0; x < fw; x++) {
                uint32_t px = buf[(size_t)(S + y) * 64 * S + S + x];
                guchar *q = d + (pad + y) * rs + (pad + i * (fw + pad) + x) * 3;
                q[0] = px >> 16;
                q[1] = px >> 8;
                q[2] = px;
            }
    }
    return gdk_pixbuf_save(out, v[1], "png", NULL, NULL) ? 0 : 2;
}
