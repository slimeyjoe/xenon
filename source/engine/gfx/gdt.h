/* GDT ("DA1") image decoder - docs/re/graphics.md §2.
 *
 * Port of the hand-written decoder in AGS.EXE segment 0456 (entry 0456:0004) via the verified
 * Python reference re-tools/gdt.py. Decodes a DA1 blob column by column into four plane buffers,
 * leaving every byte the stream does not write untouched (this is how diff images work). */
#ifndef XE_GFX_GDT_H
#define XE_GFX_GDT_H

#include <stddef.h>
#include <stdint.h>
#include "gfx/planes.h"

#define GDT_FLAG_PALETTE 0x8000u    /* 24-byte palette follows the header */
#define GDT_FLAG_CODEC1  0x4000u    /* 1-byte column codec (else 2-byte column-pair codec) */

struct gdt_header {
    uint8_t  xb;            /* x in bytes (8-pixel units) */
    uint8_t  wb;            /* width in bytes */
    uint16_t y;             /* top line */
    uint16_t h;             /* height in lines (display rect only; the decoder ignores it) */
    uint16_t flags;
    uint8_t  rgb4[48];      /* unpacked palette, 16 x {R,G,B} 0..15; valid if flags & PALETTE */
    uint32_t plane_off[4];  /* stream offsets from the blob start, plane order B,R,G,I */
    uint16_t plane_size[4];
    uint32_t end;           /* offset after the last stream */
};

/* Parse the header; returns 0 on success, -1 if the blob is not a DA1 image or is truncated. */
int gdt_parse(const uint8_t *data, size_t len, struct gdt_header *h);

/* Unpack the 24-byte packed palette (engine routine 0C2A:0514) into 16 x {R,G,B} 4-bit. */
void gdt_unpack_palette(const uint8_t raw[24], uint8_t rgb4[48]);

/* Decode into dst. `written`, if not NULL, receives the bounding box (byte columns x lines)
 * of every byte written, which may be smaller than the header rectangle (ADT frames).
 * Returns 0 on success, -1 if a stream ran past the blob (data corruption). The decoder
 * trusts the plane offsets of the data: all shipped files stay inside [0, 32000). */
int gdt_decode(const uint8_t *data, size_t len, const struct gdt_header *h,
               struct xe_planes *dst, struct xe_rect *written);

#endif
