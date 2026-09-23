#!/usr/bin/env python3
"""ELF surgery for WeType arm64 libs → glibc/qemu-aarch64.

glibc 2.39 dl-version.c 铁律（源码实测）：
- l_versions 仅当 ndx_high>0（存在 VERNEED 或 VERDEF）才分配；
- VERNEED 在 → match_symbol 校验 "LIBC" 版本（bionic 版本名 glibc 没有）→ 报错；
- VERNEED 删掉后：
  * 无 VERDEF 的库：ndx_high=0 → l_versions=NULL、l_versyms=NULL，
    但 DT_VERSYM tag 还在 → dl-reloc 读 l_versyms[symidx] → 空指针崩。
    ⇒ 必须连 DT_VERSYM tag 一起删（此后所有符号查找走无版本路径）。
  * 有 VERDEF 的库（tflite）：ndx_high>0 → 走分配分支时
    map->l_versyms = D_PTR(map, l_info[VERSYM])，若 VERSYM tag 已删则
    对 NULL 动态项解引用（Elf64_Dyn.d_un 恰在 +8）→ 崩。
    ⇒ 必须保留 DT_VERSYM，并把 .gnu.version 条目全写 0：
       l_versions[0] 恒为 calloc 零槽（hash=0）→ 查找自动降级为无版本。
"""
import struct, sys, os

DT_VERNEED = 0x6ffffffe
DT_VERSYM = 0x6ffffff0
DT_VERDEF = 0x6ffffffd

def patch(path: str) -> str:
    b = bytearray(open(path, 'rb').read())
    e_phoff, = struct.unpack_from('<Q', b, 0x20)
    e_phentsize, e_phnum = struct.unpack_from('<HH', b, 0x36)

    dyn_off = dyn_sz = None
    for i in range(e_phnum):
        ph = e_phoff + i * e_phentsize
        if struct.unpack_from('<I', b, ph)[0] == 2:
            dyn_off = struct.unpack_from('<Q', b, ph + 8)[0]
            dyn_sz = struct.unpack_from('<Q', b, ph + 32)[0]
            break
    if dyn_off is None:
        return f"{os.path.basename(path)}: no PT_DYNAMIC, skip"

    has_verdef = has_versym = False
    i = dyn_off
    while i < dyn_off + dyn_sz:
        tag, _ = struct.unpack_from('<QQ', b, i)
        if tag == 0:
            break
        if tag == DT_VERNEED:
            struct.pack_into('<QQ', b, i, 0x6ffffff9, 0)  # 伪装 RELACOUNT:0
        elif tag == DT_VERDEF:
            has_verdef = True
        elif tag == DT_VERSYM:
            has_versym = True
        i += 16

    n = 0
    if has_verdef and has_versym:
        # 保留 VERSYM，条目全写 0（l_versions[0] 必为空名 → 无版本查找）
        e_shoff, = struct.unpack_from('<Q', b, 0x28)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', b, 0x3a)
        shs = [struct.unpack_from('<IIQQQQIIQQ', b, e_shoff + j * e_shentsize)
               for j in range(e_shnum)]
        shstr = shs[e_shstrndx]
        strtab = b[shstr[4]:shstr[4] + shstr[5]]
        for s in shs:
            if bytes(strtab[s[0]:]).split(b'\0')[0] == b'.gnu.version':
                for j in range(s[4], s[4] + s[5], 2):
                    struct.pack_into('<H', b, j, 0)
                    n += 1
    elif has_versym:
        # 无 VERDEF：VERNEED 删掉后 ndx_high=0，l_versions/l_versyms 都不会分配，
        # VERSYM tag 留着必崩 → 一并删除（再次扫描：VERNEED 槽位已是 RELACOUNT）
        i = dyn_off
        while i < dyn_off + dyn_sz:
            tag, _ = struct.unpack_from('<QQ', b, i)
            if tag == 0:
                break
            if tag == DT_VERSYM:
                struct.pack_into('<QQ', b, i, 0x6ffffff9, 0)
                break
            i += 16

    open(path, 'wb').write(b)
    return (f"{os.path.basename(path)}: VERNEED removed, "
            f"VERSYM {'kept, %d entries→0' % n if has_verdef else 'removed (无 VERDEF)'}")
if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(patch(p))
