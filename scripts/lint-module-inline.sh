#!/usr/bin/env bash
# Member functions defined inside a class body are not implicitly inline in a
# named module; importers call them out of line unless marked `inline`.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

command -v gawk >/dev/null

find src -name '*.cppm' -print0 | sort -z | xargs -0 gawk '
function reset_stmt() { stmt = ""; stmt_line = 0 }

# Drop access labels, attributes and X-macro invocations preceding a declaration.
function normalize(s) {
  gsub(/[ \t]+/, " ", s)
  while (1) {
    t = s
    sub(/^ /, "", s)
    sub(/^(public|private|protected): ?/, "", s)
    sub(/^\[\[[^]]*\]\] ?/, "", s)
    sub(/^[A-Z_][A-Z0-9_]*\([^()]*\) ?/, "", s)
    if (s == t) return s
  }
}

function is_class_head(s) {
  return s ~ /^(export )?(template *<.*> *)?(struct|class|union)( |$)/
}

# Class template members are instantiated in the importer, so they inline anyway.
function is_template_head(s) {
  return s ~ /^(export )?template *</
}

function paren_depth(s) {
  return gsub(/\(/, "(", s) - gsub(/\)/, ")", s)
}

function needs_inline(s) {
  if (s !~ /\)[^()]*$/) return 0
  if (s ~ /= *\[/) return 0
  if (s ~ /^((explicit|virtual|static|friend|inline|constexpr|consteval) )*(inline|constexpr|consteval)( |$)/) return 0
  if (s ~ /^(template|requires)( |<)/) return 0
  return 1
}

BEGINFILE { depth = 0; arg_brace = 0; delete class_depth; delete template_depth; reset_stmt() }

{
  line = $0
  sub(/\/\/.*$/, "", line)
  gsub(/"([^"\\]|\\.)*"/, "\"\"", line)
  gsub(/'"'"'([^'"'"'\\]|\\.)*'"'"'/, "'"'"' '"'"'", line)
  if (line ~ /^[ \t]*#/) next

  n = length(line)
  for (i = 1; i <= n; i++) {
    c = substr(line, i, 1)
    if (stmt == "" && c ~ /[ \t]/) continue
    if (stmt == "") stmt_line = FNR
    if (c == "{" && (paren_depth(stmt) > 0 || arg_brace > 0)) {
      arg_brace++
      stmt = stmt c
    } else if (c == "}" && arg_brace > 0) {
      arg_brace--
      stmt = stmt c
    } else if (c == "{") {
      s = normalize(stmt)
      if (is_class_head(s)) {
        class_depth[depth + 1] = 1
        if (is_template_head(s)) template_depth[depth + 1] = 1
      } else if ((depth in class_depth) && !(depth in template_depth) && needs_inline(s)) {
        printf "%s:%d: %s\n", FILENAME, stmt_line, s
        bad++
      }
      depth++
      reset_stmt()
    } else if (c == "}") {
      delete class_depth[depth]
      delete template_depth[depth]
      depth--
      reset_stmt()
    } else if (c == ";") {
      reset_stmt()
    } else {
      stmt = stmt c
    }
  }
  if (stmt != "") stmt = stmt " "
}

END { if (bad) { printf "\n%d in-class definition(s) missing inline\n", bad; exit 1 } }
'

echo "Module inline lint passed."
