#!/usr/bin/env python3
"""logging Manager 的空 appender 兜底：避免 0x1e650a0 处对 NULL 对象做虚调用。
   原指令 (0x1e650a0..0x1e650bb)：
     ldr x0,[x8,#112]; stp xzr,xzr,[x1]; str q0,[sp]; ldr x8,[x0]; ldr x8,[x8,#24];
     mov x1,sp; blr x8
   移植环境下 x0 为 NULL。站点改为跳入 .rodata 空洞中的洞穴：
     x0 非空 → 原样执行上述虚调用；
     x0 为空 → 不调用，把结果槽 [sp+8] 清零（后续 ldr x19,[sp,#8]; cbz 跳过任务调度），
               x0 指向洞穴内的占位对象、x1=sp，与"调用了一个空 ret"时的寄存器状态一致。
   洞穴只用 PC 相对指令，不含绝对地址：库的装载基址随宿主 /etc/ld.so.cache 大小变化。"""
import struct, sys

LIB = sys.argv[1] if len(sys.argv) > 1 else 'runtime/libwxhld.so'
SITE = 0x1e650a0
AFTER_BLR = 0x1e650bc
CAVE = 0x59d08c
PLACEHOLDER = CAVE + 0x40   # 洞穴内全零占位对象
CAVE_END = CAVE + 0x70      # 旧版补丁写到 +0x6c

d = bytearray(open(LIB, 'rb').read())

def b_enc(pc, target):
    return 0x14000000 | (((target - pc) >> 2) & 0x03FFFFFF)
def cbz_enc(pc, target, rt):
    return 0xB4000000 | ((((target - pc) >> 2) & 0x7FFFF) << 5) | rt
def adr_enc(pc, target, rd):
    imm21 = (target - pc) & 0x1FFFFF
    return 0x10000000 | ((imm21 & 3) << 29) | ((imm21 >> 2) << 5) | rd

NULL_PATH = CAVE + 0x24
cave = [
    0xf9403900,                            # +00 ldr x0, [x8, #112]
    0xa9007c3f,                            # +04 stp xzr, xzr, [x1]
    0x3d8003e0,                            # +08 str q0, [sp]
    cbz_enc(CAVE + 0x0c, NULL_PATH, 0),    # +0c cbz x0, null_path
    0xf9400008,                            # +10 ldr x8, [x0]
    0xf9400d08,                            # +14 ldr x8, [x8, #24]
    0x910003e1,                            # +18 mov x1, sp
    0xd63f0100,                            # +1c blr x8
    b_enc(CAVE + 0x20, AFTER_BLR),         # +20 b after_blr
    0xf90007ff,                            # +24 null_path: str xzr, [sp, #8]
    adr_enc(CAVE + 0x28, PLACEHOLDER, 0),  # +28 adr x0, placeholder
    0x910003e1,                            # +2c mov x1, sp
    b_enc(CAVE + 0x30, AFTER_BLR),         # +30 b after_blr
]
assert CAVE + len(cave) * 4 <= PLACEHOLDER
d[CAVE:CAVE_END] = bytes(CAVE_END - CAVE)   # 洞穴原为全零；清掉旧版写入的绝对地址
for i, w in enumerate(cave):
    struct.pack_into('<I', d, CAVE + i * 4, w)
# 站点：b cave + 2 nop（SITE+8 的 str q0 已由洞穴执行）
struct.pack_into('<I', d, SITE, b_enc(SITE, CAVE))
struct.pack_into('<I', d, SITE + 4, 0xd503201f)
struct.pack_into('<I', d, SITE + 8, 0xd503201f)
open(LIB, 'wb').write(d)
print(f'fake appender patched (PC-relative): cave={CAVE:#x} site={SITE:#x}')
