#!/usr/bin/env bash
cd "$(dirname "$0")/.."
export LD_LIBRARY_PATH="$PWD/runtime"
exec qemu-aarch64-static -L /usr/aarch64-linux-gnu ./harness/probe "$@"
