/* Voice clip decoders: the dispatcher (vdec.h). */
#include <string.h>
#include "voice/vdec.h"

size_t vdec_in_read(struct vdec_in *in, uint32_t off, void *dst, size_t n)
{
    uint8_t *out = dst;
    size_t done = 0;
    if (!in->buf)                           /* no window (out of memory): read directly */
        return xpak_read(in->pk, in->e, off, dst, n);
    while (done < n) {
        uint32_t at = off + (uint32_t)done, m;
        if (at < in->off || at >= in->off + in->len) {
            uint32_t want;
            if (at >= in->e->size)
                break;
            want = in->e->size - at;
            if (want > VDEC_READAHEAD)
                want = VDEC_READAHEAD;
            in->off = at;
            in->len = (uint32_t)xpak_read(in->pk, in->e, at, in->buf, want);
            if (in->len == 0)
                break;
        }
        m = in->off + in->len - at;
        if (m > n - done)
            m = (uint32_t)(n - done);
        memcpy(out + done, in->buf + (at - in->off), m);
        done += m;
    }
    return done;
}

int vdec_supports(const char *codec)
{
    if (!strcmp(codec, "ldp6") || !strcmp(codec, "ldp4"))
        return 1;
#ifdef XE_HAVE_OPUS
    if (!strcmp(codec, "opus"))
        return 1;
#endif
    return 0;
}

int vdec_open(struct vdec *d, const struct xpak *pk, const struct xpak_entry *e)
{
    uint8_t m[4];
    d->kind = VDEC_NONE;
    d->rate = 0;
    if (!d->in.buf) {
        d->in.hal = pk->hal;
        d->in.buf = d->in.hal->mem_alloc(VDEC_READAHEAD, 0);
    }
    d->in.pk = pk;
    d->in.e = e;
    d->in.off = d->in.len = 0;              /* a new clip: nothing of it read yet */
    if (e->size < 4 || vdec_in_read(&d->in, 0, m, 4) != 4)
        return -1;
    if (!memcmp(m, "ldp", 3)) {
        if (vdec_ldp_open(&d->ldp, &d->in) != 0)
            return -1;
        d->kind = VDEC_LDP;
        d->rate = d->ldp.rate;
        return 0;
    }
#ifdef XE_HAVE_OPUS
    if (!memcmp(m, "opus", 4)) {
        if (vdec_opus_open(&d->opus, &d->in) != 0)
            return -1;
        d->kind = VDEC_OPUS;
        d->rate = d->opus.rate;
        return 0;
    }
#endif
    return -1;
}

int vdec_read(struct vdec *d, int16_t *out, int n)
{
    switch (d->kind) {
    case VDEC_LDP:
        return vdec_ldp_read(&d->ldp, out, n);
#ifdef XE_HAVE_OPUS
    case VDEC_OPUS:
        return vdec_opus_read(&d->opus, out, n);
#endif
    default:
        return 0;
    }
}

void vdec_close(struct vdec *d)
{
    if (d->in.buf)
        d->in.hal->mem_free(d->in.buf, VDEC_READAHEAD);
    d->in.buf = 0;
#ifdef XE_HAVE_OPUS
    vdec_opus_close(&d->opus);
#endif
    d->kind = VDEC_NONE;
}
