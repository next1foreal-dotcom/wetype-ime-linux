#!/usr/bin/env python3
"""禁用后台 scan_signature 环境完整性与词库反篡改校验补丁。

微信键盘引擎后台定时触发 scan_signature (0x21c0884)，在 Linux/QEMU 环境下
词库校验失败后走向异常解引用分支，导致定期触发 SIGSEGV (signal 11)。
本补丁在 scan_signature 入口写入 ret (0xd65f03c0)，直接提前返回，根治崩溃。
"""
import struct, sys

SO = sys.argv[1] if len(sys.argv) > 1 else 'runtime/libwxhld.so'
SITE = 0x21c0884

data = bytearray(open(SO, 'rb').read())
cur = struct.unpack_from('<I', data, SITE)[0]

# 如果未打补丁，入口指令应为 stp x29, x30, [sp, #-96]! (0xa9ba7bfd)
RET = struct.pack('<I', 0xd65f03c0)
if cur == 0xa9ba7bfd:
    data[SITE:SITE+4] = RET
    open(SO, 'wb').write(data)
    print(f'patched: disabled scan_signature @ {SITE:#x} with ret')
elif cur == 0xd65f03c0:
    print(f'already patched: scan_signature @ {SITE:#x} has ret')
else:
    raise RuntimeError(f'unexpected instruction @ {SITE:#x}: {cur:#x}')
