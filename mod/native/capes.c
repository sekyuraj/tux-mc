/* capes.c — the tux client capes, painted pixel by pixel. Used by the mod
 * (through Java's FFM, see Native.java) and linked straight into the tux-mc
 * launcher, so both show the same cape. */
#include "tuxmc.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    uint32_t r = 0;
    for (int s = 0; s <= 24; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        r |= (uint32_t)lroundf(ca + (cb - ca) * t) << s;
    }
    return r;
}

/* src (straight alpha) over an opaque dst */
static uint32_t over(uint32_t dst, uint32_t src)
{
    return 0xFF000000u | (mix(dst, src, (src >> 24) / 255.0f) & 0xFFFFFF);
}

static int ell(float x, float y, float rx, float ry) { return x * x / (rx * rx) + y * y / (ry * ry) <= 1; }

/* a penguin centred at (0.5, ~0.5); 0 = not on it */
static uint32_t penguin(float u, float v, uint32_t belly, uint32_t body, uint32_t beak)
{
    float x = (u - 0.5f) * 10 / 16, y = v - 0.5f;
    if (ell(x, y - 0.06f, 0.2f, 0.26f))
        return ell(x, y - 0.1f, 0.13f, 0.19f) ? belly : body;
    if (ell(x, y + 0.27f, 0.13f, 0.12f)) {
        if (ell(x - 0.045f, y + 0.29f, 0.025f, 0.03f) || ell(x + 0.045f, y + 0.29f, 0.025f, 0.03f))
            return ell(x - 0.045f, y + 0.285f, 0.011f, 0.014f) || ell(x + 0.045f, y + 0.285f, 0.011f, 0.014f) ? body
                                                                                                                : belly;
        return ell(x, y + 0.22f, 0.055f, 0.03f) ? beak : body;
    }
    if (ell(x - 0.09f, y - 0.33f, 0.08f, 0.035f) || ell(x + 0.09f, y - 0.33f, 0.08f, 0.035f))
        return beak;
    return 0;
}

static float hash(float u, float v, int cells)
{
    uint64_t h = (uint64_t)(int)(u * cells * 10 / 16.0f) * 0x9E3779B97F4A7C15ull ^
                 (uint64_t)(int)(v * cells) * 0xC2B2AE3D27D4EB4Full ^ 0x7C0FFEEull;
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;
    return (h >> 40) / (float)(1 << 24);
}

/* bilinear sample of an ARGB image (straight alpha); 0 outside */
static uint32_t sample(const tuxmc_img_t *im, float u, float v)
{
    if (!im || !im->px || u < 0 || v < 0 || u >= 1 || v >= 1)
        return 0;
    float fx = u * (im->w - 1), fy = v * (im->h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1 < im->w ? x0 + 1 : x0, y1 = y0 + 1 < im->h ? y0 + 1 : y0;
    float tx = fx - x0, ty = fy - y0;
    uint32_t top = mix(im->px[y0 * im->w + x0], im->px[y0 * im->w + x1], tx);
    uint32_t bot = mix(im->px[y1 * im->w + x0], im->px[y1 * im->w + x1], tx);
    return mix(top, bot, ty);
}

/* u: 0..1 left->right, v: 0..1 top->bottom of the 10x16 outside face */
static uint32_t color(const char *id, float u, float v, const tuxmc_pal_t *p, const tuxmc_img_t *img)
{
    if (!strcmp(id, "theme")) {
        float bx = fminf(fminf(u, 1 - u) * 10, fminf(v, 1 - v) * 16);
        if (bx < 0.45f)
            return p->accent;
        if (bx < 0.6f)
            return p->bg;
        uint32_t c = penguin(u, v * 1.6f - 0.25f, p->fg_bright, p->bg, p->warn);
        return c ? c : mix(p->accent, p->selection, v);
    }
    if (!strcmp(id, "tux")) {
        uint32_t c = penguin(u, v * 1.25f - 0.08f, 0xFFF2F2F2, 0xFF0B0B0B, 0xFFF4B400);
        return c ? c : mix(0xFF1A1A1A, 0xFF2C2C2C, v);
    }
    if (!strcmp(id, "gentoo")) {
        uint32_t c = mix(0xFF625A8C, 0xFF2E2848, v);
        uint32_t l = sample(img, (u - 0.08f) / 0.84f, (v * 1.6f - 0.18f) / 0.84f);
        return l ? over(c, l) : c;
    }
    if (!strcmp(id, "image")) {          /* the user's picture, cover-fit to the face */
        if (!img || !img->px)
            return 0xFF303030;
        float face = 10.0f / 16, ia = (float)img->w / img->h, su = u, sv = v;
        if (ia > face)
            su = 0.5f + (u - 0.5f) * face / ia;
        else
            sv = 0.5f + (v - 0.5f) * ia / face;
        return over(p->bg, sample(img, su, sv));
    }
    if (!strcmp(id, "stars")) {
        uint32_t c = mix(0xFF050713, 0xFF151A3A, v);
        float neb = sinf(u * 7 + v * 3) * cosf(v * 9 - u * 2);
        c = mix(c, 0xFF5A2B78, fmaxf(0, neb) * 0.35f);
        if (hash(u, v, 140) > 0.996f)
            return 0xFFFFFFFF;
        if (hash(u, v, 60) > 0.993f)
            return 0xFFBFD4FF;
        return c;
    }
    if (!strcmp(id, "aurora")) {
        uint32_t c = mix(0xFF04110F, 0xFF0B1A2E, v);
        for (int i = 0; i < 3; i++) {
            float band = 0.25f + i * 0.22f + 0.06f * sinf(u * 9 + i * 2.1f);
            c = mix(c, i == 1 ? 0xFF9B5DE5 : 0xFF3CF0A8, fmaxf(0, 1 - fabsf(v - band) * 14) * 0.85f);
        }
        return c;
    }
    if (!strcmp(id, "ember")) {
        float n = 0.5f + 0.25f * sinf(u * 23 + v * 7) + 0.25f * sinf(u * 5 - v * 19);
        float t = fmaxf(0, fminf(1, (1 - v) * 1.3f - 0.3f + n * 0.4f));
        return mix(mix(0xFF120403, 0xFFB3260E, t), 0xFFFFC14D, fmaxf(0, t - 0.65f) * 2.6f);
    }
    if (!strcmp(id, "ocean")) {
        float wave = sinf(u * 12 + sinf(v * 20) * 1.5f);
        uint32_t c = mix(0xFF0B3C5D, 0xFF021526, v);
        return fabsf(fmodf(v * 10 + wave * 0.12f + 10, 1.0f) - 0.5f) < 0.04f ? mix(c, 0xFF8FE3FF, 0.6f) : c;
    }
    if (!strcmp(id, "synth")) {
        float sx = (u - 0.5f) * 10 / 16, sy = v - 0.33f;
        if (hypotf(sx, sy) < 0.2f && v < 0.5f && !(v > 0.38f && (int)(v * 60) % 2 == 0))
            return mix(0xFFFFD319, 0xFFFF2975, (v - 0.13f) / 0.4f);
        if (v > 0.55f) {
            float gy = powf((v - 0.55f) / 0.45f, 0.6f);
            float lx = (u - 0.5f) / fmaxf(0.05f, gy);
            if (fabsf(lx * 4 - roundf(lx * 4)) < 0.05f / fmaxf(0.2f, gy) || fabsf(gy * 6 - roundf(gy * 6)) < 0.06f)
                return 0xFFFF2BD6;
            return 0xFF12021F;
        }
        return mix(0xFF1B0634, 0xFF7A0F5C, v * 1.6f);
    }
    return 0xFF808080;
}

static void fill(uint32_t *out, int s, int x, int y, int w, int h, uint32_t c)
{
    for (int py = y * s; py < (y + h) * s; py++)
        for (int px = x * s; px < (x + w) * s; px++)
            out[py * 64 * s + px] = c;
}

static void face(uint32_t *out, int s, const char *id, int fx, int fy, const tuxmc_pal_t *p, const tuxmc_img_t *img)
{
    int w = 10 * s, h = 16 * s;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            out[(fy * s + y) * 64 * s + fx * s + x] = color(id, (x + 0.5f) / w, (y + 0.5f) / h, p, img);
}

const char *const tuxmc_cape_ids[] = {"theme", "tux", "gentoo", "stars", "aurora", "ember", "ocean", "synth", "image",
                                      NULL};

void tuxmc_cape(const char *id, int s, const tuxmc_pal_t *p, const tuxmc_img_t *img, uint32_t *out)
{
    memset(out, 0, sizeof(uint32_t) * 64 * s * 32 * s);
    face(out, s, id, 1, 1, p, img);      /* outside */
    face(out, s, id, 12, 1, p, img);     /* inside */
    uint32_t edge = mix(color(id, 0.5f, 0.0f, p, img), 0xFF000000, 0.35f);
    fill(out, s, 0, 1, 1, 16, edge);
    fill(out, s, 11, 1, 1, 16, edge);
    fill(out, s, 1, 0, 20, 1, edge);
}
