#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xe_hal.h"
#include "autoplay.h"

static const struct { const char *name; uint32_t bit; } g_keys[] = {
    { "ok", XE_IN_OK }, { "cancel", XE_IN_CANCEL }, { "up", XE_IN_UP }, { "down", XE_IN_DOWN },
    { "left", XE_IN_LEFT }, { "right", XE_IN_RIGHT }, { "help", XE_IN_HELP },
    { "f1", XE_IN_F1 }, { "f2", XE_IN_F2 }, { "f3", XE_IN_F3 }, { "f4", XE_IN_F4 },
    { "f5", XE_IN_F5 }, { "other", XE_IN_OTHER }, { "quit", XE_IN_QUIT },
    { "emergency", XE_IN_EMERGENCY }, { "mouse_l", XE_IN_MOUSE_L }, { "mouse_r", XE_IN_MOUSE_R },
    { 0, 0 }
};

static void seterr(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0) {
        strncpy(err, msg, (size_t)errlen - 1);
        err[errlen - 1] = 0;
    }
}

static int is_blank(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* grow *arr (element size sz) so that it holds n+1 elements; capacities 16, 32, 64, ... */
static int grow(void **arr, int n, size_t sz)
{
    if (n == 0 || (n >= 16 && (n & (n - 1)) == 0)) {
        void *p = realloc(*arr, (size_t)(n ? 2 * n : 16) * sz);
        if (!p)
            return -1;
        *arr = p;
    }
    return 0;
}

static int cmp_at(const void *a, const void *b)
{
    const struct autoplay_at *x = a, *y = b;
    return x->tick < y->tick ? -1 : x->tick > y->tick;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* one key name at *pp (ends at '+', ',' or a blank) */
static int parse_key(const char **pp, uint32_t *bit)
{
    const char *p = *pp;
    size_t n = 0;
    int i;
    while (p[n] && p[n] != '+' && p[n] != ',' && !is_blank((unsigned char)p[n]))
        n++;
    for (i = 0; g_keys[i].name; i++)
        if (strlen(g_keys[i].name) == n && !strncmp(g_keys[i].name, p, n)) {
            *bit = g_keys[i].bit;
            *pp = p + n;
            return 0;
        }
    return -1;
}

static int parse_spec(struct autoplay *ap, const char *s, char *err, int errlen)
{
    while (*s) {
        char *end;
        while (is_blank((unsigned char)*s))
            s++;
        if (!*s)
            break;
        if (!strncmp(s, "ok:", 3)) ap->ok_every = atoi(s + 3);
        else if (!strncmp(s, "cancel:", 7)) ap->cancel_every = atoi(s + 7);
        else if (!strncmp(s, "down:", 5)) ap->down_every = atoi(s + 5);
        else if (!strncmp(s, "rand:", 5)) ap->rnd = (unsigned)atoi(s + 5) | 1u;
        else if (!strncmp(s, "at:", 3)) {
            const char *p = s + 3;
            while (*p && !is_blank((unsigned char)*p)) {
                uint32_t t = (uint32_t)strtoul(p, &end, 10), keys = 0, bit;
                if (*end != '=') {
                    seterr(err, errlen, "autoplay: expected T=KEY");
                    return -1;
                }
                p = end + 1;
                for (;;) {
                    if (parse_key(&p, &bit) != 0) {
                        seterr(err, errlen, "autoplay: unknown key");
                        return -1;
                    }
                    keys |= bit;
                    if (*p != '+')
                        break;
                    p++;
                }
                if (grow((void **)&ap->at, ap->at_count, sizeof *ap->at) != 0) {
                    seterr(err, errlen, "autoplay: out of memory");
                    return -1;
                }
                ap->at[ap->at_count].tick = t;
                ap->at[ap->at_count].keys = keys;
                ap->at_count++;
                if (*p == ',')
                    p++;
            }
            s = p;
            continue;
        } else if (!strncmp(s, "ctrl:", 5) || !strncmp(s, "instant:", 8) || !strncmp(s, "anykey:", 7)) {
            int what = s[0] == 'c' ? AP_CTRL : s[0] == 'i' ? AP_INSTANT : AP_ANYKEY;
            const char *p = strchr(s, ':') + 1;
            while (*p && !is_blank((unsigned char)*p)) {
                uint32_t t0 = (uint32_t)strtoul(p, &end, 10), t1;
                if (*end != '-') {
                    seterr(err, errlen, "autoplay: expected T0-T1");
                    return -1;
                }
                t1 = (uint32_t)strtoul(end + 1, &end, 10);
                if (grow((void **)&ap->rng, ap->rng_count, sizeof *ap->rng) != 0) {
                    seterr(err, errlen, "autoplay: out of memory");
                    return -1;
                }
                ap->rng[ap->rng_count].t0 = t0;
                ap->rng[ap->rng_count].t1 = t1;
                ap->rng[ap->rng_count].what = what;
                ap->rng_count++;
                p = end;
                if (*p == ',')
                    p++;
            }
            s = p;
            continue;
        } else if (!strncmp(s, "snap:", 5)) {
            const char *p = s + 5;
            while (*p && !is_blank((unsigned char)*p)) {
                uint32_t t = (uint32_t)strtoul(p, &end, 10);
                if (end == p) {
                    seterr(err, errlen, "autoplay: expected a snap tick");
                    return -1;
                }
                if (grow((void **)&ap->snap, ap->snap_count, sizeof *ap->snap) != 0) {
                    seterr(err, errlen, "autoplay: out of memory");
                    return -1;
                }
                ap->snap[ap->snap_count++] = t;
                p = end;
                if (*p == ',')
                    p++;
            }
            s = p;
            continue;
        } else {
            seterr(err, errlen, "autoplay: unknown word");
            return -1;
        }
        while (*s && !is_blank((unsigned char)*s))
            s++;
    }
    return 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf = 0;
    size_t len = 0, cap = 0, n;
    if (!f)
        return 0;
    for (;;) {
        if (cap - len < 4096) {
            char *nb = realloc(buf, cap + 65536);
            if (!nb) {
                free(buf);
                fclose(f);
                return 0;
            }
            buf = nb;
            cap += 65536;
        }
        n = fread(buf + len, 1, cap - len - 1, f);
        if (n == 0)
            break;
        len += n;
    }
    fclose(f);
    buf[len] = 0;
    return buf;
}

int autoplay_parse(struct autoplay *ap, const char *spec, char *err, int errlen)
{
    int rc;
    memset(ap, 0, sizeof *ap);
    if (spec[0] == '@') {
        char *text = read_file(spec + 1);
        if (!text) {
            seterr(err, errlen, "autoplay: cannot read the spec file");
            return -1;
        }
        rc = parse_spec(ap, text, err, errlen);
        free(text);
    } else {
        rc = parse_spec(ap, spec, err, errlen);
    }
    if (rc != 0) {
        autoplay_free(ap);
        return rc;
    }
    if (ap->at_count)
        qsort(ap->at, (size_t)ap->at_count, sizeof *ap->at, cmp_at);
    if (ap->snap_count)
        qsort(ap->snap, (size_t)ap->snap_count, sizeof *ap->snap, cmp_u32);
    ap->rnd_state = ap->rnd;
    return 0;
}

void autoplay_free(struct autoplay *ap)
{
    free(ap->at);
    free(ap->rng);
    free(ap->snap);
    ap->at = 0;
    ap->rng = 0;
    ap->snap = 0;
    ap->at_count = ap->rng_count = ap->snap_count = 0;
}

void autoplay_poll(struct autoplay *ap, uint32_t tick, struct xe_input *in)
{
    uint32_t pressed = ap->kept;
    int i, first = !ap->polled || tick != ap->last_tick;
    ap->kept = 0;
    ap->polled = 1;
    ap->last_tick = tick;
    if (ap->rnd) {
        if (first && tick % 20 == 0) {
            static const uint32_t cmds[] = { XE_IN_OK, XE_IN_OK, XE_IN_OK, XE_IN_DOWN, XE_IN_UP,
                                             XE_IN_CANCEL };
            ap->rnd_state = ap->rnd_state * 1103515245u + 12345u;
            pressed |= cmds[(ap->rnd_state >> 16) % 6];
        }
    } else if (ap->at_count || ap->rng_count) {
        while (ap->at_next < ap->at_count && ap->at[ap->at_next].tick <= tick)
            pressed |= ap->at[ap->at_next++].keys;
    } else if (first) {
        if (ap->ok_every && tick % (uint32_t)ap->ok_every == 0) pressed |= XE_IN_OK;
        if (ap->cancel_every && tick % (uint32_t)ap->cancel_every == 0) pressed |= XE_IN_CANCEL;
        if (ap->down_every && tick % (uint32_t)ap->down_every == 0) pressed |= XE_IN_DOWN;
    }
    in->pressed |= pressed;
    for (i = 0; i < ap->rng_count; i++) {
        const struct autoplay_range *r = &ap->rng[i];
        if (tick >= r->t0 && tick < r->t1) {
            if (r->what == AP_CTRL) in->ctrl_held = 1;
            else if (r->what == AP_INSTANT) in->instant_text = 1;
            else in->any_key_down = 1;
        }
    }
    if (pressed)
        in->any_key_down = 1;
}

void autoplay_flush(struct autoplay *ap, uint32_t tick)
{
    while (ap->at_next < ap->at_count && ap->at[ap->at_next].tick <= tick)
        ap->kept |= ap->at[ap->at_next++].keys & (XE_IN_QUIT | XE_IN_EMERGENCY);
}

int autoplay_snap_due(const struct autoplay *ap, uint32_t tick)
{
    int lo = 0, hi = ap->snap_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (ap->snap[mid] == tick)
            return 1;
        if (ap->snap[mid] < tick) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

uint32_t autoplay_last_tick(const struct autoplay *ap)
{
    uint32_t t = 0;
    int i;
    if (ap->at_count && ap->at[ap->at_count - 1].tick > t) t = ap->at[ap->at_count - 1].tick;
    if (ap->snap_count && ap->snap[ap->snap_count - 1] > t) t = ap->snap[ap->snap_count - 1];
    for (i = 0; i < ap->rng_count; i++)
        if (ap->rng[i].t1 > t) t = ap->rng[i].t1;
    return t;
}
