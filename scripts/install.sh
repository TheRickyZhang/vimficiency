#!/usr/bin/env bash
# Install the native library and the journal scorer into build/: download the
# prebuilts for this platform when HEAD is a release tag, otherwise build from
# source.
# Plugin managers run this as the build step (lazy.nvim `build`, packer `run`,
# vim-plug `do`). Set VIMFICIENCY_BUILD_FROM_SOURCE=1 to skip the download.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

repo="TheRickyZhang/vimficiency"

platform=""
ext=""
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)        platform="linux-x86_64";  ext="so" ;;
  Linux-aarch64)       platform="linux-aarch64"; ext="so" ;;
  Darwin-arm64)        platform="macos-arm64";   ext="dylib" ;;
esac

# Both files or neither: a library without its scorer would leave `:Vimfy stats`
# half working, so a release missing either one falls back to a source build.
try_prebuilt() {
  [[ -z "${VIMFICIENCY_BUILD_FROM_SOURCE:-}" ]] || return 1
  [[ -n "$platform" ]] || return 1
  command -v curl >/dev/null || return 1
  local tag
  tag="$(git describe --tags --exact-match HEAD 2>/dev/null)" || return 1

  local base="https://github.com/$repo/releases/download/$tag"
  local tmp
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/vimficiency.XXXXXX")"
  if ! curl -fsSL --retry 2 -o "$tmp/lib" "$base/libvimficiency-$platform.$ext" ||
     ! curl -fsSL --retry 2 -o "$tmp/vimfy_journal" "$base/vimfy_journal-$platform"; then
    rm -f "$tmp/lib" "$tmp/vimfy_journal"
    rmdir "$tmp"
    echo "vimficiency: no prebuilt for $platform at $base"
    return 1
  fi
  mkdir -p build
  chmod 644 "$tmp/lib"
  chmod 755 "$tmp/vimfy_journal"
  mv "$tmp/lib" "build/libvimficiency.$ext"
  mv "$tmp/vimfy_journal" build/vimfy_journal
  rmdir "$tmp"
  echo "vimficiency: installed prebuilt $tag ($platform)"
}

if try_prebuilt; then
  exit 0
fi

echo "vimficiency: building from source (Release, library and journal scorer)"
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF -DVIMF_BUILD_TESTS=OFF
cmake --build build -j --target vimficiency vimfy_journal
