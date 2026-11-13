/* Opus voice decoder (vdec.h): the clip layout of source/tools/mkvoice.py, decoded by the
 * vendored libopus (third_party/opus: decoder only, fixed point). Built with XE_HAVE_OPUS.
 *
 *   "opus"; u32 BE rate; u32 BE sample count (after the pre-skip); u16 BE pre-skip; u16 0;
 *   then packets: u16 BE byte count + one Opus packet (RFC 6716), to the end of the entry.
 *
 * One packet is decoded at a time into pcm[]; the decoder state is allocated through the pack's
 * HAL on the first open and reused for every clip (opus_decoder_init resets it). */
#ifdef XE_HAVE_OPUS
#include <string.h>
#include <opus.h>
#include "voice/vdec.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int vdec_opus_open(struct vdec_opus *d, struct vdec_in *in)
{
    const struct xpak_entry *e = in->e;
    uint8_t h[16];
    uint32_t rate;
    int size;

    if (e->size < 16 || vdec_in_read(in, 0, h, 16) != 16 || memcmp(h, "opus", 4) != 0)
        return -1;
    rate = be32(h + 4);
    if (rate != 8000 && rate != 12000 && rate != 16000 && rate != 24000 && rate != 48000)
        return -1;
    if (rate > 24000)
        return -1;                          /* pcm[] holds 120 ms at 24 kHz */
    if (!d->od) {
        size = opus_decoder_get_size(1);
        if (size <= 0)
            return -1;
        d->hal = in->pk->hal;
        d->od = d->hal->mem_alloc((size_t)size, 0);
        if (!d->od)
            return -1;
        d->od_size = (uint32_t)size;
        d->od_rate = 0;
    }
    if (d->od_rate != rate) {
        if (opus_decoder_init((OpusDecoder *)d->od, (opus_int32)rate, 1) != OPUS_OK)
            return -1;
        d->od_rate = rate;
    } else if (opus_decoder_ctl((OpusDecoder *)d->od, OPUS_RESET_STATE) != OPUS_OK)
        return -1;
    d->in = in;
    d->e = e;
    d->rate = rate;
    d->left = be32(h + 8);
    d->skip = ((uint32_t)h[12] << 8) | h[13];
    d->pos = 16;
    d->pcm_n = d->pcm_i = 0;
    return 0;
}

/* decode the next packet into pcm[]; 0 at the end of the entry or on a bad packet */
static int next_packet(struct vdec_opus *d)
{
    uint8_t h[2];
    uint32_t len;
    int n;

    if (d->pos + 2 > d->e->size || vdec_in_read(d->in, d->pos, h, 2) != 2)
        return 0;
    len = ((uint32_t)h[0] << 8) | h[1];
    if (len > sizeof d->pkt || d->pos + 2 + len > d->e->size)
        return 0;
    if (len && vdec_in_read(d->in, d->pos + 2, d->pkt, len) != len)
        return 0;
    d->pos += 2 + len;
    n = opus_decode((OpusDecoder *)d->od, d->pkt, (opus_int32)len, d->pcm, VDEC_OPUS_FRAME, 0);
    if (n < 0)
        return 0;
    d->pcm_n = n;
    d->pcm_i = 0;
    if (d->skip) {
        uint32_t k = d->skip < (uint32_t)n ? d->skip : (uint32_t)n;
        d->pcm_i = (int)k;
        d->skip -= k;
    }
    return 1;
}

int vdec_opus_read(struct vdec_opus *d, int16_t *out, int n)
{
    int k = 0;
    while (k < n && d->left) {
        uint32_t m;
        if (d->pcm_i >= d->pcm_n) {
            if (!next_packet(d)) {
                d->left = 0;
                break;
            }
            continue;
        }
        m = (uint32_t)(d->pcm_n - d->pcm_i);
        if (m > (uint32_t)(n - k))
            m = (uint32_t)(n - k);
        if (m > d->left)
            m = d->left;
        memcpy(out + k, d->pcm + d->pcm_i, m * sizeof out[0]);
        d->pcm_i += (int)m;
        k += (int)m;
        d->left -= m;
    }
    return k;
}

void vdec_opus_close(struct vdec_opus *d)
{
    if (d->od)
        d->hal->mem_free(d->od, d->od_size);
    d->od = 0;
    d->od_rate = 0;
}
#else
typedef int vdec_opus_unused;               /* ISO C: no empty translation unit */
#endif
