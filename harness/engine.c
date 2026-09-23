/* Route A harness v2: 直调引擎平面 C API。
 * 结构体布局为静态重建（v1，待真值对拍）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

/* 布局偏移（十进制字节，基址=传入指针） */
#define OFF_PLATFORM   0    /* int */
#define OFF_PTR16      16   /* 引擎读取，语义未明 */
#define OFF_WORKDIR    24   /* char* 工作目录（引擎自动建目录） */
#define OFF_FLOAT184   184
#define OFF_FLOAT188   188
#define STRUCT_SIZE    384

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: engine <libwxhld.so> [platform]\n"); return 2; }
    const char *lib = argv[1];
    int platform = argc > 2 ? atoi(argv[2]) : 0;

    void *h = dlopen(lib, RTLD_NOW);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }
    printf("dlopen ok: %s\n", lib);

    void *sym_uin = dlsym(h, "wxime_reset_user_id");
    printf("wxime_reset_user_id @ %p\n", sym_uin);

    void *sym_init = dlsym(h, "wxime_initialize");
    if (!sym_init) { printf("dlsym wxime_initialize FAILED: %s\n", dlerror()); return 1; }
    printf("wxime_initialize @ %p\n", sym_init);

    if (mkdir("/tmp/wetype-work", 0755) && errno != EEXIST) {}
    static unsigned char buf[STRUCT_SIZE];
    memset(buf, 0, sizeof buf);
    *(int *)(buf + OFF_PLATFORM) = platform;
    /* 引擎会 free 这些指针：必须 malloc，不能传 rodata/栈内存 */
    *(char **)(buf + OFF_WORKDIR) = strdup("/tmp/wetype-work");

    /* uin 必须纯数字（内部 stoull），且要在 initialize 之前设 */
    const char *uin = "10000001";
    if (sym_uin) {
        int ru = ((int (*)(const char *, int))sym_uin)(uin, (int)strlen(uin));
        printf("wxime_reset_user_id(%s) -> %d\n", uin, ru);
    }

    /* 暴力实证：全部 {ptr,len} 对都填 uin，找出哪组是 uin 源 */
    {
        static const int pair_offs[] = {48, 64, 88, 104, 120};
        for (unsigned i = 0; i < sizeof(pair_offs)/sizeof(pair_offs[0]); i++) {
            char *p = strdup(uin);
            *(char **)(buf + pair_offs[i]) = p;
            *(int *)(buf + pair_offs[i] + 8) = (int)strlen(uin);
            printf("  pair+%d -> data=%p\n", pair_offs[i], (void *)p);
        }
        /* +168 不是字符串对，是指向容器对象的指针：{?, ?, data@+16, count@+24}。
           填空容器（count=0）→ 引擎跳过遍历。 */
        static unsigned char empty_container[32];
        memset(empty_container, 0, sizeof empty_container);
        *(void **)(buf + 168) = empty_container;
        fflush(stdout);
    }

    *(float *)(buf + OFF_FLOAT184) = 1.0f;
    *(float *)(buf + OFF_FLOAT188) = 1.0f;
    printf("calling wxime_initialize(platform=%d, workdir=/tmp/wetype-work)...\n", platform);
    fflush(stdout);
    int r = ((int (*)(void *))sym_init)(buf);
    printf("wxime_initialize -> %d (0x%x)\n", r, r);
    fflush(stdout);
    return 0;
}
