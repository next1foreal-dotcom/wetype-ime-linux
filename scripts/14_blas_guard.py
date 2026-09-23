#!/usr/bin/env python3
"""BLAS 空上下文保护补丁（最终版）。

uarm helper(0x1e6507c) 从 obj->field8->field112 取执行上下文；移植环境下为
NULL → vtable 虚调用崩溃。方案：.text 零洞放 helper 完整副本（帧结构一致，
PC 相对指令全部重编码），插入 obj/holder/ctx 三重空守卫，任一为空即走
副本自身的 canary+epilogue 收尾（等价"无任务"）。
"""
import struct, sys

SO = sys.argv[1] if len(sys.argv) > 1 else 'runtime/libwxhld.so'
CAVE     = 0x59d08c
CALLSITE = 0xdbb4e8
HELPER   = 0x1e6507c
SCHED    = 0x21b4c80
RELEASE  = 0x21ca210
STKCHK   = 0x1e65130

def w(x): return struct.pack('<I', x)
def bl(src, dst):
    off = (dst - src) >> 2
    assert -(1 << 25) <= off < (1 << 25)
    return struct.pack('<I', 0x94000000 | (off & 0x3FFFFFF))
def b(src, dst):
    off = (dst - src) >> 2
    assert -(1 << 25) <= off < (1 << 25)
    return struct.pack('<I', 0x14000000 | (off & 0x3FFFFFF))
def cbz(rt, src, dst):
    off = (dst - src) >> 2
    assert 0 < off < (1 << 19), f'cbz {off:#x}'
    return struct.pack('<I', 0xB4000000 | ((off & 0x7FFFF) << 5) | rt)
def cbnz(rt, src, dst):
    off = (dst - src) >> 2
    assert 0 < off < (1 << 19)
    return struct.pack('<I', 0xB5000000 | ((off & 0x7FFFF) << 5) | rt)

data = bytearray(open(SO, 'rb').read())
orig = bytes(data[HELPER:HELPER + 0x90])

E = bytearray()
E += orig[0x00:0x1c]                                    # 00 prologue + canary setup
E += cbz(0, CAVE + 0x1c, CAVE + 0x7c)                   # 1c cbz x0, exit
E += orig[0x1c:0x24]                                    # 20 ldr x8,[x0,#8]; ldr q0,[x1]
E += cbz(8, CAVE + 0x28, CAVE + 0x7c)                   # 28 cbz x8, exit
E += orig[0x24:0x28]                                    # 2c ldr x0,[x8,#112]
E += cbz(0, CAVE + 0x30, CAVE + 0x7c)                   # 30 cbz x0, exit
E += orig[0x28:0x44]                                    # 34 stp/str/ldr/ldr/mov/blr
E += orig[0x40:0x44]                                    # 4c ldr x19,[sp,#8]
E += cbz(19, CAVE + 0x50, CAVE + 0x7c)                  # 50 cbz x19, exit
E += orig[0x48:0x54]                                    # 54 add x1,x19,#8; mov x0,#-1
E += bl(CAVE + 0x5c, SCHED)                             # 5c bl  schedule
E += cbnz(0, CAVE + 0x60, CAVE + 0x7c)                  # 60 cbnz x0, exit
E += orig[0x58:0x70]                                    # 64 ldr/mov/ldr/blr/mov
E += bl(CAVE + 0x78, RELEASE)                           # 78 bl  __release_weak
# exit = 0x7c:
E += orig[0x70:0x8c]                                    # 7c canary check + epilogue（b.ne 在 0x88）
# 重编码洞内 0x88 处的 b.ne → STKCHK
ne_pos = CAVE + 0x88
E[0x88:0x8c] = b(ne_pos, STKCHK)

data[CAVE:CAVE + len(E)] = E
data[CALLSITE:CALLSITE + 4] = bl(CALLSITE, CAVE)
open(SO, 'wb').write(data)
print(f'patched: {CALLSITE:#x} → cave {CAVE:#x} ({len(E)}B) [副本+三重守卫]')
