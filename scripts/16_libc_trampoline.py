#!/usr/bin/env python3
"""libc free 跳板补丁。

把 sysroot 的 libc.so.6 拷入 syslocal/lib/ 并在其 free 符号入口写入
`b shim_Free`（qemu-user 无 ASLR，shim/libc 装载地址恒定）：
  libc free 入口 0x400000A96BA0 → shim Free 0x400000D04390
shim Free 的透传目标 = libc free+4 = 0x400000A96BA4（见 shim 源码 g_libc_free_body）。
效果：所有经 PLT 的 free（含 libc++_shared operator delete）都先经过
mmap 区间识别——arena 指针不再送进 glibc 的严格检查。
"""
import struct, shutil, sys, subprocess

SHIM_FREE_GUEST = 0x400000c50000 + 0x4814
LIBC_FREE_GUEST = 0x400000A96BA0       # libc 基址 0x400000a00000 + 0x96ba0
LIBC_SRC = '/usr/aarch64-linux-gnu/lib/libc.so.6'
DST = 'syslocal/lib/libc.so.6'

shutil.copyfile(LIBC_SRC, DST)
data = bytearray(open(DST, 'rb').read())

# 验证入口指令是预期的 cbz x0（防止布局漂移）
cur = struct.unpack_from('<I', data, 0x96ba0)[0]
assert cur == 0xB4000880, f'libc free 入口指令漂移: {cur:#x}'

off = (SHIM_FREE_GUEST - LIBC_FREE_GUEST) >> 2
assert -(1 << 25) <= off < (1 << 25)
data[0x96ba0:0x96ba0 + 4] = struct.pack('<I', 0x14000000 | (off & 0x3FFFFFF))
open(DST, 'wb').write(data)
print(f'trampoline: libc free @{LIBC_FREE_GUEST:#x} → shim Free @{SHIM_FREE_GUEST:#x}')
