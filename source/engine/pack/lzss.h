/* Okumura LZSS decoder as used by the .CC scenario container - docs/re/scripts.md §1.
 * N = 4096, F = 18, THRESHOLD = 2, zero-filled ring (088D:0174). Used only in "dev mode"
 * when original .CC files are loaded directly; the pack stores scripts decompressed. */
#ifndef XE_PACK_LZSS_H
#define XE_PACK_LZSS_H

#include <stddef.h>
#include <stdint.h>

#define LCZ_HEADER_SIZE 0x18

/* Decode a raw LZSS stream `src` of `srclen` bytes into dst (capacity dstcap).
 * Returns the number of bytes produced (output stops at dstcap, as the engine's buffer is
 * exactly the header size). */
size_t lzss_decode(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap);

/* Decompressed size announced by an LCZ container (low 16 bits are what the engine uses). */
uint32_t lcz_size(const uint8_t *raw, size_t len);

#endif
