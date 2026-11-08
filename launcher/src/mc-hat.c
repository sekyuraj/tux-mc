/* mc-hat.c — head models ("hats"): the file format, the built-in starting
 * points and MagicaVoxel import.
 *
 * A hat is an N x N x N grid (N = 16 or 32) over a 16 x 16 x 16 skin-pixel
 * space around the head: x and z run -8..8 from the head's middle, y runs from
 * 2 pixels under the head's bottom to 6 above its top. The face is at high z.
 * The mod (Hats.java) reads the same file, so keep the two in step.
 *
 * File: zlib of  "TUXHAT1\0", N, name length, name, colour count P (1..255),
 *       P * RGB, then N^3 bytes (0 = empty, k = colour k), x fastest, then z, then y.
 */
#include "mc.h"

#include <math.h>
#include <string.h>
#include <zlib.h>

#define MAGIC "TUXHAT1"

void mc_hat_free(mc_hat_t *h)
{
    g_free(h->c);
    h->c = NULL;
    h->n = 0;
}

void mc_hat_new(mc_hat_t *h, int n)
{
    mc_hat_free(h);
    h->n = n;
    h->c = g_new0(guint32, n * n * n);
}

void mc_hat_copy(mc_hat_t *to, const mc_hat_t *from)
{
    mc_hat_new(to, from->n);
    memcpy(to->c, from->c, sizeof(guint32) * from->n * from->n * from->n);
    g_strlcpy(to->name, from->name, sizeof to->name);
}

int mc_hat_count(const mc_hat_t *h)
{
    int k = 0;
    for (int i = 0; i < h->n * h->n * h->n; i++)
        k += h->c[i] != 0;
    return k;
}

/* the head's cells in an n grid */
bool mc_hat_is_head(int n, int x, int y, int z)
{
    int s = n / 16;
    return x >= 4 * s && x < 12 * s && z >= 4 * s && z < 12 * s && y >= 2 * s && y < 10 * s;
}

GBytes *mc_hat_encode(const mc_hat_t *h)
{
    int n = h->n, nn = n * n * n;
    /* at most 255 colours: drop low bits until it fits */
    guint32 pal[255];
    int np = 0;
    guint8 *idx = g_malloc(nn);
    static const guint32 masks[] = {0xFFFFFF, 0xFEFEFE, 0xFCFCFC, 0xF8F8F8, 0xF0F0F0, 0xE0E0E0, 0xC0C0C0, 0x808080};
    for (size_t m = 0; m < G_N_ELEMENTS(masks); m++) {
        guint32 mask = masks[m];
        np = 0;
        bool full = false;
        for (int i = 0; i < nn && !full; i++) {
            if (!h->c[i]) {
                idx[i] = 0;
                continue;
            }
            guint32 c = h->c[i] & mask;
            int k = 0;
            while (k < np && pal[k] != c)
                k++;
            if (k == np) {
                if (np == 255) {
                    full = true;
                    break;
                }
                pal[np++] = c;
            }
            idx[i] = k + 1;
        }
        if (!full)
            break;
    }
    GByteArray *raw = g_byte_array_new();
    g_byte_array_append(raw, (const guint8 *)MAGIC, 8);
    size_t nl = strlen(h->name);
    guint8 hd[2] = {(guint8)n, (guint8)MIN(nl, 63)};
    g_byte_array_append(raw, hd, 2);
    g_byte_array_append(raw, (const guint8 *)h->name, hd[1]);
    guint8 pc = (guint8)MAX(np, 1);
    g_byte_array_append(raw, &pc, 1);
    for (int k = 0; k < pc; k++) {
        guint32 c = k < np ? pal[k] : 0x808080;
        guint8 rgb[3] = {c >> 16 & 255, c >> 8 & 255, c & 255};
        g_byte_array_append(raw, rgb, 3);
    }
    g_byte_array_append(raw, idx, nn);
    g_free(idx);
    uLongf zl = compressBound(raw->len);
    guint8 *z = g_malloc(zl);
    compress2(z, &zl, raw->data, raw->len, 9);
    g_byte_array_unref(raw);
    return g_bytes_new_take(z, zl);
}

bool mc_hat_decode(const guint8 *data, gsize len, mc_hat_t *h)
{
    uLongf rl = 8 + 2 + 64 + 1 + 255 * 3 + 32 * 32 * 32;
    guint8 *r = g_malloc(rl);
    bool ok = uncompress(r, &rl, data, len) == Z_OK && rl > 11 && !memcmp(r, MAGIC, 8);
    int n = ok ? r[8] : 0, nl = ok ? r[9] : 0;
    ok = ok && (n == 16 || n == 32) && nl < 64 && rl >= (uLongf)(11 + nl);
    int pc = ok ? r[10 + nl] : 0;
    size_t off = 11 + nl + pc * 3;
    ok = ok && pc >= 1 && rl == off + (size_t)n * n * n;
    if (ok) {
        mc_hat_new(h, n);
        memcpy(h->name, r + 10, nl);
        h->name[nl] = 0;
        const guint8 *p = r + 11 + nl;
        for (int i = 0; i < n * n * n; i++) {
            int k = r[off + i];
            if (k && k <= pc)
                h->c[i] = 0xFF000000u | p[(k - 1) * 3] << 16 | p[(k - 1) * 3 + 1] << 8 | p[(k - 1) * 3 + 2];
        }
    }
    g_free(r);
    return ok;
}

bool mc_hat_load(const char *path, mc_hat_t *h)
{
    char *d = NULL;
    gsize l = 0;
    if (!g_file_get_contents(path, &d, &l, NULL))
        return false;
    bool ok = mc_hat_decode((guint8 *)d, l, h);
    g_free(d);
    return ok;
}

bool mc_hat_save(const char *path, const mc_hat_t *h)
{
    GBytes *b = mc_hat_encode(h);
    gsize l;
    const char *d = g_bytes_get_data(b, &l);
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    bool ok = g_file_set_contents(path, d, l, NULL);
    g_bytes_unref(b);
    return ok;
}

char *mc_hats_dir(void) { return g_build_filename(g_get_user_data_dir(), "tux-mc", "hats", NULL); }

/* ------------------------------------------------------------ presets -- */

static mc_hat_t *P;

static void put(int x, int y, int z, guint32 rgb)
{
    if (x < 0 || y < 0 || z < 0 || x >= 16 || y >= 16 || z >= 16)
        return;
    P->c[(y * 16 + z) * 16 + x] = 0xFF000000u | rgb;
}

static void boxf(int x0, int y0, int z0, int x1, int y1, int z1, guint32 rgb)
{
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                put(x, y, z, rgb);
}

/* a ring of cells around the head's middle (7.5, 7.5) */
static void ring(int y, double r0, double r1, guint32 rgb)
{
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            double d = hypot(x - 7.5, z - 7.5);
            if (d >= r0 && d < r1)
                put(x, y, z, rgb);
        }
}

static void top_hat(void)
{
    boxf(2, 10, 2, 13, 10, 13, 0x1d1b1f);
    for (int c = 0; c < 4; c++)                         /* round the brim's corners */
        P->c[(10 * 16 + (c & 2 ? 13 : 2)) * 16 + (c & 1 ? 13 : 2)] = 0;
    boxf(4, 11, 4, 11, 15, 11, 0x26232a);
    boxf(4, 11, 4, 11, 11, 11, 0x8c2a2a);
}

static void crown(void)
{
    for (int y = 10; y <= 11; y++)
        for (int i = 4; i <= 11; i++) {
            put(i, y, 4, 0xe2b13c);
            put(i, y, 11, 0xe2b13c);
            put(4, y, i, 0xe2b13c);
            put(11, y, i, 0xe2b13c);
        }
    for (int i = 4; i <= 11; i += 2) {
        put(i, 12, 4, 0xf0c95a);
        put(i + 1, 12, 11, 0xf0c95a);
        put(4, 12, i + 1, 0xf0c95a);
        put(11, 12, i, 0xf0c95a);
    }
    put(7, 11, 11, 0xc23b3b);
    put(8, 11, 11, 0xc23b3b);
    put(4, 11, 7, 0x3b6ec2);
    put(11, 11, 8, 0x3b6ec2);
    put(7, 11, 4, 0x3bc27a);
}

static void cat_ears(void)
{
    for (int side = 0; side < 2; side++) {
        int x0 = side ? 9 : 4;
        boxf(x0, 10, 7, x0 + 2, 10, 8, 0x2b2b2e);
        boxf(x0 + (side ? 0 : 1), 11, 7, x0 + (side ? 1 : 2), 11, 8, 0x2b2b2e);
        put(side ? x0 : x0 + 2, 12, 7, 0x2b2b2e);
        put(side ? x0 : x0 + 2, 12, 8, 0x2b2b2e);
        put(x0 + 1, 10, 8, 0xe89aa8);
        put(side ? x0 + 1 : x0 + 1, 11, 8, 0xe89aa8);
    }
}

static void halo(void)
{
    ring(14, 3.6, 4.7, 0xf6d860);
}

static void horns(void)
{
    for (int side = 0; side < 2; side++) {
        int dir = side ? 1 : -1, x = side ? 12 : 3;
        boxf(x, 8, 6, x, 9, 7, 0xe9e2cf);
        boxf(x + dir, 9, 6, x + dir, 10, 7, 0xe2d9c0);
        put(x + dir, 11, 6, 0xd6cba9);
        put(x + dir, 11, 7, 0xd6cba9);
        put(x + dir, 12, 7, 0x6b6253);
    }
}

static void party_hat(void)
{
    static const guint32 st[] = {0x3a86d4, 0xf2f2f2};
    for (int y = 10; y <= 14; y++) {
        double r = 3.9 - (y - 10) * 0.8;
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++)
                if (hypot(x - 7.5, z - 7.5) < r)
                    put(x, y, z, st[((x + z + y) / 2) & 1]);
    }
    boxf(7, 15, 7, 8, 15, 8, 0xe85d75);
}

static void headphones(void)
{
    boxf(3, 10, 7, 12, 10, 8, 0x2c2c30);
    boxf(3, 5, 7, 3, 9, 8, 0x2c2c30);
    boxf(12, 5, 7, 12, 9, 8, 0x2c2c30);
    boxf(2, 4, 6, 3, 7, 9, 0x3d3d44);
    boxf(12, 4, 6, 13, 7, 9, 0x3d3d44);
    put(2, 6, 7, 0x5aa9e6);
    put(13, 6, 8, 0x5aa9e6);
}

static void sunglasses(void)
{
    boxf(4, 6, 12, 11, 6, 12, 0x1a1a1a);
    boxf(4, 5, 12, 6, 5, 12, 0x151515);
    boxf(9, 5, 12, 11, 5, 12, 0x151515);
    put(5, 6, 12, 0x3c4a5a);
    put(10, 6, 12, 0x3c4a5a);
    boxf(3, 6, 8, 3, 6, 12, 0x1a1a1a);
    boxf(12, 6, 8, 12, 6, 12, 0x1a1a1a);
}

static void propeller_cap(void)
{
    static const guint32 st[] = {0xd94848, 0xf2cf3a, 0x3f86d9, 0x4cae5a};
    for (int z = 4; z <= 11; z++)
        for (int x = 4; x <= 11; x++)
            put(x, 10, z, st[(x >= 8) + 2 * (z >= 8)]);
    boxf(5, 11, 5, 10, 11, 10, 0xd94848);
    boxf(5, 10, 12, 10, 10, 13, 0x3f86d9);
    put(7, 12, 7, 0x888888);
    put(7, 13, 7, 0x888888);
    boxf(3, 14, 7, 11, 14, 7, 0xf2cf3a);
    boxf(7, 14, 3, 7, 14, 11, 0xf2cf3a);
}

static void flowers(void)
{
    ring(10, 3.4, 4.6, 0x4e8a3a);
    static const guint32 fl[] = {0xf2a0c0, 0xf6e27a, 0xffffff, 0xb59af0};
    int k = 0;
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            double d = hypot(x - 7.5, z - 7.5);
            if (d >= 3.4 && d < 4.6 && (x * 7 + z * 3) % 5 == 0)
                put(x, 11, z, fl[k++ % 4]);
        }
}

static const struct { const char *name; void (*make)(void); } PRESETS[] = {
    {"Top hat", top_hat},       {"Crown", crown},         {"Cat ears", cat_ears},
    {"Halo", halo},             {"Horns", horns},         {"Party hat", party_hat},
    {"Headphones", headphones}, {"Sunglasses", sunglasses}, {"Propeller cap", propeller_cap},
    {"Flower crown", flowers},
};

int mc_hat_presets(void) { return G_N_ELEMENTS(PRESETS); }
const char *mc_hat_preset_name(int i) { return PRESETS[i].name; }

void mc_hat_preset(mc_hat_t *h, int i)
{
    mc_hat_new(h, 16);
    g_strlcpy(h->name, PRESETS[i].name, sizeof h->name);
    P = h;
    PRESETS[i].make();
    P = NULL;
}

/* ----------------------------------------------------- MagicaVoxel .vox -- */

static guint32 rd32(const guint8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (guint32)p[3] << 24; }

bool mc_hat_import_vox(const char *path, mc_hat_t *h, char **err)
{
    char *d = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &d, &len, NULL) || len < 20 || memcmp(d, "VOX ", 4)) {
        *err = g_strdup("That isn't a MagicaVoxel .vox file.");
        g_free(d);
        return false;
    }
    const guint8 *p = (const guint8 *)d, *end = p + len;
    int sx = 0, sy = 0, sz = 0;
    const guint8 *xyzi = NULL;
    guint32 nv = 0, pal[256];
    bool have_pal = false;
    for (const guint8 *c = p + 8; c + 12 <= end;) {
        guint32 cs = rd32(c + 4), ch = rd32(c + 8);
        const guint8 *body = c + 12;
        if (!memcmp(c, "MAIN", 4)) {
            c = body + cs;
            continue;
        }
        if (body + cs > end)
            break;
        if (!memcmp(c, "SIZE", 4) && cs >= 12 && !sx) {
            sx = rd32(body);
            sy = rd32(body + 4);
            sz = rd32(body + 8);
        } else if (!memcmp(c, "XYZI", 4) && cs >= 4 && !xyzi) {
            nv = rd32(body);
            if (4 + (guint64)nv * 4 <= cs)
                xyzi = body + 4;
        } else if (!memcmp(c, "RGBA", 4) && cs >= 1024) {
            for (int i = 0; i < 255; i++)
                pal[i + 1] = body[i * 4] << 16 | body[i * 4 + 1] << 8 | body[i * 4 + 2];
            have_pal = true;
        }
        c = body + cs + ch;
    }
    if (!xyzi || sx <= 0 || sy <= 0 || sz <= 0) {
        *err = g_strdup("No model found in that file.");
        g_free(d);
        return false;
    }
    if (!have_pal)                       /* MagicaVoxel's default palette isn't stored: make up greys and hues */
        for (int i = 1; i < 256; i++)
            pal[i] = (guint32)(i * 2654435761u) & 0xFFFFFF;
    /* .vox has z up; ours has y up and the face at high z (vox -y is its front) */
    int w = sx, dep = sy, ht = sz, big = MAX(w, MAX(dep, ht));
    int n = big > 16 ? 32 : 16;
    double k = big > 32 ? 32.0 / big : 1;
    int ow = (int)ceil(w * k), od = (int)ceil(dep * k), oh = (int)ceil(ht * k);
    int ox = (n - ow) / 2, oz = (n - od) / 2;
    int head_top = 10 * (n / 16);
    int oy = oh <= n - head_top ? head_top : MAX(0, n - oh);
    mc_hat_new(h, n);
    char *base = g_path_get_basename(path);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = 0;
    g_strlcpy(h->name, base, sizeof h->name);
    g_free(base);
    for (guint32 i = 0; i < nv; i++) {
        const guint8 *v = xyzi + i * 4;
        int x = ox + (int)(v[0] * k), z = oz + (int)((dep - 1 - v[1]) * k), y = oy + (int)(v[2] * k);
        if (x >= 0 && y >= 0 && z >= 0 && x < n && y < n && z < n && v[3])
            h->c[(y * n + z) * n + x] = 0xFF000000u | pal[v[3]];
    }
    g_free(d);
    return true;
}

/* 16 <-> 32: each cell becomes 8, or the first filled of 8 wins */
void mc_hat_resize(mc_hat_t *h, int n)
{
    if (n == h->n)
        return;
    mc_hat_t o = {0};
    mc_hat_copy(&o, h);
    mc_hat_new(h, n);
    g_strlcpy(h->name, o.name, sizeof h->name);
    for (int y = 0; y < n; y++)
        for (int z = 0; z < n; z++)
            for (int x = 0; x < n; x++) {
                guint32 c = 0;
                if (n > o.n)
                    c = o.c[((y / 2) * o.n + z / 2) * o.n + x / 2];
                else
                    for (int k = 0; k < 8 && !c; k++)
                        c = o.c[((y * 2 + (k >> 2)) * o.n + z * 2 + (k >> 1 & 1)) * o.n + x * 2 + (k & 1)];
                h->c[(y * n + z) * n + x] = c;
            }
    mc_hat_free(&o);
}
