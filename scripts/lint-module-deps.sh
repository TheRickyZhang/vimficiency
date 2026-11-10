#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# mawk lacks 3-arg match(); without rg the extractor is silently empty.
command -v rg >/dev/null
command -v gawk >/dev/null

is_known_module() {
  case "$1" in
    Types|Optimizer|Interpreter|Session|Effort|Keyboard|VimCore|Boundary|Utils) return 0 ;;
    *) return 1 ;;
  esac
}

is_allowed_edge() {
  case "$1" in
    "Optimizer->Types"|"Optimizer->Utils"|"Optimizer->Keyboard"|"Optimizer->VimCore"|"Optimizer->Boundary"|"Optimizer->Interpreter"|"Optimizer->Effort") return 0 ;;
    "Effort->Keyboard") return 0 ;;
    "Interpreter->Types"|"Interpreter->VimCore"|"Interpreter->Keyboard"|"Interpreter->Utils") return 0 ;;
    "Session->Types"|"Session->Utils"|"Session->Interpreter") return 0 ;;
    "VimCore->Types"|"VimCore->Utils"|"VimCore->Boundary") return 0 ;;
    "Keyboard->Types"|"Keyboard->Utils") return 0 ;;
    "Boundary->Types"|"Boundary->Utils") return 0 ;;
    "Utils->Types") return 0 ;;
    *) return 1 ;;
  esac
}

declare -A seen_counts=()
declare -A bad_counts=()
declare -A bad_details=()

while IFS=$'\t' read -r src_mod dst_mod location include_path; do
  if ! is_known_module "$src_mod"; then
    continue
  fi
  if ! is_known_module "$dst_mod"; then
    continue
  fi
  if [[ "$src_mod" == "$dst_mod" ]]; then
    continue
  fi

  edge="${src_mod}->${dst_mod}"
  seen_counts["$edge"]=$(( ${seen_counts["$edge"]:-0} + 1 ))

  if ! is_allowed_edge "$edge"; then
    bad_counts["$edge"]=$(( ${bad_counts["$edge"]:-0} + 1 ))
    bad_details["$edge"]+=$'\n'"  - ${location} -> ${include_path}"
  fi
done < <(
  rg -n -e '#include "([^"]+)"' -e '^\s*(export\s+)?import\s+vf\.' src | gawk '
    match($0, /^([^:]+):([0-9]+):#include "([^"]+)"/, m) {
      split(m[1], fparts, "/");
      split(m[3], iparts, "/");
      printf "%s\t%s\t%s:%s\t%s\n", fparts[2], iparts[1], m[1], m[2], m[3];
      next;
    }
    match($0, /^([^:]+):([0-9]+):\s*(export\s+)?import\s+vf\.([A-Za-z0-9_]+)\.([A-Za-z0-9_.]+)/, m) {
      split(m[1], fparts, "/");
      printf "%s\t%s\t%s:%s\t%s\n", fparts[2], m[4], m[1], m[2], "vf." m[4] "." m[5];
    }
  '
)

echo "Observed cross-module edges:"
if [[ ${#seen_counts[@]} -eq 0 ]]; then
  echo "  (none)"
else
  {
    for edge in "${!seen_counts[@]}"; do
      printf "%5d %s\n" "${seen_counts[$edge]}" "$edge"
    done
  } | sort -k2,2
fi

if [[ ${#bad_counts[@]} -gt 0 ]]; then
  echo
  echo "Disallowed module dependencies found:"
  {
    for edge in "${!bad_counts[@]}"; do
      printf "%5d %s\n" "${bad_counts[$edge]}" "$edge"
      printf "%s\n" "${bad_details[$edge]}"
    done
  } | sort -k2,2
  exit 1
fi

echo
echo "Module dependency lint passed."
