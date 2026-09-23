#!/bin/bash
set -e
HERE="$(dirname "$(readlink -f "$0")")"

# The AppImage carries no WeType binaries; demo and engine run from an installation.
installed_bin() {
  local prefix
  for prefix in "$HOME/.local" /usr; do
    if [ -x "$prefix/bin/$1" ] && [ -f "$prefix/lib/wetype-ime/arm64/lib/libwxhld_jni.so" ]; then
      echo "$prefix/bin/$1"
      return
    fi
  done
  echo "WeType 引擎尚未安装。请先运行：$(basename "${APPIMAGE:-$0}") install" >&2
  exit 1
}

case "${1:-}" in
  install|uninstall)
    action="$1"
    shift
    exec "$HERE/usr/lib/wetype-ime/appimage-manage.sh" "$HERE" "$action" "$@"
    ;;
  demo)
    shift
    bin="$(installed_bin wetype-demo)"
    exec "$bin" "$@"
    ;;
  *)
    bin="$(installed_bin wetype-ime-engine)"
    exec "$bin" "$@"
    ;;
esac
