# Session storage: workspace (memory) vs. archive (disk)

A finished result lives in the in-memory workspace (`session/store.lua`) and/or
the on-disk archive (`session/archive.lua`, files at
`stdpath('data')/vimficiency/saved/<name>.json`). User-facing version:
`doc-src/07a-session-storage.md`.

## Indexing

`store.lua` keeps `session_records[id]` as the canonical table over a session's
whole lifecycle, plus `manual_alias_to_id`, the ordered `recall_id_order` deque,
`last_finished_id` (behind `@`) and `last_suggest_id` (behind `$`, set only by
`auto_suggest` on a toast). `store.resolve_special` is the one place that maps
`@`/`$` to `(id, result)`.

Aliases and saved names are separate namespaces (`alias.is_valid_manual` is
`^%a+$`, `alias.is_valid_saved_name` is `^[%w_][%w._-]*$`). The command grammar
disambiguates: `save`/`store` take selectors, `fetch`/`rm` take filenames, and
`play`/`explore`/`view` accept either via `archive.resolve_result`, which tries
memory first.

## Moving between the two

- `save` copies workspace → disk; `store` moves (removes the workspace record).
  Both overwrite an existing file with a WARN (`disk.write_with_overwrite_warn`).
- `fetch` copies disk → workspace and refuses an occupied alias
  (`register_fetched_result` returns `nil, err`). The file is never touched.
- `playback.simulate` is workspace-first with a disk fallback and an implicit
  fetch for alphabetic names.
- `remove` takes an id, never an alias: recall aliases rotate, so an alias could
  name a different record by the time it is resolved.

## JSON schema

`vim.json.encode(result, { indent = true })` of the result table: `lines`,
`goal_lines`, `start_row/col`, `end_row/col`, `has_lines_above/below`,
`prefix`, `suffix`, `diffs`, `user_seq`, `user_cost`, `optimal_results`,
`start_time`, `key_count`, `timestamp`, `finish_reason` ∈
`{manual, watch_idle, suggest_idle, suggest_keys, suggest_cost}`.

`diffs` is the planner's own partition (half-open, 0-indexed spans per side), so
the viewer highlights exactly the regions the search ran against. It is
recomputable from `lines`/`goal_lines`, so `disk.validate` only type-checks it
and the viewer recomputes when an older file lacks it. `prefix`/`suffix` are
always empty at whole-buffer scope (see `../core/boundary-logic.md`).

A fetched file becomes a finished record with `start_kind = end_kind = "manual"`,
`finish_alias` set to the target alias, and no `stream_sub`.
