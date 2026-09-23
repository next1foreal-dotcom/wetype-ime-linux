# WeType Linux

Linux integration for the WeType Android ARM64 input engine. The Fcitx 5 addon talks to an ARM64 harness through QEMU user mode.

> **Unofficial project.** This is an independent interoperability project. It is not affiliated with, endorsed by, or supported by Tencent. WeType, 微信输入法 and 微信 are trademarks of Tencent. Use of the WeType engine is subject to Tencent's terms; you are responsible for complying with them.

This repository and its AppImage contain only this project's own code plus redistributable third-party runtime pieces (QEMU user mode, ARM64 glibc, and zlib). WeType itself is never redistributed. It works like Paper's launcher for Minecraft servers: at install time, the official WeType 3.5.4 APK is downloaded from Tencent's server (`download.z.weixin.qq.com`), verified against a pinned SHA-256, and patched locally on the user's machine.

## Install from the AppImage

The installer needs `python3`, `patchelf`, `unzip`, and `curl` (or `wget`):

```sh
sudo apt install python3 patchelf unzip curl             # Debian, Ubuntu
sudo dnf install python3 patchelf unzip curl             # Fedora (RHEL: patchelf from EPEL)
sudo pacman -S --needed python patchelf unzip curl       # Arch Linux
sudo zypper install python3 patchelf unzip curl          # openSUSE
```

The installer checks these first and prints the command for your distribution if anything is missing. It never installs packages itself. QEMU and the ARM64 C library are bundled, so no cross packages are needed.

```sh
./WeTypeIME-Engine-x86_64.AppImage install --user    # or: sudo … install --system
```

`install` downloads the 214 MB APK once and caches it in `${XDG_CACHE_HOME:-~/.cache}/wetype-ime`, or in `/var/cache/wetype-ime` for system installs. Use `--apk FILE` to supply an already downloaded copy; it must match the pinned SHA-256.

The AppImage supports these commands:

```sh
./WeTypeIME-Engine-x86_64.AppImage install --user [--apk FILE]
./WeTypeIME-Engine-x86_64.AppImage uninstall --user
sudo ./WeTypeIME-Engine-x86_64.AppImage install --system [--apk FILE]
sudo ./WeTypeIME-Engine-x86_64.AppImage uninstall --system
./WeTypeIME-Engine-x86_64.AppImage demo nihao
```

With no option, `install` and `uninstall` use the user scope for a regular user and the system scope for root. User installation goes under `~/.local`; system installation goes under `/usr`. The Fcitx5 addon, registration files, locally patched engine, bundled QEMU and ARM64 glibc, launchers, desktop entry, and icon are installed as ordinary files. Existing dictionaries are kept during reinstall and after uninstall. User learning data under `${XDG_DATA_HOME:-~/.local/share}/wetype-ime` is also kept. Restart Fcitx5 after installing or uninstalling, and add “微信拼音” in the Fcitx5 input method configuration.

Before removing files, `uninstall` prints every existing installation path it will delete. It also prints the retained dictionary and APK cache paths. User uninstall lists the retained user data paths; system uninstall states the per-user data location pattern because it does not access other users' data.

`demo` and the default command (the engine daemon) run the installed engine, so they need a prior `install`. AppImage runtime options such as `--appimage-extract` remain available.

## Requirements for building

The supported build host is currently x86_64 Debian or Ubuntu. Install the host tools:

```sh
sudo apt update
sudo apt install build-essential cmake libfcitx5core-dev patchelf binutils file \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu qemu-user-static libc6-arm64-cross \
  unzip python3 python3-dbus python3-gi curl
```

The harness links against ARM64 zlib. On Debian:

```sh
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install zlib1g-dev:arm64
```

Ubuntu serves ARM64 packages from `ports.ubuntu.com` instead of the regular mirrors. First restrict the existing sources to amd64, then add the ports mirror. The example below is for 24.04 “noble”:

```sh
sudo sed -i '/^Types:/a Architectures: amd64' /etc/apt/sources.list.d/ubuntu.sources
printf '%s\n' 'Types: deb' 'URIs: http://ports.ubuntu.com/ubuntu-ports' \
  'Suites: noble noble-updates noble-security' 'Components: main universe' \
  'Architectures: arm64' 'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' |
  sudo tee /etc/apt/sources.list.d/ubuntu-ports.sources >/dev/null
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install zlib1g-dev:arm64
```

Alternatively, set `WETYPE_ZLIB_SO` to any ARM64 `libz.so.1`.

## Build the engine runtime

```sh
scripts/20_build.sh          # project shim + ARM64 harness; needs no APK
scripts/prepare_assets.sh    # download + verify the pinned APK into .deps/wechat-ime/
scripts/10_patch_libs.sh     # patch copies of the APK libraries into runtime/
```

`scripts/prepare_assets.sh /path/to/WeType.apk` uses a local APK instead of downloading. It must be the pinned 3.5.4 build. To use an already extracted APK tree, set `WETYPE_APK_ROOT` to the directory containing `lib/` and `assets/`. The patch step never modifies the APK itself.

## Build the Fcitx 5 addon

Build without root privileges:

```sh
fcitx5-wetype/build.sh
```

The AppImage packaging step also builds this addon and includes it in the image.

## Package the AppImage

```sh
scripts/e2_img.sh
```

This builds the shim, harness, and Fcitx5 addon. It packages them with the install scripts, the host's static `qemu-aarch64-static`, and a minimal ARM64 glibc from `libc6-arm64-cross`. It needs no APK. It refuses to package if any APK-derived file ends up in `AppDir/`, so the result can be published. Never publish a build that bundles the WeType libraries or dictionaries. The license notice for the bundled third-party components is written to `usr/share/doc/wetype-ime/THIRD-PARTY.md` inside the image.

`scripts/e2_img.sh` uses AppImage `appimagetool` 1.9.1. Set `APPIMAGETOOL` to a local copy to avoid downloading it. Set `APPIMAGE_RUNTIME` to a pinned type 2 runtime file for fully repeatable packaging. Without FUSE (containers, VMs), set `APPIMAGE_EXTRACT_AND_RUN=1`.

## Tests and diagnostics

- `scripts/bench_candidate_latency.py` measures candidate latency for 1–60 pinyin characters against the harness daemon directly (no Fcitx), typing either one key per request (`incremental`) or the whole string at once (`whole`).
- `scripts/test_length_threshold.py` probes incremental input, batched input, and optional backspace/replay sequences. It writes CSV and QEMU stderr logs beside the selected output path.
- `fcitx5-wetype/e2e/run_e2e.sh` runs a desktop integration check. It requires a working D-Bus session, Fcitx 5, Python D-Bus/GI bindings, QEMU, and prepared engine assets.
- `scripts/e3_verify.sh` runs the engine smoke checks against the installed engine, or against the source tree when none is installed. It runs the learning check against `runtime/`.

The Fcitx addon writes diagnostic logs to `/tmp/wetype-harness.log` by default; override this with `WETYPE_HARNESS_LOG`.

Example length probe against the installed engine:

```sh
E=~/.local/lib/wetype-ime/arm64
python3 scripts/test_length_threshold.py \
  --engine-dir $E --harness $E/wetype-harness \
  --qemu $E/qemu-aarch64-static --sysroot $E/sysroot \
  --max-length 64 --backspace-every-length
```

## Project layout

- `fcitx5-wetype/`: Fcitx 5 addon source and desktop integration check.
- `harness/`, `shim/`: ARM64 JNI compatibility harness and host compatibility shims.
- `scripts/`: APK download/verification, engine patching, build, packaging, and diagnostic utilities.
- `.deps/`, `runtime/`, `AppDir/`, `squashfs-root/`: local inputs and generated output; intentionally not tracked.

## License

This project is licensed under the GNU General Public License v3.0 or later; see [LICENSE](LICENSE). The AppImage additionally bundles QEMU (GPL-2.0), the GNU C Library (LGPL-2.1-or-later), and zlib (Zlib license); see `usr/share/doc/wetype-ime/THIRD-PARTY.md` inside the image. The WeType engine and dictionaries are Tencent's property and are not covered by this license or distributed by this project.
