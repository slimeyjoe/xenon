/* Byte-order helpers: every file format the engine reads is little-endian (PC-98 origin);
 * the engine runs on big-endian 68k as well, so all multi-byte fields go through these. */
#ifndef XE_UTIL_ENDIAN_H
#define XE_UTIL_ENDIAN_H

#include <stdint.h>

static inline uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#endif
