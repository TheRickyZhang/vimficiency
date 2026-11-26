# Session storage: active, saved, pinned

A finished result is active in the in-memory workspace (`session/store.lua`),
saved to disk as it finishes, and optionally pinned. The disk side is two
shelves under `stdpath('data')/vimficiency/` (`session/disk.lua`), `sessions/`
and `pinned/`, each one `<filename>.json` per result.

User-facing version: `doc-src/07a-session-storage.md`.

## Indexing

`store.lua` keeps `active_sessions[id]` and `finished_sessions[id]` (finish
moves a record across), plus `named_to_id`, the ordered `recall_id_order` deque,
`last_finished_id` (behind `@`) and `last_suggest_id` (behind `$`, moved by
`auto_suggest` on every new-fingerprint result, shown or not). `convert_alias_to_id` is the one place any alias
kind becomes an id; see `session-invocation.md` § Handles for the kinds.

Aliases and filenames are separate namespaces, and every name or recall alias
is also a valid filename (`@`/`$` are not). The command grammar disambiguates:
`unpin` and `rm` take a filename, and `view`/`explore`/`pin` accept
either via `archive.resolve_result`, which tries the store, then `pinned/`,
then `sessions/`.

`remove` takes an id, never an alias: recall aliases rotate, so an alias could
name a different record by the time it is resolved.

## Saved at finish

- `store.convert_to_finished` calls `disk.save_new(base, result)` when
  `config.sessions.save`, keeps the filename on the finished record as
  `saved_filename`, and passes it to the session log.
- The filename is `<base>-<YYYYmmdd>-<HHMMSS>`, the local time of
  `finished_wall_epoch`. The base is the named alias for mark and watch, and
  `session_type(kind)` (`recall`, `suggest`) otherwise, because a recall alias
  is a position that moves.
- The file is created exclusively (`fs_open` with `"wx"`), and `EEXIST` moves
  on to `-2`, `-3`…. Two Neovims finishing in the same second cannot share a
  filename.
- A failed write warns and the session finishes without a filename
  (`save_new` returns `nil, err`). A file not written whole is unlinked.
- Nothing expires. Auto-suggest finishes a session on every fire, shown or
  not, so `sessions/` gains a file per logged session.

## Pins

- `archive.pin(alias_or_filename, filename)` copies the resolved result to
  `pinned/`. Without `filename` a live alias pins under its record's
  `saved_filename`, and must be given one if it was not saved; a filename
  pins under itself.
- An active alias is refused before resolution, else it would fall through to
  a pin of the same name. Writing over a pin warns (`disk.write_pin`).
- `unpin` deletes from `pinned/` and `rm` from `sessions/`. Neither touches the
  other shelf or the workspace.
- `saved/` held the named saves from before pins, and nothing writes it any
  more. `disk.dir("pinned")` renames one that exists to `pinned/` while
  `pinned/` is absent, checked on every call so a second Neovim finds nothing
  to move.
- The picker's rename and duplicate act on `pinned/` (`disk.rename`,
  `disk.duplicate`). On an alias, `archive.duplicate_active` adds a second one
  for the same result with `store.add_finished`, carrying its `saved_filename`.
- `playback.view` takes a `VF.Session.Viewable`, the part of a result that
  View and the stats preview read. `playback.simulate` resolves an alias or
  filename and calls it.

## JSON schema

The result table excluding the stats log's `query`, plus `version`
(`disk.FORMAT_VERSION`), pretty-printed with
sorted keys: `lines`, `goal_lines`, `start_row/col`, `end_row/col`,
`has_lines_above/below`, `prefix`, `suffix`, `diffs`, `window_height`,
`scroll_amount`, `user_seq`, `user_cost`, `optimal_results`, `key_count`, `finished_wall_epoch` (`os.time()` at finish;
the file's only clock), and
`kind` ∈ `{mark, watch, recall, suggest_idle, suggest_keys, suggest_cost}`.

`disk.decode` accepts v2 and legacy v1. V2 adds `recording`, a starting
cursor/mode and a list of steps with resolved keys, optional differing pressed keys, post-step cursor/mode
and changed line ranges. Coordinates refer to the same slice as `lines`.
View warns when opening a v1 result and shows its two recorded endpoints.
Pinning that result preserves v1. Unknown or missing versions are rejected. All fields
are required except these:
- `analyze_ms`, the time taken to analyze the session.
- `indent` and `filetype`, the buffer's as the session started. View replays
  with that indent and highlights its panes by the filetype.
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
its log record always agree. The record also names the file, as
`saved_filename`, empty when the session was not saved.

### The session log

`sessions.vfs` is a `vf.stats.SessionLog` (`src/LuaExports/proto/stats.proto`),
appended through `vf_append_session` and read only by `vf_stats_summary` and
`vf_stats_runs` (`src/Stats/Summary.cpp`). Each session is logged under the stamp it was scored
with: `SCORING_VERSION` (`src/Versions.cppm`) plus every setting a score depends on
(`stats_log.stamp()`, which the journal sampler sends too). A release that
leaves scoring alone keeps the stamp, so its history stays comparable.

- A new log opens with a `header` record holding `SESSION_LOG_FORMAT`
  (`src/Versions.cppm`). Format 2 requires a query kind and positive size.
  Format 1 records (including headerless logs) remain usable if they contain
  those measurements. Reads and appends report outdated or malformed records
  with their byte offset. A newer format also fails both operations.
- A record carries the stamp's id, an FNV-1a hash of the serialized stamp. The
  full stamp is written once, before the first session under it, so concurrent
  Neovims need no ordering. A duplicate stamp record from a race is harmless.
- Lua never sees the id (LuaJIT has no 64-bit integers). The summary carries
  the current stamp's label, `scoring <N>·<id>` (`scoring_label`).
- Sessions are never rescored: the log keeps no buffer text, and nothing reads
  the saved results back to score them. The stats count every
  stamp's sessions and rollups by default, on the assumption that scoring
  settles as the tool matures; `current_only` keeps the current stamp's (and
  the current params' rollups) and counts the rest in `hidden_runs`.
- The unstamped v1 `sessions.jsonl` is moved to `legacy/sessions.v1.jsonl` on
  the next append and not read.

`vf_stats_summary` (`src/LuaExports/proto/summary.proto`) reduces the sessions
and rollups in scope and the current params' journal samples
(`dev/lua/atom-stream.md` § Sampling). `stats/render.lua` formats the summary into one page, and
`stats/view.lua` lays out the tab and maps its keys. The view
holds the run length (`charted_size`) and Look back unit (`chart_unit`) as
settings alongside weight. Use `h`/`l` on the corresponding selector to
change either setting.

- A score is `Σ w·min(best/user, 1) / Σ w` over scored spans, with `w` the
  user's effort by default (`stats.weight = "effort"`, so it equals
  `Σ min(best, user) / Σ user`) or 1 (`"uniform"`). `unbeaten` spans score
  `best = user`; `no_result` spans are counted, never scored.
- Day buckets break at local midnight. Minute and hour buckets use epoch
  timestamps. The chart period scopes the summary and its drill filters.
  Both sources group retained runs with a positive finite duration and a
  recorded query size into timing buckets for the same period.
- Command tables count each stored command (`Stats::countKeys`): a parser token
  with `f`/`t`/`r` targets collapsed to `_`, an unparsed command as itself, and
  an insert split into its entry command, editing keys such as `<BS>`, and a
  typed-character count. `ce`/`cw` and `cE`/`cW` share a row.
- Each source has its own tables: `SessionSummary.commands` covers the scored
  sessions, `JournalSummary.commands` the scored journal runs of the charted
  size. The two are never added together (`dev/decisions.md`).
- The tables cover the overview chart's Look back buckets and use the
  `scored` outcome. `score` also includes `unbeaten` spans with positive
  user effort. Journal day and week buckets include archived daily command
  tallies. Minute and hour buckets use individual timestamped runs.
- `gj`/`gk` share a row with `j`/`k`, except a journal sample's `within_line`
  commands (`Stats::countWithinLine`): the window cutter marks a `gj`/`gk`
  alone in its tick that moved the cursor without leaving its row.
- A command the optimizer never used is listed in `never`, by the user's
  count, and kept out of `more`.
- The user's insert text is counted from `typed_insert` (`Stats::countInsertKeys`),
  not from the commands, whose keys also hold what mappings typed. A session or
  run without `typed_insert` counts no insert text on either side.

Both exports apply a `vf.stats.Filter` through `src/Stats/Query.cpp`. A row's
`drill` selects matching retained runs. Lua passes the filter from the
rendered item to the run list float in `stats/run_list.lua`.

The stats page is laid out as items (`ui/item_nav.lua`). An item is a block of
rows, optionally a span of display columns, with the cells to highlight.

- `h`/`j`/`k`/`l` move to the nearest item that way; `<CR>` and `<Esc>` act on
  the current one, which is highlighted with the cursor hidden.
- `build_page` stacks a band per source, Sessions over Journal with a rule
  between: its score, chart and optimizer times, and its Commands tables. With the width for it the
  tables go beside the chart, still in one buffer; else under it. A source
  with nothing recorded has no tables.
- Each side is an item `group`, shared by both bands, so `j`/`k` run down a
  column across them. The view's `leave` says where a move off a side's edge
  lands: `l` on the band's first command, `h` on its chart. An item carries
  its band's `source` for that, and to keep ids apart (`sessions:dw (you only)`).
- The cursor always rests inside the current item, so the item is found from
  the cursor; the remembered id only carries it across a redraw.
- `<CR>` expands or collapses a command list, or opens a chart or run list
  in a float. `chart_page` draws the chart to the float's size and starts on
  the most recent history bucket or the smallest timing bucket.
- The stats keep a section to its headline (the Sessions score, the Journal's
  run lengths), its chart and its optimizer times. `chart_page` puts the rest under the chart: the
  counts, the scored share, and every row of the section that lists runs.
- The overview uses 30 history buckets. `+`/`-` in a history float change
  its own range without changing the overview's Look back setting.
  Timing floats group searches into fixed size buckets.
- Both history floats draw scored runs by kind and query size from the same
  `RunBreakdown`. The session log keeps these measurements in `query`.
  Each selectable segment opens its source's runs.
  A `TimingBucket`'s `score` and `drill` cover its scoring runs, timed or not,
  while its `timing` covers the timed ones of any outcome.
- A kind's rolled-up runs kept no query size. They are drawn as the rest of
  its bar and are no item, since no filter lists them.
- The run list is the one page a drill opens, from any float: `view.lua`'s
  `list_runs` hands `stats/run_list.lua` the item's `drill`, `label` and
  `order`. A new breakdown adds items with a `drill`, never a page of its own.

- Command filters need individual retained runs. Eligible rolled-up runs
  are reported separately in `rollup_runs_without_commands` because their
  tallies cannot identify which individual runs used the command.
- The charted size is the filter's window size, else the request's
  `charted_size`, else the middle one. No drill sets a window size, so a drill
  made before the run length changes follows the switch. Window size 0 is the charted size
  everywhere but the per-size scores.
- Run rows carry `user_display`/`best_display` (`Stats::displayKeys`): each
  stretch of typed characters as `⟨n⟩`, line breaks and every other key as the
  sequence display renders them. Nothing in Lua reads them: the run list's
  preview renders the keys themselves, cut to two lines. Those are the looked-up
  run's `user_seq`, which for a journal run is the detail's resolved `keys`;
  they tokenize where typed keys such as `gj` do not.
- `vf_stats_runs` pages the sessions in scope and the charted runs of the
  current params' samples by one `Order` column, largest first unless
  `ascending`; a run with no value in that column goes last either way, and
  ties go newest first, then by id. Rolled-up runs count in `total` but have
  no rows.
- A journal run's `RunId` is its file, window size and `last_atom`; a session's
  is its index among the log's sessions. A session's row also carries its
  `saved_filename`, which is what the user sees and types.
- `vf_stats_run` (`Journal::runDetail`) cuts a journal run's window again under
  the request's `sample_params` and returns it sliced as its search saw it
  (`Journal::sliceWindow`, shared with `evaluate`); `keys` are the sliced
  `userSeq`, gj/gk approximated as in a session's `user_seq`. A session's id, a
  run without a slice, a deleted journal, or params that no longer cut that
  window are errors, which `ffi/stats.run` returns rather than raises.
- `stats/query.run(row)` is the one lookup that differs by kind, and returns a
  `VF.Session.Viewable` or `nil, err`. A session is `disk.load("sessions",
  row.saved_filename)`; a journal run is the `vf_stats_run` detail, with the
  row's cost and best sequence as `user_cost` and `optimal_results`.
- The run list caches lookups by row until it lists again. The preview opens
  with the run's id (`render.run_name`), then shows `lines` beside
  `goal_lines`, or the lookup's error.
- `<CR>` hands the run to `playback.view`, so a session replays with every
  sequence it found and a journal run with its best, a step per complete
  recorded tick. `filetype` highlights only the visible panes.
- `p` pins the current row's saved session through `archive.pin`, asking for
  the filename. A journal run has no kind, diffs or window metrics to make a
  full result, so it cannot be pinned.

`diffs` is the planner's own partition (half-open, 0-indexed spans per side), so
the viewer highlights exactly the regions the search ran against; `disk.validate`
only type-checks it. `prefix`/`suffix` are always empty at whole-buffer scope
(see `../core/boundary-logic.md`).
