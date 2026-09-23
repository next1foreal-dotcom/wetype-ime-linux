#!/usr/bin/env python3
"""把引擎库 .dynstr 中的 stdio 符号名原地改写（首字母大写，长度不变）。

背景：glibc 2.35+ _dl_sort_maps_dfs 拓扑排序会把"无依赖约束"的 shim 排到
搜索列表末尾，导致与 libc 同名的包装符号（fflush 等）永远绑不到 shim。
改写后这些名字（Fflush/Fprintf/…）只有 shim 导出，与顺序无关地绑定到 shim，
由 shim 完成.FILE* 映射（bionic __sF 指针 → glibc 真实流）后再转发 glibc。
"""
import struct, sys, os

# 需要改写的符号（全部在 wetype-shim 中有对应大写别名）
RENAMES = [
    'fflush', 'fprintf', 'vfprintf', 'fwrite', 'fputs', 'fread',
    'fgetc', 'fputc', 'fclose', 'feof', 'ferror',
    'fseek', 'fseeko', 'ftell', 'ftello',
    'sigaction', 'sigprocmask', 'sigfillset', 'sigdelset',
    'mmap', 'munmap', 'free', 'dlerror', 'dlsym',
]

def rename(path: str) -> str:
    b = bytearray(open(path, 'rb').read())
    e_shoff, = struct.unpack_from('<Q', b, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', b, 0x3a)
    shs = [struct.unpack_from('<IIQQQQIIQQ', b, e_shoff + j * e_shentsize)
           for j in range(e_shnum)]
    shstrtab = shs[e_shstrndx]
    sstr = b[shstrtab[4]:shstrtab[4] + shstrtab[5]]
    total = 0
    for s in shs:
        if bytes(sstr[s[0]:]).split(b'\0')[0] != b'.dynstr':
            continue
        start, size = s[4], s[5]
        dynstr = bytearray(b[start:start + size])
        n = 0
        for name in RENAMES:
            pat = b'\0' + name.encode() + b'\0'
            rep = b'\0' + name[0].upper().encode() + name[1:].encode() + b'\0'
            pos = dynstr.find(pat)
            while pos != -1:
                dynstr[pos:pos + len(rep)] = rep
                n += 1
                pos = dynstr.find(pat, pos + 1)
        b[start:start + size] = dynstr
        total += n
    open(path, 'wb').write(b)
    return f"{os.path.basename(path)}: {total} syms renamed"

if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(rename(p))
