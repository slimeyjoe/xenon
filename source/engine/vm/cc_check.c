/* Static script checker: the grammar of scripts.md §3 as a recursive-descent parse over the
 * token stream, with byte coverage accounting (the C counterpart of re-tools/ccdis.py --check).
 * Used by the tests (every shipped script must parse with 0 unknown opcodes and 100 % byte
 * coverage) and available to the pack builder. */
#include <string.h>
#include "vm/cc.h"

struct chk {
    const uint8_t *d;
    uint32_t n;
    uint32_t pos;
    uint8_t *cov;              /* per-byte coverage counts (may be NULL) */
    struct cc_check_result *r;
    int failed;
};

static uint8_t at(const struct chk *c, uint32_t p)
{
    return p < c->n ? c->d[p] : 0;
}

static void fail(struct chk *c)
{
    if (!c->failed) {
        c->failed = 1;
        c->r->parse_error = 1 + (int)c->pos;
    }
}

static void mark(struct chk *c, uint32_t a, uint32_t b)
{
    uint32_t k;
    if (!c->cov)
        return;
    for (k = a; k < b && k < c->n; k++)
        c->cov[k]++;
}

/* token length as cc_skip_token */
static uint32_t toklen(const struct chk *c, uint32_t p)
{
    uint8_t b = at(c, p);
    if (b == CC_T_INT || b == CC_T_FLAG || b == CC_T_LABEL)
        return 3;
    if (b == CC_T_STR) {
        uint32_t q = p + 1;
        while (q < c->n && c->d[q] != 0)
            q++;
        return q + 1 - p;
    }
    return 1;
}

static int eof(const struct chk *c)
{
    return c->pos >= c->n;
}

static void take(struct chk *c)
{
    uint32_t l = toklen(c, c->pos);
    mark(c, c->pos, c->pos + l);
    c->pos += l;
}

static int expect(struct chk *c, uint8_t b)
{
    if (eof(c) || at(c, c->pos) != b) {
        fail(c);
        return 0;
    }
    take(c);
    return 1;
}

static int cond(struct chk *c)
{
    for (;;) {
        uint8_t b = at(c, c->pos);
        if (eof(c)) { fail(c); return 0; }
        if (b == CC_CONDEND) { take(c); return 1; }
        if (b == CC_AND) { take(c); continue; }
        if (b != CC_T_FLAG) { fail(c); return 0; }
        take(c);
        if (!expect(c, CC_EQ)) return 0;
        if (at(c, c->pos) != CC_T_INT) { fail(c); return 0; }
        take(c);
    }
}

static int block(struct chk *c, uint8_t stop1, uint8_t stop2);

static const struct cc_opinfo *op_lookup(uint8_t op)
{
    const struct cc_opinfo *o;
    for (o = cc_optable; o->op; o++)
        if (o->op == op)
            return o;
    return 0;
}

static int menu(struct chk *c)
{
    int nopts = 0;
    take(c);                                    /* 1B */
    if (!expect(c, CC_SEN)) return 0;
    for (;;) {
        uint8_t b = at(c, c->pos);
        if (eof(c)) { fail(c); return 0; }
        if (b == CC_SENEND) { take(c); break; }
        if (b == CC_IF) {
            take(c);
            if (!cond(c)) return 0;
        }
        if (at(c, c->pos) != CC_T_STR) { fail(c); return 0; }
        c->r->strings++;
        if (c->d[c->pos + 1] != toklen(c, c->pos) - 3)
            c->r->lenbyte_mismatch++;
        take(c);
        if (at(c, c->pos) != CC_T_LABEL) { fail(c); return 0; }
        take(c);
        nopts++;
    }
    if (nopts > CC_MAX_OPTIONS)
        c->r->warnings++;
    while (!eof(c) && at(c, c->pos) == CC_T_LABEL) {
        take(c);
        if (!block(c, CC_END, CC_END)) return 0;
        if (!expect(c, CC_END)) return 0;
    }
    return 1;
}

static int stmt(struct chk *c)
{
    uint8_t b = at(c, c->pos);
    const struct cc_opinfo *o;
    const char *s;

    if (b == CC_T_STR) {
        c->r->strings++;
        if (c->d[c->pos + 1] != toklen(c, c->pos) - 3)
            c->r->lenbyte_mismatch++;
        take(c);
        return 1;
    }
    if (b == CC_T_FLAG) {
        take(c);
        if (!expect(c, CC_EQ)) return 0;
        if (at(c, c->pos) != CC_T_INT) { fail(c); return 0; }
        take(c);
        return 1;
    }
    if (b == CC_T_INT || b == CC_T_LABEL) { fail(c); return 0; }
    if (b == CC_SENMARK)
        return menu(c);
    if (b == CC_IF) {
        take(c);
        if (!cond(c)) return 0;
        if (!expect(c, CC_THEN)) return 0;
        if (!block(c, CC_IFEND, CC_IFEND)) return 0;
        return expect(c, CC_IFEND);
    }
    o = op_lookup(b);
    if (!o || o->spec == 0) {
        c->r->unknown_ops++;
        take(c);
        return 1;
    }
    take(c);
    for (s = o->spec; *s; s++) {
        uint8_t y = at(c, c->pos);
        if (*s == 'i' || *s == 'o') {
            if (!eof(c) && y == CC_T_INT)
                take(c);
            else if (*s == 'i') { fail(c); return 0; }
        } else if (*s == 's') {
            if (eof(c) || y != CC_T_STR) { fail(c); return 0; }
            c->r->strings++;
            if (c->d[c->pos + 1] != toklen(c, c->pos) - 3)
                c->r->lenbyte_mismatch++;
            take(c);
        }
    }
    return 1;
}

static int block(struct chk *c, uint8_t stop1, uint8_t stop2)
{
    for (;;) {
        uint8_t b;
        if (eof(c)) { fail(c); return 0; }
        b = at(c, c->pos);
        if (b == stop1 || b == stop2)
            return 1;
        if (!stmt(c))
            return 0;
    }
}

int cc_check(const uint8_t *buf, uint32_t size, struct cc_check_result *r, uint8_t *coverage)
{
    struct chk c;
    uint32_t k;

    memset(r, 0, sizeof *r);
    c.d = buf;
    c.n = size;
    c.pos = 0;
    c.cov = coverage;
    c.r = r;
    c.failed = 0;
    if (coverage)
        memset(coverage, 0, size);

    /* junk before the first PAGE is never executed */
    while (!eof(&c) && at(&c, c.pos) != CC_PAGE) {
        r->warnings++;
        take(&c);
    }
    while (!eof(&c) && !c.failed) {
        if (!expect(&c, CC_PAGE)) break;
        if (at(&c, c.pos) != CC_T_INT) { fail(&c); break; }
        take(&c);
        if (!block(&c, CC_PEND, CC_PEND)) break;
        if (!expect(&c, CC_PEND)) break;
        r->pages++;
    }
    if (coverage && !c.failed) {
        for (k = 0; k < size; k++) {
            if (coverage[k] == 0) r->uncovered++;
            else if (coverage[k] > 1) r->overlap++;
        }
    }
    return c.failed ? -1 : 0;
}
