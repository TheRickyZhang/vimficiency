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

The result table plus `version` (`disk.FORMAT_VERSION`), pretty-printed with
sorted keys: `lines`, `goal_lines`, `start_row/col`, `end_row/col`,
`has_lines_above/below`, `prefix`, `suffix`, `diffs`, `window_height`,
`scroll_amount`, `user_seq`, `user_cost`, `optimal_results`, `key_count`, `finished_wall_epoch` (`os.time()` at finish;
the file's only clock), and
`kind` ∈ `{mark, watch, recall, suggest_idle, suggest_keys, suggest_cost}`.

`disk.decode` rejects any `version` other than the current one, including
unversioned files, and strips it before validating; there is no migration.
Bump `FORMAT_VERSION` on any change that old files cannot satisfy. All fields
are required except these:
- `analyze_ms`, debug-only.
- `user_typed`, the pressed keys behind `user_cost` (`user_seq` is what ran).
- `user_typed_insert`, the insert text in `user_typed` (`atom.typed_insert`,
  see `atom-stream.md`). The session log copies it as `typed_insert`.
- `optimizer_overrides` and `results_calculated`, which complete the replayable
  `VF.SearchRequest` and are absent from files saved before 0.3.0.
- `commands`, the `{keys, kind}` atoms `user_seq` concatenates, and `unbeaten`,
  both absent from older files. Older files may carry a debug `capture_debug`
  instead of `commands`.

The persisted clock is wall time because `hrtime` values from another boot are
not comparable; the workspace keeps its own `*_mono_ns` stamps on the record
instead. The session log (`stats/log.lua`) copies this value into its
`finished_wall_epoch` rather than reading the clock again, so a saved file and
its log record always agree.

### The session log

`sessions.vfs` is a `vf.stats.SessionLog` (`src/LuaExports/proto/stats.proto`),
appended through `vf_append_session` and read only by `vf_stats_summary` and
`vf_stats_runs` (`src/Stats/Summary.cpp`). Each session is logged under the stamp it was scored
with: `SCORING_VERSION` (`src/Versions.cppm`) plus every setting a score depends on
(`stats_log.stamp()`, which the journal sampler sends too). A release that
leaves scoring alone keeps the stamp, so its history stays comparable.

- A new log opens with a `header` record holding `SESSION_LOG_FORMAT`
  (`src/Versions.cppm`); a log without one is format 1. A newer format fails
  both the summary and the append, rather than being misread.
- A record carries the stamp's id, an FNV-1a hash of the serialized stamp. The
  full stamp is written once, before the first session under it, so concurrent
  Neovims need no ordering. A duplicate stamp record from a race is harmless.
- Lua never sees the id (LuaJIT has no 64-bit integers). The summary carries
  the current stamp's label, `scoring <N>·<id>` (`scoring_label`).
- Sessions are never rescored: they keep no buffer text. The stats count every
  stamp's sessions and rollups by default, on the assumption that scoring
  settles as the tool matures; `current_only` keeps the current stamp's (and
  the current params' rollups) and counts the rest in `hidden_runs`.
- The unstamped v1 `sessions.jsonl` is moved to `legacy/sessions.v1.jsonl` on
  the next append and not read.

`vf_stats_summary` (`src/LuaExports/proto/summary.proto`) reduces the sessions
and rollups in scope and the current params' journal samples
(`dev/lua/atom-stream.md` § Sampling). `stats/render.lua` formats the summary into pages, and
`stats/view.lua` lays out the tab and maps its keys. The view
holds the charted window size (`charted_size`, cycled by `l`) as a setting like
the weight, not a filter.

- A score is `Σ w·min(best/user, 1) / Σ w` over scored spans, with `w` the
  user's effort by default (`stats.weight = "effort"`, so it equals
  `Σ min(best, user) / Σ user`) or 1 (`"uniform"`). `unbeaten` spans score
  `best = user`; `no_result` spans are counted, never scored.
- Days are local (`localtime_r`), so the 30-day charts break at local midnight.
  Dev timing covers sessions of the last 30 days.
- Command tables count each stored command (`Stats::countKeys`): a parser token
  with `f`/`t`/`r` targets collapsed to `_`, an unparsed command as itself, and
  an insert split into its entry command, editing keys such as `<BS>`, and a
  typed-character count. `ce`/`cw` and `cE`/`cW` share a row. They cover scored
  sessions and the journal runs of the charted size.
- `gj`/`gk` share a row with `j`/`k`, except a journal sample's `within_line`
  commands (`Stats::countWithinLine`): the window cutter marks a `gj`/`gk`
  alone in its tick that moved the cursor without leaving its row.
- A command the optimizer never used is listed in `never`, by the user's
  count, and kept out of `more`.
- The user's insert text is counted from `typed_insert` (`Stats::countInsertKeys`),
  not from the commands, whose keys also hold what mappings typed. A session or
  run without `typed_insert` counts no insert text on either side.

Both exports narrow by one `vf.stats.Filter` (`src/Stats/Query.cpp` applies it)
and read the same scope, so a row's `drill` filter lists exactly the runs it
counted. Lua copies `drill` verbatim and never builds a filter: `build_pages`
returns each drillable row and daily chart column with its `drill`, and the
view opens `stats/run_list.lua`'s float over the one under the cursor.

- A field a source lacks leaves it out: a window size or nav/edit leaves
  sessions out; commands leave rollups out, counted as
  `rollup_runs_without_commands`.
- The charted size is the filter's window size, else the request's
  `charted_size`, else the middle one. No drill sets a window size, so a drill
  made before `l` follows the switch. Window size 0 is the charted size
  everywhere but the per-size scores.
- Run rows carry `user_display`/`best_display` (`Stats::displayKeys`): each
  stretch of typed characters as `⟨n⟩`, line breaks and every other key as the
  sequence display renders them. Nothing in Lua reads them: the run list's
  preview renders the keys in full.
- `vf_stats_runs` pages the sessions in scope and the charted runs of the
  current params' samples by one `Order` column, largest first unless
  `ascending`; a run with no value in that column goes last either way, and
  ties go newest first, then by id. Rolled-up runs count in `total` but have
  no rows.
- A journal run's `RunId` is its file, window size and `last_atom`; a session's
  is its index among the log's sessions.
- `vf_stats_run` (`Journal::runDetail`) cuts a journal run's window again under
  the request's `sample_params` and returns it sliced as its search saw it
  (`Journal::sliceWindow`, shared with `evaluate`); `keys` are the sliced
  `userSeq`, gj/gk approximated as in a session's `user_seq`. A session's id, a
  run without a slice, a deleted journal, or params that no longer cut that
  window are errors, which `ffi/stats.run` returns rather than raises.
- `<CR>` on a journal run feeds the detail to `simulate.simulate_compare`
  (`filetype` highlights only the visible panes). The preview shows the same
  detail's `lines` beside its `goal_lines`; a session has neither.

`diffs` is the planner's own partition (half-open, 0-indexed spans per side), so
the viewer highlights exactly the regions the search ran against; `disk.validate`
only type-checks it. `prefix`/`suffix` are always empty at whole-buffer scope
(see `../core/boundary-logic.md`).

A fetched file goes straight into `finished_sessions` with the file's `kind`
and `finish_alias` set to the target alias.
