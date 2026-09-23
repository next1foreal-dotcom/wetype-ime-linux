#define _GNU_SOURCE
/* libwetype-shim: bionic liblog/__sF 替身，仅覆盖微信输入法引擎闭包实际引用的符号。
   未覆盖的符号在 dlopen 报错时按需补充。 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

/* ---- bionic __sF：bionic 代码按 sizeof(bionic FILE)=152 计算各元素的地址，
   而真 glibc FILE 是 216B+vtable，两种布局无法共存于同一块内存。
   方案：__sF 只做 3×152B 占位符，拦截所有 FILE* 传参的 stdio 入口，
   把落在占位区间内的指针映射回 glibc 真实 stdin/stdout/stderr 后转发。 */
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

char __sF[3 * 152];
#define WSF_STRIDE 152

static FILE *wsf_map(FILE *p) {
    unsigned long off = (unsigned long)p - (unsigned long)(void *)__sF;
    if (off < 3 * WSF_STRIDE) {
        switch (off / WSF_STRIDE) {
        case 0: return stdin;
        case 1: return stdout;
        default: return stderr;
        }
    }
    return p;
}

/* RTLD_NEXT 取真函数，惰性初始化 */
/* RTLD_NEXT 取真函数，惰性初始化（显式类型，避开 __typeof__ 限制） */
static int (*real_fclose)(FILE *);
static int (*real_feof)(FILE *);
static int (*real_ferror)(FILE *);
static int (*real_fflush)(FILE *);
static int (*real_fgetc)(FILE *);
static int (*real_fputc)(int, FILE *);
static int (*real_fputs)(const char *, FILE *);
static size_t (*real_fread)(void *, size_t, size_t, FILE *);
static size_t (*real_fwrite)(const void *, size_t, size_t, FILE *);
static int (*real_fseek)(FILE *, long, int);
static int (*real_fseeko)(FILE *, off_t, int);
static long (*real_ftell)(FILE *);
static off_t (*real_ftello)(FILE *);
static int (*real_vfprintf)(FILE *, const char *, va_list);

static int w_is_self(void *p) {
    Dl_info a, b;
    if (!dladdr((void *)w_is_self, &a)) return 0;
    if (!dladdr(p, &b)) return 0;
    return a.dli_fbase == b.dli_fbase;
}
static void *wreal(const char *n, void *cur) {
    if (cur) return cur;
    void *p = dlsym(RTLD_NEXT, n);
    if (!p || w_is_self(p)) {
        /* shim 以依赖身份被 RTLD_LOCAL 加载时不在全局作用域，RTLD_NEXT 会失败；
           回退 RTLD_DEFAULT（全局作用域），若解析回自身则放弃防递归 */
        p = dlsym(RTLD_DEFAULT, n);
        if (p && w_is_self(p)) p = 0;
    }
    return p;
}

#define WSTDIO(name, ret, argdecl, arguse, realargs) \
static ret w_##name(argdecl) { \
    return ((ret (*)())wreal(#name, real_##name))(realargs); \
}

static int w_fclose(FILE *f) { return ((int (*)(FILE *))wreal("fclose", real_fclose))(wsf_map(f)); }
static int w_feof(FILE *f) { return ((int (*)(FILE *))wreal("feof", real_feof))(wsf_map(f)); }
static int w_ferror(FILE *f) { return ((int (*)(FILE *))wreal("ferror", real_ferror))(wsf_map(f)); }
static int w_fflush(FILE *f) { return ((int (*)(FILE *))wreal("fflush", real_fflush))(wsf_map(f)); }
static int w_fgetc(FILE *f) { return ((int (*)(FILE *))wreal("fgetc", real_fgetc))(wsf_map(f)); }
static int w_fputc(int c, FILE *f) { return ((int (*)(int, FILE *))wreal("fputc", real_fputc))(c, wsf_map(f)); }
static int w_fputs(const char *s, FILE *f) { return ((int (*)(const char *, FILE *))wreal("fputs", real_fputs))(s, wsf_map(f)); }
static size_t w_fread(void *b, size_t n, size_t m, FILE *f) { return ((size_t (*)(void *, size_t, size_t, FILE *))wreal("fread", real_fread))(b, n, m, wsf_map(f)); }
static size_t w_fwrite(const void *b, size_t n, size_t m, FILE *f) { return ((size_t (*)(const void *, size_t, size_t, FILE *))wreal("fwrite", real_fwrite))(b, n, m, wsf_map(f)); }
static int w_fseek(FILE *f, long o, int w) { return ((int (*)(FILE *, long, int))wreal("fseek", real_fseek))(wsf_map(f), o, w); }
static int w_fseeko(FILE *f, off_t o, int w) { return ((int (*)(FILE *, off_t, int))wreal("fseeko", real_fseeko))(wsf_map(f), o, w); }
static long w_ftell(FILE *f) { return ((long (*)(FILE *))wreal("ftell", real_ftell))(wsf_map(f)); }
static off_t w_ftello(FILE *f) { return ((off_t (*)(FILE *))wreal("ftello", real_ftello))(wsf_map(f)); }
static int w_vfprintf(FILE *f, const char *fmt, va_list ap) { return ((int (*)(FILE *, const char *, va_list))wreal("vfprintf", real_vfprintf))(wsf_map(f), fmt, ap); }

int fclose(FILE *f) { return w_fclose(f); }
int feof(FILE *f) { return w_feof(f); }
int ferror(FILE *f) { return w_ferror(f); }
int fflush(FILE *f) { return w_fflush(f); }
int fgetc(FILE *f) { return w_fgetc(f); }
int fputc(int c, FILE *f) { return w_fputc(c, f); }
int fputs(const char *s, FILE *f) { return w_fputs(s, f); }
size_t fread(void *b, size_t n, size_t m, FILE *f) { return w_fread(b, n, m, f); }
size_t fwrite(const void *b, size_t n, size_t m, FILE *f) { return w_fwrite(b, n, m, f); }
int fseek(FILE *f, long o, int w) { return w_fseek(f, o, w); }
int fseeko(FILE *f, off_t o, int w) { return w_fseeko(f, o, w); }
long ftell(FILE *f) { return w_ftell(f); }
off_t ftello(FILE *f) { return w_ftello(f); }
int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = w_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

static const char *prio_name(int prio) {
    switch (prio) {
    case 2: return "V"; case 3: return "D"; case 4: return "I";
    case 5: return "W"; case 6: return "E"; case 7: return "F";
    default: return "?";
    }
}

int __android_log_write(int prio, const char *tag, const char *text) {
    return fprintf(stderr, "[alog %s/%s] %s\n", prio_name(prio), tag ? tag : "-", text ? text : "");
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);
    return __android_log_write(prio, tag, buf);
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __android_log_vprint(prio, tag, fmt, ap);
    va_end(ap);
    return r;
}

int __android_log_buf_write(int bufID, int prio, const char *tag, const char *text) {
    (void)bufID;
    return __android_log_write(prio, tag, text);
}

int __android_log_buf_print(int bufID, int prio, const char *tag, const char *fmt, ...) {
    (void)bufID;
    va_list ap;
    va_start(ap, fmt);
    int r = __android_log_vprint(prio, tag, fmt, ap);
    va_end(ap);
    return r;
}

int __android_log_is_loggable(int prio, const char *tag, int def) {
    (void)prio; (void)tag;
    return def;
}

/* ---- bionic abort 消息接甲：libc++_shared 引用，转为 stderr 打印 ---- */
void android_set_abort_message(const char *msg) {
    fprintf(stderr, "[abort-message] %s\n", msg ? msg : "(null)");
}

/* ---- bionic errno 入口 ---- */
int *__errno(void) {
    return __errno_location();
}

/* ---- bionic system property 接甲：返回空值，按需再伪造具体属性 ---- */
#include <stddef.h>

int __system_property_get(const char *name, char *value) {
    (void)name;
    if (value) value[0] = '\0';
    return 0;
}

const void *__system_property_find(const char *name) {
    (void)name;
    return NULL;
}

void __system_property_read_callback(const void *pi,
                                     void (*cb)(void *, const char *, const char *, unsigned int),
                                     void *cookie) {
    (void)pi; (void)cb; (void)cookie;
}

int __system_property_foreach(void (*cb)(const void *, void *), void *cookie) {
    (void)cb; (void)cookie;
    return 0;
}

int android_get_device_api_level(void) {
    return 34; /* 冒充 Android 14 */
}

/* ---- Android looper 接甲（andromeda）：假 looper，poll 恒超时 ---- */
static int fake_looper;

void *ALooper_prepare(void) { return &fake_looper; }
void ALooper_acquire(void *looper) { (void)looper; }
void ALooper_release(void *looper) { (void)looper; }
int ALooper_pollOnce(int timeoutMs, int *outFd, int *outEvents, void **outData) {
    if (outFd) *outFd = -1;
    if (outEvents) *outEvents = 0;
    if (outData) *outData = 0;
    (void)timeoutMs;
    return 3; /* ALOOPER_POLL_TIMEOUT */
}
int ALooper_addFd(void *looper, int fd, int ident, int events,
                  int (*cb)(int, int, void *), void *data) {
    (void)looper; (void)fd; (void)ident; (void)events; (void)cb; (void)data;
    return 1;
}
int ALooper_removeFd(void *looper, int fd) { (void)looper; (void)fd; return 1; }

/* ---- bionic resolver：wxhld 引用，glibc 的 res_init 不可直接等价，空实现 ---- */
int res_init(void) { return 0; }

/* ---- bionic __pthread_cleanup_push/pop：glibc 无此导出（只有 __pthread_cleanup_routine）。
   xlog 等库直接引用。自维护链表实现，语义与 bionic 宏展开一致。 ---- */
typedef struct wpcu {
    struct wpcu *prev;
    void (*fn)(void *);
    void *arg;
} wpcu_t;
static wpcu_t *g_wpcu_top;
void __pthread_cleanup_push(wpcu_t *c, void (*fn)(void *), void *arg) {
    c->prev = g_wpcu_top; c->fn = fn; c->arg = arg; g_wpcu_top = c;
}
void __pthread_cleanup_pop(wpcu_t *c, int execute) {
    g_wpcu_top = c->prev;
    if (execute && c->fn) c->fn(c->arg);
}

/* ---- NDK AAsset 家族：磁盘文件后端。引擎读词库用 openFileDescriptor 拿 fd。 ---- */
typedef struct { FILE *f; long size; } wasset_t;
static const char *g_asset_roots[] = {
    "assets/", "runtime/assets/", ".deps/wechat-ime/assets/",
};
void *AAssetManager_fromJava(void *env, void *am) {
    (void)env; (void)am;
    static int wam_token;
    return &wam_token;
}
void *AAssetManager_open(void *mgr, const char *path, long mode) {
    (void)mgr; (void)mode;
    char full[600];
    for (unsigned i = 0; i < sizeof(g_asset_roots)/sizeof(g_asset_roots[0]); i++) {
        snprintf(full, sizeof full, "%s%s", g_asset_roots[i], path);
        FILE *f = fopen(full, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        wasset_t *a = malloc(sizeof(wasset_t));
        a->f = f; a->size = size;
        return a;
    }
    return NULL;
}
long AAsset_openFileDescriptor(void *asset, int *fd, long *offset) {
    wasset_t *a = asset;
    if (!a || !a->f) return -1;
    *fd = dup(fileno(a->f));
    *offset = 0;
    return 0;
}
void AAsset_close(void *asset) {
    wasset_t *a = asset;
    if (a) { if (a->f) fclose(a->f); free(a); }
}

/* ---- 大写别名：消费者库的 .dynstr 已被 12_rename_syms.py 改写指向这些名字。
   名字独一无二，符号查找顺序无关。 ---- */
int Fflush(FILE *f) { return w_fflush(f); }
int Fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = w_vfprintf(f, fmt, ap);
    va_end(ap); return r;
}
int Vfprintf(FILE *f, const char *fmt, va_list ap) { return w_vfprintf(f, fmt, ap); }
int Fwrite(const void *b, size_t n, size_t m, FILE *f) { return w_fwrite(b, n, m, f); }
int Fputs(const char *s, FILE *f) { return w_fputs(s, f); }
size_t Fread(void *b, size_t n, size_t m, FILE *f) { return w_fread(b, n, m, f); }
int Fgetc(FILE *f) { return w_fgetc(f); }
int Fputc(int c, FILE *f) { return w_fputc(c, f); }
int Fclose(FILE *f) { return w_fclose(f); }
int Feof(FILE *f) { return w_feof(f); }
int Ferror(FILE *f) { return w_ferror(f); }
int Fseek(FILE *f, long o, int w) { return w_fseek(f, o, w); }
int Fseeko(FILE *f, off_t o, int w) { return w_fseeko(f, o, w); }
long Ftell(FILE *f) { return w_ftell(f); }
off_t Ftello(FILE *f) { return w_ftello(f); }

/* ---- bionic FORTIFY _chk 桩（glibc 没有，其余 __*_chk glibc 自带） ---- */
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>

size_t __strlen_chk(const char *s, size_t n) { (void)n; return strlen(s); }
char *__strchr_chk(const char *p, int ch, size_t n) { (void)n; return strchr(p, ch); }
char *__strrchr_chk(const char *p, int ch, size_t n) { (void)n; return strrchr(p, ch); }
char *__strncpy_chk2(char *dst, const char *src, size_t n,
                     size_t dst_size, size_t src_size) {
    (void)dst_size; (void)src_size;
    return strncpy(dst, src, n);
}
void __FD_SET_chk(int fd, fd_set *set, size_t sz) { (void)sz; FD_SET(fd, set); }
void __FD_CLR_chk(int fd, fd_set *set, size_t sz) { (void)sz; FD_CLR(fd, set); }
int  __FD_ISSET_chk(int fd, fd_set *set, size_t sz) { (void)sz; return FD_ISSET(fd, set); }


/* ============ mmap 区间登记：free(mmap指针) → munmap ============ */
#define MM_MAX 4096
static struct { void *base; size_t len; } g_mm[MM_MAX];
static int g_mm_n = 0;
static volatile int g_mm_lock = 0;
static __thread int g_inmm = 0;
static int mm_contains(void *p);
static void mm_lock(void) { while (__sync_lock_test_and_set(&g_mm_lock, 1)) ; }
static void mm_unlock(void) { __sync_lock_release(&g_mm_lock); }
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    void *(*r)(void *, size_t, int, int, int, long) = wreal("mmap", 0);
    void *p = r(addr, len, prot, flags, fd, off);
    if (p != (void *)-1 && !g_inmm) {
        g_inmm = 1;
        mm_lock();
        if (g_mm_n < MM_MAX) { g_mm[g_mm_n].base = p; g_mm[g_mm_n].len = len; g_mm_n++; }
        mm_unlock();
        g_inmm = 0;
    }
    return p;
}
int munmap(void *addr, size_t len) {
    int (*r)(void *, size_t) = wreal("munmap", 0);
    if (!g_inmm) {
        g_inmm = 1;
        mm_lock();
        for (int i = 0; i < g_mm_n; i++)
            if (g_mm[i].base == addr) { g_mm[i] = g_mm[g_mm_n - 1]; g_mm_n--; break; }
        mm_unlock();
        g_inmm = 0;
    }
    return r(addr, len);
}
/* ============ 分配登记表：只释放自己分配的指针 ============
 * 引擎在 bionic（jemalloc）上开发，会把非 malloc 指针/被踩坏的指针丢给 free；
 * glibc 严格校验直接 abort（"free(): invalid pointer/size"）。这里保留 glibc
 * 堆语义（引擎依赖它），但把每个 malloc 结果登记下来：free 时只有在登记表中
 * 的指针才真正交给 glibc 释放，其余一律跳过（泄漏可接受，换来不崩）。
 */
#define REG_BITS 21
#define REG_SIZE (1u << REG_BITS)
typedef struct { void *p; size_t n; } RegEnt;
static RegEnt *g_reg = 0;
static volatile int g_reg_lock = 0;
static void reg_lock(void) { while (__sync_lock_test_and_set(&g_reg_lock, 1)) {} }
static void reg_unlock(void) { __sync_lock_release(&g_reg_lock); }
void *Malloc(size_t n);
void Free(void *p);
static void dbg(const char *fmt, ...) {
    char b[256]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n > 0) { ssize_t _w = write(2, b, n); (void)_w; }
}
static void reg_init(void) {
    if (g_reg) return;
    long r = syscall(SYS_mmap, 0, REG_SIZE * sizeof(RegEnt), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (r > 0) {
        g_reg = (RegEnt *)r;
        char b[160];
        int n = snprintf(b, sizeof b, "[reg] 登记表 %u 槽 %zu MB [shim] Malloc=%p Free=%p\n",
                         REG_SIZE, (size_t)(REG_SIZE * sizeof(RegEnt)) >> 20,
                         (void *)&Malloc, (void *)&Free);
        if (n > 0) { ssize_t _w = write(2, b, n); (void)_w; }
    }
}
static unsigned int reg_hash(void *p) {
    unsigned long long x = (unsigned long long)(unsigned long)p >> 4;
    x *= 0x9E3779B97F4A7C15ULL;
    return (unsigned int)(x >> (64 - REG_BITS));
}
#define REG_TOMB ((void *)1)
static void reg_put(void *p, size_t n) {
    if (!p) return;
    if (!g_reg) reg_init();
    if (!g_reg) return;
    reg_lock();
    unsigned int i = reg_hash(p);
    int placed = 0;
    unsigned int tomb = (unsigned int)-1;
    for (unsigned int k = 0; k < REG_SIZE; k++) {
        unsigned int j = (i + k) & (REG_SIZE - 1);
        if (g_reg[j].p == p) { g_reg[j].n = n; placed = 1; break; }
        if (g_reg[j].p == REG_TOMB && tomb == (unsigned int)-1) tomb = j;
        if (!g_reg[j].p) {
            if (tomb != (unsigned int)-1) j = tomb;
            g_reg[j].p = p; g_reg[j].n = n; placed = 1; break;
        }
    }
    reg_unlock();
    (void)placed;
}
static int reg_take(void *p, size_t *n) {
    if (!g_reg) return 0;
    reg_lock();
    unsigned int i = reg_hash(p);
    int found = 0;
    for (unsigned int k = 0; k < REG_SIZE; k++) {
        unsigned int j = (i + k) & (REG_SIZE - 1);
        if (!g_reg[j].p) break;
        if (g_reg[j].p == p) { if (n) *n = g_reg[j].n; g_reg[j].p = REG_TOMB; found = 1; break; }
    }
    reg_unlock();
    return found;
}
static int reg_has(void *p) {
    if (!g_reg || !p) return 0;
    reg_lock();
    unsigned int i = reg_hash(p);
    int found = 0;
    for (unsigned int k = 0; k < REG_SIZE; k++) {
        unsigned int j = (i + k) & (REG_SIZE - 1);
        if (!g_reg[j].p) break;
        if (g_reg[j].p == p) { found = 1; break; }
    }
    reg_unlock();
    return found;
}
void *Malloc(size_t n) {
    void *(*r)(size_t) = wreal("malloc", 0);
    { static int t = 0; if (t < 6) { t++; dbg("[ma] r=%p n=%zu c=%p\n", (void *)r, n, __builtin_return_address(0)); } }
    void *p = r(n);
    reg_put(p, n);
    { static int t = 0; if (t < 6) { t++; dbg("[ma] -> p=%p\n", p); } }
    return p;
}
void *Calloc(size_t a, size_t b) {
    void *(*r)(size_t, size_t) = wreal("calloc", 0);
    void *p = r(a, b);
    reg_put(p, a * b);
    return p;
}
void *Realloc(void *q, size_t n) {
    if (!q) return Malloc(n);
    if (reg_has(q)) {
        void *(*r)(void *, size_t) = wreal("realloc", 0);
        size_t old = 0;
        reg_take(q, &old);
        void *p = r(q, n);
        reg_put(p, n);
        return p;
    }
    void *(*r)(void *, size_t) = wreal("realloc", 0);   /* 非本分配器指针：原样透传 */
    return r(q, n);
}
void *Memalign(size_t align, size_t n) {
    void *(*r)(size_t, size_t) = wreal("memalign", 0);
    void *p = r(align, n);
    reg_put(p, n);
    return p;
}
void *Aligned_alloc(size_t align, size_t n) {
    void *(*r)(size_t, size_t) = wreal("aligned_alloc", 0);
    if (!r) r = wreal("memalign", 0);
    void *p = r(align, n);
    reg_put(p, n);
    return p;
}
int Posix_memalign(void **out, size_t align, size_t n) {
    int (*r)(void **, size_t, size_t) = wreal("posix_memalign", 0);
    int rc = r(out, align, n);
    if (!rc) reg_put(*out, n);
    return rc;
}
size_t Malloc_usable_size(void *p) {
    if (reg_has(p)) {
        size_t n = 0;
        reg_lock();
        unsigned int i = reg_hash(p);
        for (unsigned int k = 0; k < REG_SIZE; k++) {
            unsigned int j = (i + k) & (REG_SIZE - 1);
            if (!g_reg[j].p) break;
            if (g_reg[j].p == p) { n = g_reg[j].n; break; }
        }
        reg_unlock();
        return n;
    }
    size_t (*r)(void *) = wreal("malloc_usable_size", 0);
    return r ? r(p) : 0;
}
/* 兼容旧名（引擎若直接调用小写） */
void *malloc(size_t n) { return Malloc(n); }
void *calloc(size_t a, size_t b) { return Calloc(a, b); }
void *realloc(void *q, size_t n) { return Realloc(q, n); }
static __thread int g_in_free = 0;
static void (*g_libc_free_body)(void *) = 0;  /* 惰性取 libc free 体（避免硬编码基址） */
static void shim_free_impl(void *p) {
    if (!p) return;
    if (mm_contains(p)) return;   /* arena/mmap 内部指针：引擎自治，忽略 */
    {   /* 默认：基线透传（与引擎在 bionic 上一致）；WETYPE_REG_FREE=1 才启用登记表 */
        const char *e = getenv("WETYPE_REG_FREE");
        if (!e || strcmp(e, "1")) {
            if (!g_libc_free_body) {
                void *fp = dlsym(RTLD_NEXT, "free");
                if (!fp) fp = dlsym(RTLD_DEFAULT, "free");
                if (!fp) fp = (void *)0x400000A96BA0;
                {
                    unsigned int insn = *(volatile unsigned int *)fp;
                    if ((insn & 0xFC000000u) == 0x14000000u) fp = (char *)fp + 4;
                }
                g_libc_free_body = (void (*)(void *))fp;
                dbg("[freebody] dlsym=%p -> %p\n", fp, (void *)g_libc_free_body);
            }
            g_libc_free_body(p);
            return;
        }
    }
    if (!g_libc_free_body) {
        void *fp = dlsym(RTLD_NEXT, "free");
        if (!fp) fp = dlsym(RTLD_DEFAULT, "free");
        if (!fp) fp = (void *)0x400000A96BA0;
        {   /* 若入口已被跳板 b 指令覆盖（0x14 高位），则 +4 取原始函数体 */
            unsigned int insn = *(volatile unsigned int *)fp;
            if ((insn & 0xFC000000u) == 0x14000000u) fp = (char *)fp + 4;
        }
        g_libc_free_body = (void (*)(void *))fp;
    }
    if (!g_in_free) {
        g_in_free = 1;
        if (reg_take(p, 0)) { g_libc_free_body(p); g_in_free = 0; return; }   /* 自己分配的：真释放 */
        g_in_free = 0;
        { static int sk = 0; if (sk < 200) { sk++; char b[96]; int n = snprintf(b, sizeof b, "[free-skip] p=%p c=%p\n", p, __builtin_return_address(0)); if (n > 0) { ssize_t _w = write(2, b, n); (void)_w; } } }
        return;   /* 非本分配器指针（libc 内部/静态/已踩坏）：跳过，防 glibc abort */
    }
    g_libc_free_body(p);
}
void free(void *p) { shim_free_impl(p); }
/* ============ Mmap/Munmap/Free：改名消费者的 mmap 感知释放 ============ */
void *Mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    return mmap(addr, len, prot, flags, fd, off);
}
int Munmap(void *addr, size_t len) {
    return munmap(addr, len);
}
static int mm_contains(void *p) {
    if (g_inmm) return 0;
    int hit;
    g_inmm = 1;
    mm_lock();
    for (int i = 0; i < g_mm_n; i++)
        if ((char *)p >= (char *)g_mm[i].base &&
            (char *)p < (char *)g_mm[i].base + g_mm[i].len) { hit = 1; mm_unlock(); g_inmm = 0; return hit; }
    mm_unlock();
    g_inmm = 0;
    return 0;
}
void Free(void *p) {
    if (!p) return;
    if (mm_contains(p)) return;   /* arena/mmap 内部指针：引擎自治，忽略 */
    shim_free_impl(p);
}
/* ============ Dlerror：永不返回 NULL ============
 * 引擎有 strlen(dlerror()) 的调用（libandromeda+0x252724）：glibc 无错误时
 * dlerror() 返回 NULL，直接 strlen(NULL) 崩溃；返回空串保持语义安全。
 */
char *Dlerror(void) {
    char *(*r)(void) = wreal("dlerror", 0);
    char *e = r ? r() : 0;
    return e ? e : (char *)"";
}
/* ============ Wdlsym：引擎的 dlsym("free") 命中 mmap 感知分配器 ============ */
void *Dlsym(void *handle, const char *name) {
    void *(*r)(void *, const char *) = wreal("dlsym", 0);
    if (name) {
        if (!strcmp(name, "free") || !strcmp(name, "malloc") || !strcmp(name, "calloc") ||
            !strcmp(name, "realloc") || !strcmp(name, "mmap") || !strcmp(name, "munmap"))
            fprintf(stderr, "[Dlsym] %s\n", name);
        if (!strcmp(name, "free"))    return (void *)free;
        if (!strcmp(name, "malloc"))  return (void *)malloc;
        if (!strcmp(name, "calloc"))  return (void *)calloc;
        if (!strcmp(name, "realloc")) return (void *)realloc;
        if (!strcmp(name, "mmap"))    return (void *)mmap;
        if (!strcmp(name, "munmap"))  return (void *)munmap;
    }
    {
        void *res = r(handle, name);
        /* JNI 库以 RTLD_LOCAL 加载，其依赖 libc++_shared 的符号不在全局作用域，
           引擎 dlsym(RTLD_NEXT,"__cxa_throw") 会得 NULL → 调用即崩。
           回退：拿已加载的 libc++_shared 句柄再查一次。 */
        if (!res && name) {
            static void *cpph = (void *)-2;
            if (cpph == (void *)-2) cpph = dlopen("libc++_shared.so", RTLD_NOW | RTLD_NOLOAD);
            if (cpph && cpph != (void *)-2) res = r(cpph, name);
        }
        static int t = 0;
        if (t < 300) {
            t++;
            dbg("[Dlsym] %s -> %p%s\n", name ? name : "(null)", res, res ? "" : "   <<< NULL");
        }
        return res;
    }
}
/* 信号族接甲见 wetype-signal.c（需脱离 glibc signal.h 原型约束编译） */
