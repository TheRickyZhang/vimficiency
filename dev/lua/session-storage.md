# Session storage: workspace (memory) vs. archive (disk)

A finished result lives in the in-memory workspace (`session/store.lua`) and/or
the on-disk archive (`session/archive.lua`, files at
`stdpath('data')/vimficiency/saved/<filename>.json`). User-facing version:
`doc-src/07a-session-storage.md`.

## Indexing

`store.lua` keeps `active_sessions[id]` and `finished_sessions[id]` (finish
moves a record across), plus `named_to_id`, the ordered `recall_id_order` deque,
`last_finished_id` (behind `@`) and `last_suggest_id` (behind `$`, moved by
`auto_suggest` on every new-fingerprint result, shown or not). `convert_alias_to_id` is the one place any alias
kind becomes an id; see `session-invocation.md` § Handles for the kinds.

Aliases and filenames are separate namespaces, and every name or recall alias
is also a valid filename (`@`/`$` are not). The command grammar disambiguates:
`save`/`store` take an alias and an optional filename, `fetch`/`rm` take a
filename, and `play`/`explore`/`view` accept either via
`archive.resolve_result`, which tries the store first.

## Moving between the two

- `save` copies workspace → disk; `store` moves (removes the workspace record).
  Both overwrite an existing file with a WARN (`disk.write_with_overwrite_warn`).
  The filename defaults to the alias, or for `@`/`$` to the alias the record was
  finished under; a suggest result gets `suggest-<timestamp>` instead, since its
  recall alias would collide on every fire (`archive.default_filename`).
- `fetch` copies disk → workspace and refuses an occupied alias
  (`register_fetched_result` returns `nil, err`). The file is never touched.
- `playback.simulate` is workspace-first with a disk fallback and an implicit
  fetch for name aliases.
- `remove` takes an id, never an alias: recall aliases rotate, so an alias could
  name a different record by the time it is resolved.

## JSON schema

`vim.json.encode(result, { indent = true })` of the result table: `lines`,
`goal_lines`, `start_row/col`, `end_row/col`, `has_lines_above/below`,
`prefix`, `suffix`, `diffs`, `user_seq`, `user_cost`, `optimal_results`,
`key_count`, `finished_wall_epoch` (`os.time()` at finish; the file's only
clock), `kind` ∈ `{mark, watch, recall, suggest_idle, suggest_keys, suggest_cost}`.

`finished_wall_epoch` replaced the monotonic `start_time`/`timestamp` pair
(2026-11-11): `hrtime` values from another boot are not comparable, so the
workspace keeps its own `*_mono_ns` stamps on the record instead. The stats log
(`stats/log.lua`) copies this value into its `finish_epoch` rather than reading
the clock again, so a saved file and its log row always agree.

`kind` replaced `finish_reason` (2026-11-11); files written before that carry
neither, and `fetch` registers them as `mark`.

`diffs` is the planner's own partition (half-open, 0-indexed spans per side), so
the viewer highlights exactly the regions the search ran against. It is
recomputable from `lines`/`goal_lines`, so `disk.validate` only type-checks it
and the viewer recomputes when an older file lacks it. `prefix`/`suffix` are
always empty at whole-buffer scope (see `../core/boundary-logic.md`).

A fetched file goes straight into `finished_sessions` with the file's `kind`
and `finish_alias` set to the target alias.
