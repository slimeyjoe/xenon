/* Script VM conformance tool.
 *
 *   test_cc check FILE.bin...    static check of decompressed scripts: one line per file
 *                                "NAME pages=N unknown=N uncovered=N overlap=N lenbyte=N warn=N
 *                                 strings=N parse_error=N"
 *   test_cc trace FILE.bin       run the script with a tracing host: every message, menu
 *                                (always picks option 0, or cancels when it was shown before)
 *                                and opcode is printed; stops at FILE/EXIT/error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm/cc.h"

static uint8_t *read_file(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        exit(2);
    fclose(f);
    *len = (uint32_t)n;
    return buf;
}

static const char *basename_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* ---- tracing host ---- */
static int g_menus_seen;

static void t_message(void *ctx, const uint8_t *text)
{
    (void)ctx;
    printf("MSG %s\n", (const char *)text);
}

static int t_menu(void *ctx, const uint8_t *const *items, int count, int cursor)
{
    int i;
    (void)ctx;
    printf("MENU cursor=%d:", cursor);
    for (i = 0; i < count; i++)
        printf(" [%s]", (const char *)items[i]);
    printf("\n");
    g_menus_seen++;
    return g_menus_seen > 50 ? -1 : (g_menus_seen - 1) % (count ? count : 1);
}

static int t_sysmenu(void *ctx)
{
    (void)ctx;
    printf("SYSMENU -> return to title\n");
    return 1;
}

static void t_op(void *ctx, uint8_t op, const int *ints, int n, const uint8_t *str)
{
    int i;
    (void)ctx;
    printf("OP %02X", op);
    for (i = 0; i < n; i++)
        printf(" %d", ints[i]);
    if (str)
        printf(" \"%s\"", (const char *)str);
    printf("\n");
}

static void t_error(void *ctx, int code, const char *script, int page, int label)
{
    (void)ctx;
    printf("ERROR %d in %s page=%d label=%d\n", code, script, page, label);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 3)
        return 2;
    if (strcmp(argv[1], "check") == 0) {
        int rc = 0;
        for (i = 2; i < argc; i++) {
            uint32_t len;
            uint8_t *buf = read_file(argv[i], &len);
            uint8_t *cov = malloc(len + 1);
            struct cc_check_result r;
            cc_check(buf, len, &r, cov);
            printf("%s pages=%d unknown=%d uncovered=%d overlap=%d lenbyte=%d warn=%d strings=%d parse_error=%d\n",
                   basename_of(argv[i]), r.pages, r.unknown_ops, r.uncovered, r.overlap,
                   r.lenbyte_mismatch, r.warnings, r.strings, r.parse_error);
            if (r.parse_error || r.unknown_ops || r.uncovered || r.overlap)
                rc = 1;
            free(cov);
            free(buf);
        }
        return rc;
    }
    if (strcmp(argv[1], "trace") == 0) {
        struct cc_host host;
        struct cc_vm vm;
        uint32_t len;
        uint8_t *buf = read_file(argv[2], &len);
        memset(&host, 0, sizeof host);
        host.message = t_message;
        host.menu = t_menu;
        host.system_menu = t_sysmenu;
        host.op = t_op;
        host.error = t_error;
        cc_vm_init(&vm, &host);
        cc_vm_set_script(&vm, basename_of(argv[2]), buf, len);
        cc_run(&vm);
        printf("END file_jump=%d script=%s leave=%d quit=%d err=%d page=%d\n",
               vm.file_jump, vm.script, vm.leave, vm.quit, vm.err, vm.cur_page);
        return vm.err == CC_E_NONE ? 0 : 1;
    }
    return 2;
}
