#!/usr/bin/env bash
# NEEDED-name fixes + versym surgery for all runtime libs（幂等：先重拷原始库）
# 用法: 10_patch_libs.sh [输出目录，默认 runtime/]。只处理 APK 里的库；shim/libz 由 20_build.sh 产出。
# 安装时也在用户机器上运行（AppImage install），因此只依赖 patchelf + python3。
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE="$(dirname "$SCRIPT_DIR")"
APK_ROOT="${WETYPE_APK_ROOT:-$BASE/.deps/wechat-ime}"
SRC="$APK_ROOT/lib/arm64-v8a"
OUT="${1:-$BASE/runtime}"
mkdir -p "$OUT"
cd "$OUT"

for tool in patchelf python3; do
  command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done

if [ ! -f "$SRC/libwxhld_jni.so" ]; then
  echo "Missing WeType APK libraries at $SRC" >&2
  echo "Run: $BASE/scripts/prepare_assets.sh" >&2
  exit 1
fi

# 引擎闭包 9 库（shim 是我们自己的原生库，绝不能被手术！）
ENGINELIBS="libandromeda.so libcryptopp.so libc++_shared.so libime_net.so libowl.so \
  libprotobuf-lite.so libtensorflowlite_c.so libwcwss.so libwxhld.so libwxhld_jni.so libwechatxlog.so"

for f in $ENGINELIBS; do
  cp -f "$SRC/$f" .
done
for p in $ENGINELIBS; do
  patchelf --replace-needed libc.so libc.so.6 "$p" 2>/dev/null || true
  patchelf --replace-needed libm.so libm.so.6 "$p" 2>/dev/null || true
  patchelf --replace-needed libdl.so libdl.so.2 "$p" 2>/dev/null || true
  patchelf --replace-needed libz.so libz.so.1 "$p" 2>/dev/null || true
  patchelf --replace-needed libstdc++.so libstdc++.so.6 "$p" 2>/dev/null || true
  patchelf --replace-needed liblog.so libwetype-shim.so "$p" 2>/dev/null || true
  patchelf --remove-needed libandroid.so "$p" 2>/dev/null || true
  # 关键：去掉对 libc 的直接依赖。否则 libc 在本库局部作用域内必压过 shim 的
  # stdio 包装（fflush/fprintf… 被 bionic 步长的 __sF 指针烧穿）。
  # 摘掉后这些符号经全局作用域回落到主程序已加载的 libc，而 stdio 只剩 shim。
  patchelf --remove-needed libc.so.6 "$p" 2>/dev/null || true
  # 同理必须摘干净：libm/libdl/libz 会把 libc 以深度 2 拉进本库的搜索列表，
  # glibc 的 _dl_sort_maps 深度降序会让 libc 永远压过 depth-1 的 shim。
  # 这些符号统一回落到全局作用域（probe 已链 -lm -ldl -lz 兑底）。
  patchelf --remove-needed libm.so.6 "$p" 2>/dev/null || true
  patchelf --remove-needed libdl.so.2 "$p" 2>/dev/null || true
  patchelf --remove-needed libz.so.1 "$p" 2>/dev/null || true
  # shim 必须在每个引 __sF 的库的局部作用域里且排在 libc 之前（stdio 包装生效的前提）
  if ! patchelf --print-needed "$p" | grep -qx 'libwetype-shim.so'; then
    patchelf --add-needed libwetype-shim.so "$p"
  fi
done

python3 "$SCRIPT_DIR/versym_surgery.py" $ENGINELIBS
python3 "$SCRIPT_DIR/12_rename_syms.py" $ENGINELIBS
python3 "$SCRIPT_DIR/promote_shim.py" $ENGINELIBS
python3 "$SCRIPT_DIR/17_disable_scan_sig.py" libwxhld.so
python3 "$SCRIPT_DIR/21_fake_appender.py" libwxhld.so

echo "== 手术后 DT_NEEDED 抽查 (libwxhld.so)"
patchelf --print-needed libwxhld.so
