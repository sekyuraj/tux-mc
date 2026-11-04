/* tuxmc.h — libtuxmc: the native half of the tux client mod (also linked
 * into the tux-mc launcher). Pixels are ARGB, straight alpha. */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t accent, selection, bg, fg_bright, warn;
} tuxmc_pal_t;

typedef struct {
    const uint32_t *px;
    int32_t w, h;
} tuxmc_img_t;

/* NULL-terminated, in menu order ("image" = the user's own picture) */
extern const char *const tuxmc_cape_ids[];

/* Paint cape `id` in the vanilla 64x32 layout scaled by s into out
 * (64*s x 32*s). img: the logo for a logo cape (see tuxmc_cape_logo), the picture
 * for "image". */
void tuxmc_cape(const char *id, int s, const tuxmc_pal_t *pal, const tuxmc_img_t *img, uint32_t *out);

/* 1 if cape `id` is a logo cape: it needs its <id>.png passed as img */
int tuxmc_cape_logo(const char *id);

/* one socket of this process (fixed 40-byte layout, read from Java) */
typedef struct {
    uint8_t proto;                       /* 6 tcp, 17 udp */
    uint8_t family;                      /* 4 or 6 */
    uint16_t port;                       /* remote, host order */
    uint32_t pad;
    uint8_t addr[16];                    /* remote; v4 in the first 4 bytes */
    uint64_t sent, recv;                 /* bytes (tcp only) */
} tuxmc_conn_t;

/* this process's connected sockets, straight from the kernel (sock_diag):
 * no `ss` process per sample. Returns the count, or -1. */
int tuxmc_net(tuxmc_conn_t *out, int max);
