/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A HAL for the test programs (after lrvne's tests/test_hal.h): a pack read from a file,
 * malloc, warnings on stderr, and a machine that never waits and has no player. A test that
 * needs more replaces those members. */
#ifndef XE_TESTS_TEST_HAL_H
#define XE_TESTS_TEST_HAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lrvne/hal.h"

static FILE *th_pak;
static uint32_t th_size;

static void *th_pack_open(int which, uint32_t *size)
{
    if (which != LRV_PACK_MAIN || !th_pak)
        return 0;
    *size = th_size;
    return th_pak;
}

static size_t th_pack_read(void *h, uint32_t off, void *dst, size_t n)
{
    if (fseek((FILE *)h, (long)off, SEEK_SET) != 0)
        return 0;
    return fread(dst, 1, n, (FILE *)h);
}

static void th_pack_close(void *h) { (void)h; }
static void *th_alloc(size_t n, unsigned flags) { (void)flags; return malloc(n ? n : 1); }
static void th_free(void *p, size_t n) { (void)n; free(p); }

static void th_log(int level, const char *msg)
{
    if (level >= LRV_LOG_WARN)
        fprintf(stderr, "%s\n", msg);
}

static void th_fatal(const char *msg)
{
    fprintf(stderr, "fatal: %s\n", msg);
    exit(1);
}

static int th_video_open(const struct lrv_video_cfg *cfg) { (void)cfg; return 0; }
static void th_video_close(void) {}
static void th_video_present(const struct lrv_frame *frame) { (void)frame; }
static void th_frame_wait(void) {}
static void th_input_poll(struct lrv_input *in) { memset(in, 0, sizeof *in); }
static void th_input_flush(void) {}

/* fills *hal; with a pack file name, opens it (as LRV_PACK_MAIN): returns 0, or -1 when it
 * cannot be opened */
static int th_hal(struct lrv_hal *hal, const char *pak)
{
    memset(hal, 0, sizeof *hal);
    hal->video_open = th_video_open;
    hal->video_close = th_video_close;
    hal->video_present = th_video_present;
    hal->frame_wait = th_frame_wait;
    hal->input_poll = th_input_poll;
    hal->input_flush = th_input_flush;
    hal->pack_open = th_pack_open;
    hal->pack_read = th_pack_read;
    hal->pack_close = th_pack_close;
    hal->mem_alloc = th_alloc;
    hal->mem_free = th_free;
    hal->log = th_log;
    hal->fatal = th_fatal;
    if (!pak)
        return 0;
    th_pak = fopen(pak, "rb");
    if (!th_pak) {
        perror(pak);
        return -1;
    }
    fseek(th_pak, 0, SEEK_END);
    th_size = (uint32_t)ftell(th_pak);
    return 0;
}

#endif
