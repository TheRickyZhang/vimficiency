#!/usr/bin/env bash
# Lua lint gate: two grep ratchets, then lua-language-server in batch-check
# mode over every Lua file in the repo (mirrors the LSP's editor-time
# diagnostics). Fails on any ratchet breach or LuaLS problem.
#
# Resolves the LuaLS binary in this order:
#   1. anything on PATH (system package, manual symlink, etc.)
#   2. Mason's installation (~/.local/share/nvim/mason/packages/...)
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

failed=0

# `assert(type(` is only legal at ingress constructors; everything inside
# trusts its inputs (lua/vimficiency/AGENTS.md "Trust boundaries").
ASSERT_TYPE_ALLOW='^lua/vimficiency/(ffi/|config/|config\.lua|subcommands\.lua|session/disk\.lua|session/alias\.lua)'
assert_hits="$(grep -rl 'assert(type(' lua/vimficiency --include='*.lua' | grep -Ev "$ASSERT_TYPE_ALLOW" || true)"
if [[ -n "$assert_hits" ]]; then
  echo "error: assert(type( outside an ingress constructor:" >&2
  echo "$assert_hits" >&2
  failed=1
fi

# Per-file pcall ceilings. Only error boundaries (`:Vimfy`, util.entry),
# ingress decoding, best-effort I/O, and window teardown may pcall. Lower a
# ceiling whenever a file drops below it; never raise one without a reason.
declare -A PCALL_CEILING=(
  [lua/vimficiency/async/poller.lua]=1
  [lua/vimficiency/dev_reload.lua]=2
  [lua/vimficiency/explore/buf_window.lua]=4
  [lua/vimficiency/explore/handlers.lua]=1
  [lua/vimficiency/explore/lifecycle.lua]=1
  [lua/vimficiency/ffi/config.lua]=2
  [lua/vimficiency/ffi/core.lua]=1
  [lua/vimficiency/ffi/explore.lua]=1
  [lua/vimficiency/health.lua]=1
  [lua/vimficiency/init.lua]=3
  [lua/vimficiency/session/compute.lua]=3
  [lua/vimficiency/session/disk.lua]=2
  [lua/vimficiency/session/picker.lua]=3
  [lua/vimficiency/session/picker/popups.lua]=5
  [lua/vimficiency/session/picker/preview.lua]=1
  [lua/vimficiency/session/result_window.lua]=2
  [lua/vimficiency/settings_profile.lua]=2
  [lua/vimficiency/simulate.lua]=1
  [lua/vimficiency/simulate/windows.lua]=1
  [lua/vimficiency/stats/log.lua]=1
  [lua/vimficiency/stats.lua]=1
  [lua/vimficiency/util.lua]=3
)
while IFS= read -r file; do
  count="$(grep -c 'pcall(' "$file" || true)"
  ceiling="${PCALL_CEILING[$file]:-0}"
  if (( count > ceiling )); then
    echo "error: $file has $count pcall( calls, ceiling is $ceiling" >&2
    failed=1
  elif (( count < ceiling )); then
    echo "note: $file has $count pcall( calls; lower its ceiling from $ceiling" >&2
  fi
done < <(find lua/vimficiency -name '*.lua' | sort)

if (( failed )); then
  exit 1
fi

luals="$(command -v lua-language-server || true)"
if [[ -z "$luals" ]]; then
  mason_luals="$HOME/.local/share/nvim/mason/packages/lua-language-server/lua-language-server"
  if [[ -x "$mason_luals" ]]; then
    luals="$mason_luals"
  else
    echo "error: lua-language-server not found on PATH or in Mason" >&2
    echo "install via: pacman -S lua-language-server  (or :MasonInstall lua-language-server)" >&2
    exit 1
  fi
fi

# LuaLS requires --logpath even though --check_format=pretty (the default)
# writes diagnostics to stdout. The logpath just collects internal logs.
# Older releases exit 0 with problems, so the summary line is the gate.
report="$("$luals" \
  --check "$repo_root" \
  --configpath "$repo_root/.luarc.json" \
  --logpath "${LUALS_LOG_DIR:-/tmp/luals-check}" 2>&1 || true)"
printf '%s\n' "$report"
if ! grep -q 'no problems found' <<<"$report"; then
  exit 1
fi
