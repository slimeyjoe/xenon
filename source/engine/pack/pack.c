#include "pack/pack.h"
#include "util/endian.h"
#include "xe_hal.h"

static int name_eq(const char *a, const char *b)
{
    /* case-insensitive compare of a NUL-padded pack name with a C string */
    int i;
    for (i = 0; i < XPAK_NAME_LEN; i++) {
        int ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
    }
    return b[XPAK_NAME_LEN] == 0;
}

static int read_header(struct xpak *pk, uint32_t base, uint32_t avail)
{
    uint8_t hdr[XPAK_HEADER_SIZE];
    if (avail < XPAK_HEADER_SIZE)
        return -1;
    if (pk->hal->pack_read(pk->src, base, hdr, XPAK_HEADER_SIZE) != XPAK_HEADER_SIZE)
        return -1;
    if (hdr[0] != 'X' || hdr[1] != 'P' || hdr[2] != 'A' || hdr[3] != 'K')
        return -1;
    if (rd16le(hdr + 4) != XPAK_VERSION)
        return -1;
    pk->count = rd16le(hdr + 6);
    pk->total = rd32le(hdr + 8);
    pk->base = base;
    return pk->total <= avail ? 0 : -1;
}

int xpak_open(struct xpak *pk, const struct xe_hal *hal, int which)
{
    uint32_t size, base = 0;
    uint8_t tr[XPKT_TRAILER_SIZE];
    size_t tbl;
    uint8_t *raw;
    uint16_t i;

    pk->hal = hal;
    pk->entries = 0;
    pk->count = 0;
    pk->lang = XPAK_L_ANY;
    pk->src = hal->pack_open(which, &size);
    if (!pk->src) {
        if (which == XE_PACK_MAIN)
            hal->log(XE_LOG_ERROR, "pack: no asset pack found");
        return -1;
    }
    /* A bundled pack ends with a trailer that says how long it is. */
    if (size >= XPKT_TRAILER_SIZE &&
        hal->pack_read(pk->src, size - XPKT_TRAILER_SIZE, tr, XPKT_TRAILER_SIZE) == XPKT_TRAILER_SIZE &&
        tr[0] == 'X' && tr[1] == 'P' && tr[2] == 'K' && tr[3] == 'T' &&
        tr[12] == 'X' && tr[13] == 'P' && tr[14] == 'K' && tr[15] == 'T') {
        uint32_t psize = rd32le(tr + 4);
        if ((rd32le(tr + 8) ^ psize) == 0xFFFFFFFFu && psize + XPKT_TRAILER_SIZE <= size)
            base = size - XPKT_TRAILER_SIZE - psize;
        else {
            hal->log(XE_LOG_ERROR, "pack: corrupt trailer");
            return -1;
        }
    }
    if (read_header(pk, base, size - base) != 0) {
        hal->log(XE_LOG_ERROR, "pack: bad header");
        return -1;
    }
    tbl = (size_t)pk->count * XPAK_ENTRY_SIZE;
    raw = hal->mem_alloc(tbl ? tbl : 1, XE_MEM_ANY);
    pk->entries = hal->mem_alloc(sizeof(*pk->entries) * (pk->count ? pk->count : 1), XE_MEM_ANY);
    if (!raw || !pk->entries) {
        hal->log(XE_LOG_ERROR, "pack: out of memory");
        return -1;
    }
    if (hal->pack_read(pk->src, base + XPAK_HEADER_SIZE, raw, tbl) != tbl) {
        hal->log(XE_LOG_ERROR, "pack: truncated entry table");
        hal->mem_free(raw, tbl ? tbl : 1);
        return -1;
    }
    for (i = 0; i < pk->count; i++) {
        const uint8_t *e = raw + (size_t)i * XPAK_ENTRY_SIZE;
        struct xpak_entry *o = &pk->entries[i];
        int k;
        for (k = 0; k < XPAK_NAME_LEN; k++)
            o->name[k] = (char)e[k];
        o->type = e[20];
        o->lang = e[21];
        o->offset = rd32le(e + 24);
        o->size = rd32le(e + 28);
    }
    hal->mem_free(raw, tbl ? tbl : 1);
    return 0;
}

void xpak_close(struct xpak *pk)
{
    if (pk->entries)
        pk->hal->mem_free(pk->entries, sizeof(*pk->entries) * (pk->count ? pk->count : 1));
    pk->entries = 0;
    if (pk->src)
        pk->hal->pack_close(pk->src);
    pk->src = 0;
}

static const struct xpak_entry *find_in(const struct xpak *pk, const char *name, int type, int lang)
{
    uint16_t i;
    /* Linear search: a few hundred entries, lookups happen at scene changes only. */
    for (i = 0; i < pk->count; i++) {
        const struct xpak_entry *e = &pk->entries[i];
        if (e->lang == lang && (type < 0 || e->type == type) && name_eq(e->name, name))
            return e;
    }
    return 0;
}

const struct xpak_entry *xpak_find(const struct xpak *pk, const char *name, int type)
{
    const struct xpak_entry *e = 0;
    if (pk->lang != XPAK_L_ANY)
        e = find_in(pk, name, type, pk->lang);
    if (!e)
        e = find_in(pk, name, type, XPAK_L_ANY);
    return e;
}

uint8_t *xpak_load(const struct xpak *pk, const struct xpak_entry *e, uint32_t *size)
{
    uint8_t *buf = pk->hal->mem_alloc(e->size ? e->size : 1, XE_MEM_ANY);
    if (!buf)
        return 0;
    if (pk->hal->pack_read(pk->src, pk->base + e->offset, buf, e->size) != e->size) {
        pk->hal->mem_free(buf, e->size ? e->size : 1);
        return 0;
    }
    *size = e->size;
    return buf;
}

void xpak_free(const struct xpak *pk, uint8_t *data, uint32_t size)
{
    pk->hal->mem_free(data, size ? size : 1);
}

size_t xpak_read(const struct xpak *pk, const struct xpak_entry *e, uint32_t off,
                 void *dst, size_t n)
{
    if (off >= e->size)
        return 0;
    if (n > e->size - off)
        n = e->size - off;
    return pk->hal->pack_read(pk->src, pk->base + e->offset + off, dst, n);
}

int xpak_lang_id(const char *name)
{
    static const char *const names[] = { "ja", "en", "pl" };
    static const int ids[] = { XPAK_L_JA, XPAK_L_EN, XPAK_L_PL };
    int k;
    for (k = 0; k < 3; k++) {
        const char *a = name, *b = names[k];
        while (*a && *b && (*a | 0x20) == *b) {
            a++;
            b++;
        }
        if (!*a && !*b)
            return ids[k];
    }
    return 0;
}
