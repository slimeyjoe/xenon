/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The game profile: what makes the engine run one particular game. It is the pack's "profile"
 * entry (tools/mkpack.py writes it from games/<game>/profile.txt), key=value lines in UTF-8,
 * parsed by lrvne (lrvne/profile.h): a key the table below does not have is an error, and so
 * is a required key that is missing. docs/profile.md describes the keys. */
#include <stdlib.h>
#include <string.h>
#include "xe.h"

#define P struct xe_profile
static const struct lrv_pfield fields[] = {
    LRV_PROFILE_BASE_FIELDS(P, base),
    LRV_PF_STR("title_image", P, title_image),
    LRV_PF_INTS("title_rows", P, title_rows, ntitle_rows, 0, XE_SCREEN_H - 1),
    LRV_PF_INTS("title_bar", P, title_bar, ntitle_bar, 0, XE_SCREEN_H),
    LRV_PF_STR("start", P, start),
    LRV_PF_STR("start_if_clear", P, start_if_clear),
    LRV_PF_STR("load_script", P, load_script),
    LRV_PF_STR("omake_image", P, omake_image),
    LRV_PF_STR("omake_scripts", P, omake_scripts),
    LRV_PF_INT("omake_unlock", P, omake_unlock, 0, 255),
    LRV_PF_STR("se_bank", P, se_bank),
    LRV_PF_INT("music_buffer", P, music_buffer, 256, 65535),
    LRV_PF_BOOL("music_keep_after_fade", P, music_keep_after_fade),
    LRV_PF_INT("voice_music_volume", P, voice_music_volume, 1, 100)
};
#undef P

static int bad(const char *what)
{
    lrv_log(xe.hal, LRV_LOG_ERROR, "profile: %s", what);
    return LRV_ERR_FORMAT;
}

/* copies the next element of a comma-separated list (spaces around it dropped) into out;
 * returns the rest after it, NULL at the end of the list, or "" (and out empty) when the
 * element does not fit */
static const char *next_item(const char *p, char *out, size_t cap)
{
    size_t n;
    while (*p == ' ')
        p++;
    if (!*p)
        return 0;
    n = strcspn(p, ",");
    out[0] = 0;
    if (n > 0 && n < cap) {
        memcpy(out, p, n);
        while (n && out[n - 1] == ' ')
            n--;
        out[n] = 0;
    }
    p += strcspn(p, ",");
    return *p ? p + 1 : p;
}

/* start_if_clear: MASK:SCRIPT,... (the first rule none of whose progress bits are set) */
static int parse_rules(struct xe_profile *pr)
{
    const char *p = pr->start_if_clear;
    char item[48];
    while ((p = next_item(p, item, sizeof item)) != 0) {
        char *colon = strchr(item, ':'), *end;
        unsigned long mask;
        if (pr->nrules == XE_RULES_MAX || !colon)
            return bad("start_if_clear: MASK:SCRIPT,... (at most 8)");
        *colon = 0;
        mask = strtoul(item, &end, 0);
        if (end == item || *end || mask == 0 || mask > 255 || !colon[1] ||
            strlen(colon + 1) >= XE_NAME_MAX)
            return bad("start_if_clear: MASK:SCRIPT,... (a mask of progress bits 1..0xFF)");
        pr->rule[pr->nrules].mask = (unsigned)mask;
        strcpy(pr->rule[pr->nrules].script, colon + 1);
        pr->nrules++;
    }
    return LRV_OK;
}

static int parse_omake(struct xe_profile *pr)
{
    const char *p = pr->omake_scripts;
    char item[XE_NAME_MAX];
    while ((p = next_item(p, item, sizeof item)) != 0) {
        if (pr->nomake == XE_TITLE_ROWS || !item[0])
            return bad("omake_scripts: 1 to 3 script names");
        strcpy(pr->omake[pr->nomake++], item);
    }
    return pr->nomake ? LRV_OK : bad("omake_scripts: 1 to 3 script names");
}

int profile_load(void)
{
    struct xe_profile *p = &xe.profile;
    int rc;
    memset(p, 0, sizeof *p);
    rc = lrv_profile_load(&xe.pack, fields, (int)(sizeof fields / sizeof fields[0]), p);
    if (rc == LRV_ERR_NOTFOUND)
        lrv_log(xe.hal, LRV_LOG_ERROR, "pack: no game profile");
    if (rc != LRV_OK)
        return rc;
    if (!p->base.id[0] || !p->base.lang[0] || !p->title_image[0] || !p->start[0] ||
        !p->load_script[0] || !p->omake_image[0] || !p->music_buffer)
        return bad("id, lang, title_image, title_rows, title_bar, start, load_script, "
                   "omake_image, omake_scripts and music_buffer are required");
    if (p->ntitle_rows != XE_TITLE_ROWS)
        return bad("title_rows: the top lines of the three bars of the title menu");
    if (p->ntitle_bar != 3 || p->title_bar[0] + p->title_bar[1] > XE_STRIDE ||
        p->title_rows[0] + p->title_bar[2] > XE_SCREEN_H ||
        p->title_rows[2] + p->title_bar[2] > XE_SCREEN_H)
        return bad("title_bar: column, width in bytes and lines of a bar, on the screen");
    if (parse_rules(p) != LRV_OK || parse_omake(p) != LRV_OK)
        return LRV_ERR_FORMAT;
    if (!p->base.title[0])
        strcpy(p->base.title, p->base.id);
    if (!p->voice_music_volume)
        p->voice_music_volume = 100;
    return LRV_OK;
}
