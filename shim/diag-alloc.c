/* 诊断版分配器拦截：LD_PRELOAD 用。无 __thread（避免早期 TLS 崩溃）。 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define GZ_HEAD  0xCAFEF00DBEEF0001ULL
#define GZ_TAIL  0xCAFEF00DBEEF0002ULL
#define GZ_PAD   32
typedef struct { unsigned long magic; size_t size; void *site; unsigned long pad; } GzHead;

static void *(*r_malloc)(size_t);
static void *(*r_calloc)(size_t, size_t);
static void *(*r_realloc)(void *, size_t);
static void (*r_free)(void *);
static int inited;

#define LIVE_MAX 200000
static struct { unsigned char *raw; size_t size; void *site; int alive; } live[LIVE_MAX];
static int live_n, free_cnt;

static void init(void) {
    if (inited) return;
    r_malloc  = dlsym(RTLD_NEXT, "malloc");
    r_calloc  = dlsym(RTLD_NEXT, "calloc");
    r_realloc = dlsym(RTLD_NEXT, "realloc");
    r_free    = dlsym(RTLD_NEXT, "free");
    inited = 1;
}
void *malloc(size_t n) {
    init();
    unsigned char *raw = r_malloc(n + sizeof(GzHead) + GZ_PAD);
    if (!raw) return 0;
    GzHead *h = (GzHead *)raw;
    h->magic = GZ_HEAD; h->size = n; h->site = __builtin_return_address(0); h->pad = 0;
    *(unsigned long *)(raw + sizeof(GzHead) + n) = GZ_TAIL;
    if (live_n < LIVE_MAX) { live[live_n].raw = raw; live[live_n].size = n; live[live_n].site = h->site; live[live_n].alive = 1; live_n++; }
    return raw + sizeof(GzHead);
}
void *calloc(size_t a, size_t b) {
    init();
    size_t n = a * b;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}
void *realloc(void *q, size_t n) {
    init();
    if (!q) return malloc(n);
    GzHead *h = (GzHead *)((unsigned char *)q - sizeof(GzHead));
    if (h->magic != GZ_HEAD) { void *p = r_realloc(q, n); if (p) fprintf(stderr, "[dg] realloc 非 wrapping 块 %p\n", q); return p; }
    size_t old = h->size;
    void *p = malloc(n);
    if (p) { memcpy(p, q, old < n ? old : n); free(q); }
    return p;
}
void free(void *p) {
    init();
    if (!p) { r_free(0); return; }
    unsigned char *raw = (unsigned char *)p - sizeof(GzHead);
    GzHead *h = (GzHead *)raw;
    if (h->magic == GZ_HEAD) {
        if (*(unsigned long *)(raw + sizeof(GzHead) + h->size) != GZ_TAIL)
            fprintf(stderr, "[dg] 越界写!! site=%p size=%lu\n", h->site, (unsigned long)h->size);
        if (++free_cnt % 200 == 0)
            for (int i = 0; i < live_n; i++)
                if (live[i].alive && *(unsigned long *)(live[i].raw + sizeof(GzHead) + live[i].size) != GZ_TAIL) {
                    fprintf(stderr, "[dg-audit] #%d site=%p size=%lu 被踩!\n", i, live[i].site, (unsigned long)live[i].size);
                    live[i].alive = 0;
                }
        h->magic = 0;
        r_free(raw);
    } else {
        r_free(p);   /* 非包装块：ld.so 早期分配等 */
    }
}
