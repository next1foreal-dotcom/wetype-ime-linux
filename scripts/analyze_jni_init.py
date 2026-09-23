#!/usr/bin/env python3
"""解析 libwxhld_jni initialize() 的反汇编：
adrp+add 构造的地址 → .rodata 字符串（Java 字段名/签名），
配合 ldr/blr（JNIEnv 表调用）与 str 偏移，输出字段→结构体偏移映射。"""
import re, struct, subprocess, sys

SO = sys.argv[1] if len(sys.argv) > 1 else 'libwxhld_jni.so'
ASM = sys.argv[2] if len(sys.argv) > 2 else '/tmp/init_fn.asm'

# 1) section headers: vaddr -> file offset
secs = []
out = subprocess.run(['readelf', '-S', '-W', SO], capture_output=True, text=True).stdout
for m in re.finditer(r'\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', out):
    name, addr, off, size = m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)
    if addr:
        secs.append((addr, size, off, name))

def v2o(v):
    for addr, size, off, name in secs:
        if addr <= v < addr + size:
            return off + (v - addr)
    return None

data = open(SO, 'rb').read()
def getstr(v):
    o = v2o(v)
    if o is None:
        return None
    e = data.find(b'\0', o)
    s = data[o:e]
    return s.decode('utf-8', 'replace') if 0 < len(s) < 200 else None

# 2) parse asm: track adrp page per (reg), resolve add imm
page = {}
ldreg = {}  # reg -> (base, off) 最近一次 ldr xN, [xM, #imm]
JNI = {0x30:'FindClass', 0x108:'GetMethodID', 0x110:'CallObjectMethod', 0x2f0:'GetFieldID',
       0x2f8:'GetObjectField', 0x300:'GetBooleanField', 0x308:'GetByteField', 0x310:'GetCharField',
       0x318:'GetShortField', 0x320:'GetIntField', 0x328:'GetLongField', 0x330:'GetFloatField',
       0x338:'GetDoubleField', 0x538:'NewStringUTF', 0x548:'GetStringUTFChars', 0x550:'ReleaseStringUTFChars',
       0x558:'GetArrayLength', 0x1e8:'CallVoidMethod', 0x358:'SetObjectField', 0x480:'GetStaticFieldID',
       0x488:'GetStaticObjectField', 0x5a0:'GetByteArrayElements', 0x5c0:'GetByteArrayRegion'}
prev = None
for line in open(ASM, encoding='utf-8', errors='replace'):
    m = re.match(r'\s*([0-9a-f]+):\s*[0-9a-f]+\s+(adrp|add|blr|ldr|str|stur|strb|strh)\t(.*)', line)
    if not m:
        continue
    addr, op, ops = int(m.group(1), 16), m.group(2), m.group(3)
    if op == 'adrp':
        r, rest = ops.split(',', 1)
        pm = re.search(r'([0-9a-f]+)\s*<', rest)
        if pm:
            page[r.strip()] = int(pm.group(1), 16)
    elif op == 'add':
        parts = [p.strip() for p in ops.split(',')]
        if len(parts) == 3 and parts[0] in page and parts[1] == parts[0] and parts[2].startswith('#'):
            imm = int(parts[2][1:], 16) if parts[2].startswith('#0x') else int(parts[2][1:])
            s = getstr(page[parts[0]] + imm)
            if s and s.isprintable() and len(s) > 1:
                print(f'{addr:6x} STR  "{s}"')
            page.pop(parts[0], None)
    elif op == 'ldr':
        mm = re.match(r'(x[0-9]+), \[(x[0-9]+)(?:, #(0x[0-9a-f]+|\d+))?\]', ops)
        if mm:
            dst, src, offs = mm.group(1), mm.group(2), mm.group(3)
            offv = int(offs, 16) if offs and offs.startswith('0x') else (int(offs) if offs else 0)
            # 数据段指针解引用：adrp 页 + ldr → 读指针 → 字符串
            if src in page:
                ptr_addr = page[src] + offv
                o = v2o(ptr_addr)
                if o is not None and o + 8 <= len(data):
                    ptr, = struct.unpack_from('<Q', data, o)
                    s = getstr(ptr)
                    if s and s.isprintable() and len(s) > 1:
                        print(f'{addr:6x} STRP [{ptr_addr:#x}] -> "{s}"')
                        continue
        mm = re.match(r'(x[0-9]+), \[(x[0-9]+)(?:, #(0x[0-9a-f]+|\d+))?\]', ops)
        if mm:
            dst, src, offs = mm.group(1), mm.group(2), mm.group(3)
            offv = int(offs, 16) if offs and offs.startswith('0x') else (int(offs) if offs else 0)
            st = ldreg.get(src)
            if st is not None:  # src 是 JNIEnv 或函数表 → dst 成为函数表/函数指针
                ldreg[dst] = st + offv if ldreg.get('_table') == src or st >= 0 else st
            if ldreg.get('_env') == src:
                ldreg[dst] = 'TABLE'
            elif ldreg.get(dst) == 'TABLE' or (ldreg.get(src) == 'TABLE' and dst == src):
                ldreg[dst] = offv
    elif op == 'mov':
        mm = re.match(r'(x[0-9]+), (x[0-9]+)', ops)
        if mm and mm.group(2) == 'x0':
            ldreg[mm.group(1)] = 'ENV'
            ldreg['_env'] = mm.group(1)
    elif op == 'blr':
        r = ops.strip()
        v = ldreg.get(r)
        if v == 'TABLE' or v is None:
            name = '?'
        else:
            name = JNI.get(v, f'jni+{v:#x}')
        print(f'{addr:6x} CALL {name} (*{r})')
    elif op in ('str', 'stur', 'strb', 'strh'):
        mm = re.search(r'\[(x[0-9]+|sp)(?:, #(0x[0-9a-f]+|\d+))?\]', ops)
        if mm:
            off = mm.group(2)
            offv = int(off[1:], 16) if off and off.startswith('#0x') else (int(off[1:]) if off else 0)
            print(f'{addr:6x} STORE [{mm.group(1)}, +{offv:#x}] {ops.split(",")[0]}')
