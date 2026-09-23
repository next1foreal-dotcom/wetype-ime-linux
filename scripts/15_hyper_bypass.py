#!/usr/bin/env python3
"""hyper_params 缺失绕过补丁。

hp_base 解析在移植环境失败（堆溢出源），hyper_params_ 恒为 NULL →
ConfigDicts:912 与加载作用域两处 `cbz hyper_params_, throw` 必然触发异常，
新资源被丢弃，config_dict 永远失败。
本补丁 NOP 掉两处守卫（0xda78c4 / 0xdb8bac），让资源在无超参的情况下
强制安装（NN 排序降级，传统词典路径可用）。
注意：必须配合 WETYPE_SKIP_DICTS 跳过 hp_base.bin，否则其解析会破坏堆。
"""
import struct, sys

SO = sys.argv[1] if len(sys.argv) > 1 else 'runtime/libwxhld.so'
SITES = [0xda78c4, 0xdb8bac]

data = bytearray(open(SO, 'rb').read())
NOP = struct.pack('<I', 0xD503201F)
for s in SITES:
    data[s:s + 4] = NOP
open(SO, 'wb').write(data)
print('patched: NOP hyper_params_ guards @', ', '.join(hex(s) for s in SITES))
