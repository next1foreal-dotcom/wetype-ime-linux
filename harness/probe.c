/* probe: 在 qemu-aarch64 下 dlopen 引擎库，报告成功/失败与可选符号探测 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lib.so> [symbol ...]\n", argv[0]);
        return 2;
    }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) {
        fprintf(stderr, "dlopen FAILED: %s\n", dlerror());
        return 1;
    }
    printf("dlopen ok: %s\n", argv[1]);
    for (int i = 2; i < argc; i++) {
        void *s = dlsym(h, argv[i]);
        printf("  dlsym(%-40s) = %p\n", argv[i], s);
        dlerror();
    }
    /* 自检：对 shim 句柄逐个 dlsym，验证 .gnu.hash 可查性 */
    const char *syms[] = {"fflush", "fprintf", "fwrite", "__sF", "sigsetjmp", "res_init"};
    for (int i = 0; i < 6; i++) {
        void *s = dlsym(h, syms[i]);
        printf("  self dlsym(%-12s) = %p %s\n", syms[i], s, s ? "" : "  <-- MISS");
        dlerror();
    }
    /* 最小复现：模拟 bionic 消费者调用 Fflush(&__sF[1]) */
    if (argc > 2 && !strcmp(argv[2], "--fflush-test")) {
        void *ff = dlsym(h, "Fflush");
        void *sf = dlsym(h, "__sF");
        printf("fflush-test: Fflush=%p __sF=%p\n", ff, sf);
        int r = ((int (*)(FILE *))ff)((FILE *)((char *)sf + 152));
        printf("fflush-test: Fflush(&__sF[1]) = %d\n", r);
        r = ((int (*)(FILE *))ff)((FILE *)((char *)sf + 304));
        printf("fflush-test: Fflush(&__sF[2]) = %d\n", r);
    }
    return 0;
}
