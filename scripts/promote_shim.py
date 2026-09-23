#!/usr/bin/env python3
"""把 libwetype-shim.so 提为库的第一个 DT_NEEDED。
patchelf --add-needed 只能追加到末尾（排在 libc 之后，stdio 包装无法压过 glibc 原版），
这里直接交换动态数组里两个 DT_NEEDED 的字符串偏移，把 shim 挪到最前。
"""
import struct, sys, os

DT_NEEDED = 1
SHIM = b'libwetype-shim.so'

def reorder(path: str) -> str:
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
        return f"{os.path.basename(path)}: no PT_DYNAMIC"

    # dynstr 定位
    e_shoff, = struct.unpack_from('<Q', b, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', b, 0x3a)
    shs = [struct.unpack_from('<IIQQQQIIQQ', b, e_shoff + j * e_shentsize)
           for j in range(e_shnum)]
    shstrtab = shs[e_shstrndx]
    sstr = b[shstrtab[4]:shstrtab[4] + shstrtab[5]]
    dynstr_off = dynstr_sz = None
    for s in shs:
        name = bytes(sstr[s[0]:]).split(b'\0')[0]
        if name == b'.dynstr':
            dynstr_off, dynstr_sz = s[4], s[5]
            break
    if dynstr_off is None:
        return f"{os.path.basename(path)}: no .dynstr"
    strtab = bytes(b[dynstr_off:dynstr_off + dynstr_sz])

    def str_of(off):
        return bytes(strtab[off:strtab.index(b'\0', off)])

    needed = []  # (动态数组文件偏移, strtab offset)
    i = dyn_off
    while i < dyn_off + dyn_sz:
        tag, val = struct.unpack_from('<QQ', b, i)
        if tag == 0:
            break
        if tag == DT_NEEDED:
            needed.append((i, val))
        i += 16
    if not needed:
        return f"{os.path.basename(path)}: no NEEDED"

    shim_off = strtab.find(SHIM + b'\0')
    first_i, first_val = needed[0]
    if first_val == shim_off:
        return f"{os.path.basename(path)}: shim already first"
    # 找到 shim 的 NEEDED 项并和第一项交换字符串偏移
    for idx_i, val in needed:
        if val == shim_off:
            struct.pack_into('<QQ', b, first_i, DT_NEEDED, shim_off)
            struct.pack_into('<QQ', b, idx_i, DT_NEEDED, first_val)
            open(path, 'wb').write(b)
            return f"{os.path.basename(path)}: shim promoted to first NEEDED"
    return f"{os.path.basename(path)}: shim not in NEEDED"

if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(reorder(p))
