/* wetype-signal: bionic 信号族接甲（脱离 glibc signal.h 原型约束编译）。
   bionic:  sigset_t = 8B（64 信号），struct sigaction { flags@0, handler@8, mask@16 }；
   glibc:   sigset_t = 128B，          struct sigaction { handler@0, mask@8, flags@136, restorer@144 }。
   直接透传会越界读写调用方栈，必须逐字段翻译。
   实现体全部为 static（本地直调，无 PLT 竞争），导出名 + 大写别名都只做薄转发。 */
#include <unistd.h>
#include <sys/syscall.h>

#ifndef SYS_rt_sigprocmask
#define SYS_rt_sigprocmask 135 /* aarch64 */
#endif
#define W_SIG_BLOCK    0
#define W_SIG_UNBLOCK  1
#define W_SIG_SETMASK  2

/* glibc struct sigaction 布局镜像（aarch64 LP64） */
struct wglibc_sigaction {
    void *handler;
    unsigned long mask[16];     /* 128B */
    int flags;
    void *restorer;
};

extern int __sigaction(int, const struct wglibc_sigaction *, struct wglibc_sigaction *);
extern long syscall(long, ...);

/* bionic aarch64 布局镜像 */
struct wbionic_sigaction {
    unsigned int sa_flags;
    void *sa_handler;
    unsigned long sa_mask;      /* 8B sigset */
};

static int w_sigaction(int signum, const struct wbionic_sigaction *act,
                       struct wbionic_sigaction *oact) {
    struct wglibc_sigaction ga, go, *gp = 0;
    if (act) {
        __builtin_memset(&ga, 0, sizeof(ga));
        ga.handler = act->sa_handler;
        ga.flags = (int)act->sa_flags;
        ga.mask[0] = act->sa_mask;
        gp = &ga;
    }
    int r = __sigaction(signum, gp, oact ? &go : 0);
    if (oact) {
        oact->sa_handler = go.handler;
        oact->sa_flags = (unsigned int)go.flags;
        oact->sa_mask = go.mask[0];
    }
    return r;
}

static int w_sigprocmask(int how, const unsigned long *set, unsigned long *oldset) {
    /* 内核 rt_sigprocmask 原生 8B sigset，绕开 glibc 的 128B 读法 */
    return (int)syscall(SYS_rt_sigprocmask, (long)how, set, oldset, 8UL);
}

static int w_sigfillset(unsigned long *set) { *set = ~0UL; return 0; }

static int w_sigdelset(unsigned long *set, int signum) {
    if (signum >= 1 && signum <= 64) *set &= ~(1UL << (signum - 1));
    return 0;
}

/* ---- sigsetjmp/siglongjmp：wcwss 引的是 bionic 符号，glibc 无此导出，
   且 glibc jmp_buf(~312B) 大于 bionic jmp_buf(256B)，不能透传。
   自存自恢复，只用缓冲区前 176B，两侧全在本文件内自洽。 ---- */
void *sigsetjmp(unsigned long *env, int savemask) {
    long old = 0;
    __asm__ __volatile__(
        "stp x19, x20, [%0, #0]\n\t"
        "stp x21, x22, [%0, #16]\n\t"
        "stp x23, x24, [%0, #32]\n\t"
        "stp x25, x26, [%0, #48]\n\t"
        "stp x27, x28, [%0, #64]\n\t"
        "stp x29, x30, [%0, #80]\n\t"
        "mov x1, sp\n\t"
        "str x1, [%0, #96]\n\t"
        "stp d8, d9, [%0, #104]\n\t"
        "stp d10, d11, [%0, #120]\n\t"
        "stp d12, d13, [%0, #136]\n\t"
        "stp d14, d15, [%0, #152]"
        :: "r"(env) : "x1", "memory");
    env[21] = (unsigned long)(savemask ? 1 : 0);
    if (savemask) {
        syscall(SYS_rt_sigprocmask, W_SIG_SETMASK, 0L, &old, 8UL);
        env[22] = (unsigned long)old;
    }
    return 0; /* 直接返回路径恒为 0 */
}

void siglongjmp(unsigned long *env, int val) {
    if (env[21]) {
        unsigned long m = env[22];
        syscall(SYS_rt_sigprocmask, W_SIG_SETMASK, &m, 0L, 8UL);
    }
    __asm__ __volatile__(
        "ldp x19, x20, [%0, #0]\n\t"
        "ldp x21, x22, [%0, #16]\n\t"
        "ldp x23, x24, [%0, #32]\n\t"
        "ldp x25, x26, [%0, #48]\n\t"
        "ldp x27, x28, [%0, #64]\n\t"
        "ldp x29, x30, [%0, #80]\n\t"
        "ldr x1, [%0, #96]\n\t"
        "mov sp, x1\n\t"
        "ldp d8, d9, [%0, #104]\n\t"
        "ldp d10, d11, [%0, #120]\n\t"
        "ldp d12, d13, [%0, #136]\n\t"
        "ldp d14, d15, [%0, #152]\n\t"
        "mov x0, %1\n\t"
        "cmp x0, #0\n\t"
        "csinc x0, x0, xzr, ne\n\t"
        "br x30"
        :: "r"(env), "r"((unsigned long)val) : "x0", "x1", "memory");
    __builtin_unreachable();
}

/* ---- 导出层：小写（消费者 dynstr 未改写的）与大写（12_rename_syms.py 改写的）---- */
int sigaction(int signum, const struct wbionic_sigaction *act, struct wbionic_sigaction *oact) {
    return w_sigaction(signum, act, oact);
}
int sigprocmask(int how, const unsigned long *set, unsigned long *oldset) {
    return w_sigprocmask(how, set, oldset);
}
int sigfillset(unsigned long *set) { return w_sigfillset(set); }
int sigdelset(unsigned long *set, int signum) { return w_sigdelset(set, signum); }
int Sigaction(int signum, const struct wbionic_sigaction *act, struct wbionic_sigaction *oact) {
    return w_sigaction(signum, act, oact);
}
int Sigprocmask(int how, const unsigned long *set, unsigned long *oldset) {
    return w_sigprocmask(how, set, oldset);
}
int Sigfillset(unsigned long *set) { return w_sigfillset(set); }
int Sigdelset(unsigned long *set, int signum) { return w_sigdelset(set, signum); }
