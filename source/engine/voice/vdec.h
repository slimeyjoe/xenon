/* Voice clip decoders (docs/voice.md §3). A decoder streams one clip from a pack entry in
 * small reads, so no clip is ever held whole in memory. */
#ifndef XE_VOICE_VDEC_H
#define XE_VOICE_VDEC_H

#include <stdint.h>
#include "pack/pack.h"
#include "xe_hal.h"

/* Read-ahead over one clip: the decoders' small reads are served from a window of the entry,
 * refilled with one large pack read. On a slow file system each pack read costs a seek (FFS:
 * milliseconds even for a short one, backends/amiga/README.md "Pack on FFS"), so the window
 * keeps the reads to about one per second of ldp6 audio. */
#define VDEC_READAHEAD 16384
struct vdec_in {
    const struct xpak *pk;
    const struct xpak_entry *e;
    uint8_t *buf;               /* VDEC_READAHEAD bytes, allocated through the pack's HAL */
    const struct xe_hal *hal;   /* buf's allocator */
    uint32_t off, len;          /* entry offset of buf[0], valid bytes */
};
/* Like xpak_read on in->e: bytes [off, off + n) of the clip; returns the count read. */
size_t vdec_in_read(struct vdec_in *in, uint32_t off, void *dst, size_t n);

/* ldp4 / ldp6, the Windows release's own format (docs/re/other-versions.md §2.4). */
struct vdec_ldp {
    struct vdec_in *in;
    const struct xpak_entry *e;
    uint32_t pos;               /* next byte of the entry to read */
    uint32_t left;              /* samples of the clip still to produce */
    uint32_t blk_bytes;         /* bytes of the current block not yet read into buf */
    uint32_t blk_samples;       /* samples of the current block still to produce */
    int bits;                   /* code width: 4 or 6 */
    const int16_t *table;
    uint32_t bitbuf;
    int nbits;
    uint8_t buf[256];
    int buf_n, buf_i;
    uint16_t acc;
    uint32_t pend;              /* samples of the current code still to output */
    uint32_t rate;
};

/* Open the clip of `in`. Returns 0, or -1 if the entry is not an ldp clip. */
int vdec_ldp_open(struct vdec_ldp *d, struct vdec_in *in);
/* Decode up to n samples. Returns the number produced; 0 at the end of the clip. */
int vdec_ldp_read(struct vdec_ldp *d, int16_t *out, int n);

#ifdef XE_HAVE_OPUS
/* Opus packets in the layout of source/tools/mkvoice.py ("opus", rate, count, pre-skip, then
 * u16 BE length + packet), decoded by the vendored fixed-point libopus (third_party/opus). */
#define VDEC_OPUS_FRAME 2880            /* the longest Opus frame (120 ms) at 24 kHz */
struct vdec_opus {
    struct vdec_in *in;
    const struct xpak_entry *e;
    uint32_t pos;
    uint32_t left;              /* samples of the clip still to produce */
    uint32_t skip;              /* pre-skip samples still to drop */
    uint32_t rate;
    int pcm_n, pcm_i;           /* decoded samples in pcm[], next one to output */
    const struct xe_hal *hal;   /* the decoder state's allocator */
    void *od;                   /* OpusDecoder, allocated on the first open, kept until close */
    uint32_t od_size;
    uint32_t od_rate;           /* the rate od was set up for */
    int16_t pcm[VDEC_OPUS_FRAME];
    uint8_t pkt[1275];
};
int vdec_opus_open(struct vdec_opus *d, struct vdec_in *in);
int vdec_opus_read(struct vdec_opus *d, int16_t *out, int n);
void vdec_opus_close(struct vdec_opus *d);
#endif

/* Any clip: the decoder is chosen by the entry's magic. */
struct vdec {
    int kind;                   /* VDEC_NONE / VDEC_LDP / VDEC_OPUS */
    uint32_t rate;              /* the clip's sample rate */
    struct vdec_in in;
    struct vdec_ldp ldp;
#ifdef XE_HAVE_OPUS
    struct vdec_opus opus;
#endif
};
enum { VDEC_NONE, VDEC_LDP, VDEC_OPUS };

/* 0, or -1 if the clip's format is unknown or not built in */
int vdec_open(struct vdec *d, const struct xpak *pk, const struct xpak_entry *e);
int vdec_read(struct vdec *d, int16_t *out, int n);
void vdec_close(struct vdec *d);        /* frees what the decoders keep between clips */
/* 1 if this build decodes the voice pack's codec (its "voice" meta codec=) */
int vdec_supports(const char *codec);

#endif
