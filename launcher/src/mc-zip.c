/* mc-zip.c — reading zip files without an unzip program (Windows has none):
 * the central directory, stored and deflated entries, zip64 sizes. Used for
 * mod metadata and for importing worlds and packs.
 *
 * Extracting is careful: every name is checked before anything is written,
 * so an entry can't land outside the target folder (no "..", no absolute or
 * drive paths), and symlinks are skipped. */
#include "mc.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    char *name;
    guint64 csize, usize, offset;
    guint16 method;
    bool dir, link;
} zent_t;

static void zent_free(zent_t *z)
{
    g_free(z->name);
    g_free(z);
}

static guint16 u16(const guchar *p) { return (guint16)(p[0] | p[1] << 8); }
static guint32 u32(const guchar *p) { return (guint32)p[0] | (guint32)p[1] << 8 | (guint32)p[2] << 16 | (guint32)p[3] << 24; }
static guint64 u64(const guchar *p) { return (guint64)u32(p) | (guint64)u32(p + 4) << 32; }

static bool seek(FILE *f, guint64 off)
{
#ifdef _WIN32
    return _fseeki64(f, (__int64)off, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)off, SEEK_SET) == 0;
#endif
}

static guint64 file_size(FILE *f)
{
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    return (guint64)_ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    return (guint64)ftello(f);
#endif
}

/* all entries, or NULL when it isn't a zip we can read */
static GPtrArray *entries(FILE *f)
{
    guint64 size = file_size(f);
    if (size < 22)
        return NULL;
    /* end of central directory: in the last 64 KiB + 22 bytes */
    guint64 tail = MIN(size, 65557);
    guchar *buf = g_malloc(tail);
    if (!seek(f, size - tail) || fread(buf, 1, tail, f) != tail) {
        g_free(buf);
        return NULL;
    }
    gssize eocd = -1;
    for (gssize i = (gssize)tail - 22; i >= 0 && eocd < 0; i--)
        if (u32(buf + i) == 0x06054b50)
            eocd = i;
    if (eocd < 0) {
        g_free(buf);
        return NULL;
    }
    guint64 count = u16(buf + eocd + 10), cdsize = u32(buf + eocd + 12), cdoff = u32(buf + eocd + 16);
    /* zip64: the locator sits right before the EOCD */
    if (eocd >= 20 && u32(buf + eocd - 20) == 0x07064b50) {
        guint64 z64 = u64(buf + eocd - 20 + 8);
        guchar r[56];
        if (seek(f, z64) && fread(r, 1, 56, f) == 56 && u32(r) == 0x06064b50) {
            count = u64(r + 32);
            cdsize = u64(r + 40);
            cdoff = u64(r + 48);
        }
    }
    g_free(buf);
    if (cdsize > 512u * 1024 * 1024 || cdoff + cdsize > size)
        return NULL;
    guchar *cd = g_malloc(cdsize ? cdsize : 1);
    if (!seek(f, cdoff) || fread(cd, 1, cdsize, f) != cdsize) {
        g_free(cd);
        return NULL;
    }
    GPtrArray *all = g_ptr_array_new_with_free_func((GDestroyNotify)zent_free);
    guint64 p = 0;
    for (guint64 n = 0; n < count && p + 46 <= cdsize; n++) {
        const guchar *h = cd + p;
        if (u32(h) != 0x02014b50)
            break;
        guint16 nlen = u16(h + 28), xlen = u16(h + 30), clen = u16(h + 32);
        if (p + 46 + nlen + xlen + clen > cdsize)
            break;
        zent_t *z = g_new0(zent_t, 1);
        z->method = u16(h + 10);
        z->csize = u32(h + 20);
        z->usize = u32(h + 24);
        z->offset = u32(h + 42);
        guint32 ext = u32(h + 38);
        z->link = (h[5] == 3) && ((ext >> 16) & 0170000) == 0120000;   /* unix symlink */
        /* names are UTF-8 (flag 11) or, in old zips, CP437: keep what is valid */
        z->name = g_utf8_make_valid((const char *)h + 46, nlen);
        /* zip64 extra: the 0xFFFFFFFF fields, in order */
        const guchar *x = h + 46 + nlen, *xe = x + xlen;
        while (x + 4 <= xe) {
            guint16 id = u16(x), sz = u16(x + 2);
            const guchar *d = x + 4;
            if (id == 0x0001) {
                if (z->usize == 0xFFFFFFFFu && d + 8 <= x + 4 + sz) { z->usize = u64(d); d += 8; }
                if (z->csize == 0xFFFFFFFFu && d + 8 <= x + 4 + sz) { z->csize = u64(d); d += 8; }
                if (z->offset == 0xFFFFFFFFu && d + 8 <= x + 4 + sz) { z->offset = u64(d); }
            }
            x += 4 + sz;
        }
        g_strdelimit(z->name, "\\", '/');
        z->dir = g_str_has_suffix(z->name, "/");
        g_ptr_array_add(all, z);
        p += 46 + nlen + xlen + clen;
    }
    g_free(cd);
    return all;
}

/* the data of one entry, through out (a FILE or a GByteArray) */
static bool inflate_to(FILE *f, const zent_t *z, FILE *out, GByteArray *mem, guint64 max)
{
    guchar lh[30];
    if (!seek(f, z->offset) || fread(lh, 1, 30, f) != 30 || u32(lh) != 0x04034b50)
        return false;
    if (!seek(f, z->offset + 30 + u16(lh + 26) + u16(lh + 28)))
        return false;
    if (z->usize > max)
        return false;
    guchar in[65536], buf[65536];
    guint64 left = z->csize, written = 0;
    if (z->method == 0) {
        while (left) {
            size_t k = fread(in, 1, MIN(left, sizeof in), f);
            if (!k)
                return false;
            if (out && fwrite(in, 1, k, out) != k)
                return false;
            if (mem)
                g_byte_array_append(mem, in, (guint)k);
            left -= k;
        }
        return true;
    }
    if (z->method != 8)
        return false;
    z_stream s = {0};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK)
        return false;
    int r = Z_OK;
    while (r != Z_STREAM_END) {
        if (!s.avail_in) {
            size_t k = left ? fread(in, 1, MIN(left, sizeof in), f) : 0;
            if (!k)
                break;
            left -= k;
            s.next_in = in;
            s.avail_in = (uInt)k;
        }
        s.next_out = buf;
        s.avail_out = sizeof buf;
        r = inflate(&s, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END)
            break;
        size_t got = sizeof buf - s.avail_out;
        written += got;
        if (written > max)               /* lies about its size */
            break;
        if (out && got && fwrite(buf, 1, got, out) != got)
            break;
        if (mem && got)
            g_byte_array_append(mem, buf, (guint)got);
    }
    inflateEnd(&s);
    return r == Z_STREAM_END;
}

GBytes *mc_zip_read(const char *zip, const char *pattern)
{
    FILE *f = g_fopen(zip, "rb");
    if (!f)
        return NULL;
    GPtrArray *all = entries(f);
    GBytes *res = NULL;
    for (guint i = 0; all && i < all->len && !res; i++) {
        zent_t *z = all->pdata[i];
        if (z->dir || !g_pattern_match_simple(pattern, z->name))
            continue;
        GByteArray *b = g_byte_array_new();
        if (inflate_to(f, z, NULL, b, 64u * 1024 * 1024) && b->len)
            res = g_byte_array_free_to_bytes(b);
        else
            g_byte_array_unref(b);
    }
    if (all)
        g_ptr_array_unref(all);
    fclose(f);
    return res;
}

char **mc_zip_list(const char *zip)
{
    FILE *f = g_fopen(zip, "rb");
    if (!f)
        return NULL;
    GPtrArray *all = entries(f);
    fclose(f);
    if (!all)
        return NULL;
    GStrvBuilder *b = g_strv_builder_new();
    for (guint i = 0; i < all->len; i++)
        g_strv_builder_add(b, ((zent_t *)all->pdata[i])->name);
    g_ptr_array_unref(all);
    char **r = g_strv_builder_end(b);
    g_strv_builder_unref(b);
    return r;
}

/* a relative path that stays inside the target, or NULL */
static char *safe_rel(const char *name)
{
    if (!*name || name[0] == '/' || (g_ascii_isalpha(name[0]) && name[1] == ':'))
        return NULL;
    char **parts = g_strsplit(name, "/", -1);
    GString *s = g_string_new(NULL);
    bool ok = true;
    for (char **p = parts; *p && ok; p++) {
        if (!**p || !strcmp(*p, "."))
            continue;
        if (!strcmp(*p, "..") || strpbrk(*p, ":*?\"<>|"))
            ok = false;
        else {
            if (s->len)
                g_string_append_c(s, G_DIR_SEPARATOR);
            g_string_append(s, *p);
        }
    }
    g_strfreev(parts);
    if (!ok || !s->len) {
        g_string_free(s, TRUE);
        return NULL;
    }
    return g_string_free(s, FALSE);
}

bool mc_zip_extract(const char *zip, const char *prefix, const char *dest, char **err)
{
    FILE *f = g_fopen(zip, "rb");
    if (!f) {
        *err = g_strdup("can't open the zip");
        return false;
    }
    GPtrArray *all = entries(f);
    if (!all) {
        fclose(f);
        *err = g_strdup("not a zip file (or a damaged one)");
        return false;
    }
    size_t pl = prefix ? strlen(prefix) : 0;
    bool ok = true;
    guint done = 0;
    for (guint i = 0; i < all->len && ok; i++) {
        zent_t *z = all->pdata[i];
        if (z->link || (pl && strncmp(z->name, prefix, pl)))
            continue;
        char *rel = safe_rel(z->name + pl);
        if (!rel) {
            if (z->name[pl])             /* "" is the prefix folder itself */
                g_message("zip: skipped unsafe entry %s", z->name);
            continue;
        }
        char *path = g_build_filename(dest, rel, NULL);
        if (z->dir) {
            ok = g_mkdir_with_parents(path, 0755) == 0;
        } else {
            char *dir = g_path_get_dirname(path);
            g_mkdir_with_parents(dir, 0755);
            g_free(dir);
            FILE *o = g_fopen(path, "wb");
            ok = o && inflate_to(f, z, o, NULL, G_MAXUINT64);
            if (o && fclose(o) != 0)
                ok = false;
            if (!ok)
                *err = g_strdup_printf("couldn't unpack %s", z->name);
            done++;
        }
        g_free(path);
        g_free(rel);
    }
    if (ok && !done) {
        ok = false;
        *err = g_strdup("nothing to unpack");
    }
    g_ptr_array_unref(all);
    fclose(f);
    return ok;
}

/* ---------------------------------------------- many reads from one zip -- */

struct mc_zip {
    FILE *f;
    GPtrArray *all;
    GHashTable *by_name;                 /* name -> zent_t (not owned) */
};

mc_zip_t *mc_zip_open(const char *zip)
{
    FILE *f = g_fopen(zip, "rb");
    if (!f)
        return NULL;
    GPtrArray *all = entries(f);
    if (!all) {
        fclose(f);
        return NULL;
    }
    mc_zip_t *z = g_new0(mc_zip_t, 1);
    z->f = f;
    z->all = all;
    z->by_name = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < all->len; i++) {
        zent_t *e = all->pdata[i];
        if (!e->dir)
            g_hash_table_insert(z->by_name, e->name, e);
    }
    return z;
}

GBytes *mc_zip_get(mc_zip_t *z, const char *name)
{
    zent_t *e = z ? g_hash_table_lookup(z->by_name, name) : NULL;
    if (!e)
        return NULL;
    GByteArray *b = g_byte_array_new();
    if (inflate_to(z->f, e, NULL, b, 64u * 1024 * 1024))
        return g_byte_array_free_to_bytes(b);
    g_byte_array_unref(b);
    return NULL;
}

void mc_zip_names(mc_zip_t *z, void (*each)(const char *name, void *data), void *data)
{
    for (guint i = 0; z && i < z->all->len; i++) {
        zent_t *e = z->all->pdata[i];
        if (!e->dir)
            each(e->name, data);
    }
}

void mc_zip_close(mc_zip_t *z)
{
    if (!z)
        return;
    g_hash_table_unref(z->by_name);
    g_ptr_array_unref(z->all);
    fclose(z->f);
    g_free(z);
}

/* --------------------------------------------------------------- writing -- */

typedef struct {
    char *name;
    guint32 crc, csize, usize, offset;
    guint16 method;
} went_t;

static void put16(GByteArray *b, guint16 v) { guint8 x[2] = {v & 255, v >> 8}; g_byte_array_append(b, x, 2); }
static void put32(GByteArray *b, guint32 v)
{
    guint8 x[4] = {v & 255, v >> 8 & 255, v >> 16 & 255, v >> 24};
    g_byte_array_append(b, x, 4);
}

static void add_files(const char *root, const char *rel, GPtrArray *out)
{
    char *path = rel ? g_build_filename(root, rel, NULL) : g_strdup(root);
    GDir *d = g_dir_open(path, 0, NULL);
    for (const char *e; d && (e = g_dir_read_name(d));) {
        char *r = rel ? g_strconcat(rel, "/", e, NULL) : g_strdup(e);
        char *full = g_build_filename(root, r, NULL);
        if (g_file_test(full, G_FILE_TEST_IS_SYMLINK))
            g_free(r);
        else if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
            add_files(root, r, out);
            g_free(r);
        } else
            g_ptr_array_add(out, r);
        g_free(full);
    }
    if (d)
        g_dir_close(d);
    g_free(path);
}

/* every file under dir, deflated, names relative to dir with '/' */
bool mc_zip_write_dir(const char *dir, const char *zip, char **err)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    add_files(dir, NULL, files);
    g_ptr_array_sort_values(files, (GCompareFunc)g_strcmp0);
    char *tmp = g_strconcat(zip, ".part", NULL);
    FILE *f = g_fopen(tmp, "wb");
    if (!f) {
        *err = g_strdup_printf("Can't write %s", zip);
        g_free(tmp);
        g_ptr_array_unref(files);
        return false;
    }
    GArray *ents = g_array_new(FALSE, TRUE, sizeof(went_t));
    guint32 off = 0;
    bool ok = true;
    for (guint i = 0; i < files->len && ok; i++) {
        const char *rel = files->pdata[i];
        char *full = g_build_filename(dir, rel, NULL), *data = NULL;
        gsize len = 0;
        ok = g_file_get_contents(full, &data, &len, NULL);
        g_free(full);
        if (!ok)
            break;
        went_t w = {g_strdup(rel), (guint32)crc32(0, (const Bytef *)data, len), 0, (guint32)len, off, 8};
        /* raw deflate (no zlib header), as zip wants */
        z_stream zs = {0};
        deflateInit2(&zs, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        uLong cap = deflateBound(&zs, len);
        guint8 *cbuf = g_malloc(cap);
        zs.next_in = (Bytef *)data;
        zs.avail_in = len;
        zs.next_out = cbuf;
        zs.avail_out = cap;
        deflate(&zs, Z_FINISH);
        w.csize = zs.total_out;
        deflateEnd(&zs);
        const guint8 *body = cbuf;
        if (w.csize >= len) {            /* doesn't shrink: store it */
            w.method = 0;
            w.csize = len;
            body = (const guint8 *)data;
        }
        GByteArray *h = g_byte_array_new();
        put32(h, 0x04034b50); put16(h, 20); put16(h, 0x0800); put16(h, w.method);
        put16(h, 0); put16(h, 0x21);     /* 1980-01-01 */
        put32(h, w.crc); put32(h, w.csize); put32(h, w.usize);
        put16(h, strlen(rel)); put16(h, 0);
        g_byte_array_append(h, (const guint8 *)rel, strlen(rel));
        ok = fwrite(h->data, 1, h->len, f) == h->len && fwrite(body, 1, w.csize, f) == w.csize;
        off += h->len + w.csize;
        g_byte_array_unref(h);
        g_free(cbuf);
        g_free(data);
        g_array_append_val(ents, w);
    }
    GByteArray *cd = g_byte_array_new();
    for (guint i = 0; i < ents->len; i++) {
        went_t *w = &g_array_index(ents, went_t, i);
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0x0800); put16(cd, w->method);
        put16(cd, 0); put16(cd, 0x21);
        put32(cd, w->crc); put32(cd, w->csize); put32(cd, w->usize);
        put16(cd, strlen(w->name)); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put32(cd, 0); put32(cd, w->offset);
        g_byte_array_append(cd, (const guint8 *)w->name, strlen(w->name));
        g_free(w->name);
    }
    put32(cd, 0x06054b50); put16(cd, 0); put16(cd, 0); put16(cd, ents->len); put16(cd, ents->len);
    put32(cd, cd->len - 0); put32(cd, off); put16(cd, 0);
    /* the size field above counted itself in: fix it to the directory alone */
    guint32 cdlen = cd->len - 22;
    cd->data[cd->len - 10] = cdlen & 255; cd->data[cd->len - 9] = cdlen >> 8 & 255;
    cd->data[cd->len - 8] = cdlen >> 16 & 255; cd->data[cd->len - 7] = cdlen >> 24;
    ok = ok && fwrite(cd->data, 1, cd->len, f) == cd->len;
    ok = fclose(f) == 0 && ok;
    g_byte_array_unref(cd);
    g_array_unref(ents);
    g_ptr_array_unref(files);
    if (ok)
        ok = g_rename(tmp, zip) == 0;
    if (!ok) {
        g_remove(tmp);
        *err = g_strdup_printf("Couldn't write %s", zip);
    }
    g_free(tmp);
    return ok;
}
