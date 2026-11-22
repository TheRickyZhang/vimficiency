# Atom stream (`capture/atom_stream.lua`)

Read before changing capture, the admission rule, recall start marks, or any
listener (`capture/recall.lua`, `capture/journal.lua`, `session.lua`,
`capture/end_trigger.lua`).
`key_tracking.lua` is Explore-only; see `neovim_on_key_issues.md`.

## Model

Neovim's `CmdAtom` autocmd (0.13-dev) fires once per atomic user action, after
it runs, with the resolved post-mapping `keys`. `atom_stream.lua` owns the one
autocmd, normalizes each event into a `VF.Atom`, attaches a decision, and pushes
both to every named listener. It stores nothing; each listener keeps its own
atoms and does its own buffer/window scoping.

`add_listener(name, fn)` lazily creates the autocmd; `remove_listener(name)`
deletes it when the last listener leaves. A name collision returns `false` so a
double install is loud. `setup()` raises when `##CmdAtom` is absent.

## Event fields consumed

- `type` — `operator | insert | motion | normal | visual | jump | scroll | excmd | mouse | mapping`.
  Forwarded as `VF.Atom.kind`; journals and finished sessions keep it per command.
- `keys` — replayable sequence. `nil` is a Lua callback, `""` an unexpressible resolution.
  Keys that embed a callback (`keynorm.has_callback`) are treated as `nil`.
- `lhs`, `count` — the mapping side as typed; `count` drives synthetic-count
  stripping.
- `changed`, `moved` — whether the action touched text / cursor.

`reg` and `undoseq` are never read.

## Synthetic count-1

Vim folds a synthetic `1` onto uncounted commands (`ifoo` arrives as `1ifoo`,
`count == 1`). Genuine counts arrive as `>= 2` or `nil`; uncounted `J` arrives
as `2J` by Vim's own join semantics. `strip_synthetic_count` drops a leading `1`
only when `count == 1`, so `i1` → `1i1` → `i1` keeps its content digit.

## Admission decision

An atom is replayable when `keys` is neither `nil` nor `""`, the type is not
`excmd`, and the keys are not a mouse token. Replayable → `record`. Otherwise
`abort` if `changed` or `moved`, else `skip`.

`keys == nil` is a Lua-callback mapping (flash.nvim's `f`, a `vim.keymap.set`
function). With `config.lossy_mappings` set to `"warn"` or `"allow"` the
effectful ones are recorded under their typed `lhs` instead
(`lossy_passthrough`; the count is re-prefixed because `lhs` omits it) and the
atom is flagged `lossy`. Under `"warn"` (the default) the named-session
listener notifies once per session; `"allow"` is silent; recall records them
silently in both. `"drop"` aborts instead. There is no
general way to verify such an atom — `;` after flash's `f` would need the
previous find, and every mapping would need its own state — so the choice is
the user's. No-effect callbacks stay `skip` either way.

An operator over such a mapping does arrive with `keys`, but they hold the
callback instead of keys: `d<K_LUA>538<NL>e` for flash's `dfe` (a function ref
only this Neovim can run), `c<Cmd>lua MiniAi…<NL>` for mini.ai's `ci(`.
`dispatch` drops them to `nil` so they take the same `lhs` path; `excmd` atoms
are left alone because they are never replayable.

A mapped move inside a selection (`j`→`gj` after `V`) arrives twice: once
alone as a `mapping` atom with nil keys, then within the Visual atom's keys.
`dispatch` treats the first as a no-op when the latest mapping ran in Visual
mode, so it is a `skip`. A burst that mixes a normal-mode Lua mapping with a
later Visual one can still misjudge it.

The recorded `lhs` is only as good as Neovim's (see "Pressed keys" for its
gaps), so a lossy atom's `keys` can miss a count or a Visual `v`. Its `typed`
does not.

A Lua Insert-mode mapping that inserts through the API (nvim-ts-autotag's `>`)
leaves its text out of a non-nil `keys`, so the atom is recorded, not lossy,
and replay drops that text. Only the play view's user pane replays `keys`, so
this is accepted until Neovim marks such atoms.

`skip` is a measured no-effect fact, not a tolerance: it is what lets `:w`,
`:noh`, and `:Vimfy` commands pass through a live session. `abort` means the
recording can no longer be replayed to the end state, so every listener drops
what it was recording: the named-session listener removes its session, and the
recall listener removes the buffer's active recall records
(`store.abort_recall`). An aborting atom also carries `abort_reason`, a
one-line description (`describe_abort`) naming the ex command, mouse token, or
mapping `lhs` and whether it changed text or moved the cursor; the
named-session listener surfaces it in the drop notice.

## Pressed keys

`atom.typed` is what the user physically pressed for the atom, where
`atom.keys` is what ran. The user is costed by `typed`; replay, state and the
search bound use `keys` (`dev/decisions.md`).

Neovim has no single source of them (measured on 0.13-dev-2069).
`vim.on_key`'s `typed` is exact for every key the main loop reads, but skips
keys a plugin reads with `getchar()` (flash's target, mini.ai's `iw`). CmdAtom
`lhs` is a rebuilt label that drops counts and Visual prefixes, and later
mappings overwrite it.

So `atom_stream` records what Neovim read as typed, in order:

- `on_key`'s `typed`
- what `vim.fn.getchar`/`getcharstr` return, through wrappers (peeks with
  `getchar(1)` excluded)
- keys passed to `vim.api.nvim_feedkeys`/`vim.fn.feedkeys` with the `t` flag,
  marked as fed

A plugin that feeds keys as typed makes Neovim read them again. which-key
does it for every trigger (`d`, `c`, `y`, `g`, `z`, `"`, `v`): it reads the
next key with `getcharstr` and re-feeds it with `"mit"`. With no fed keys, the
reads are the answer. Otherwise `logic::typedKeys` cancels each read against
the next fed key. A read that doesn't match means the fed keys were consumed
unreported, so the rest are dropped.

The wrappers are installed on the first listener and never removed, since
unwrapping could drop a later plugin's wrapper around ours. They only record
while the autocmd exists. Under the real config (flash, mini.ai, mini.pairs,
mini.surround, which-key) 43 of 43 probed sequences came out exact.

Known gaps, all Neovim's:

- A burst is delivered after all its keys were read, so its first atom carries
  the whole burst and the rest carry `""`. With a key held down, a key can also
  be read while the previous atoms are still being delivered and land one atom
  early. Totals are conserved, so sum `typed` over a run; never compare it atom
  by atom.
- An expr mapping that calls `getchar()` gets no `typed` from `on_key`, so its
  own keys are lost (mini.ai's `i` without which-key in front: `yiw` records
  `yw`). Neovim clears `on_key`'s buffer inside the nested `getchar()`.
- Vimscript `getchar()`/`feedkeys()` callers, and Lua that saved the function
  before the wrappers went in, are not seen.
- Keys that end in no atom (an operator cancelled with `<Esc>`) count toward
  the next one. Terminal-mode keys are ignored, since nothing would bound them.

## Deferred delivery

Atoms are delivered when control returns to the main loop, so a typing burst
arrives back to back with the buffer already at its final state. A recall record
at atom `i` therefore takes its start state from the mark kept after atom
`i-1`; `store.lua` shifts one `recall_prev_state[buf]` forward per atom, and the
first atom in a buffer only seeds it.

A mark is the cursor/viewport (`util.capture_view`) plus a `buffer_history`
sequence number, not a copy of the text. `capture/buffer_history.lua` keeps one
shadow copy per tracked buffer, as of its last mark. `on_lines` only widens one
pending range, so its cost does not grow with the buffer or the event count.

`buffer_history.mark(buf)` folds that range into the shadow as a single delta:
the lines it replaced. `lines_at(buf, seq)` rebuilds the start text only when a
record is finished, undoing at most one delta per atom in the ring.

The ring trims that history to the buffer's oldest active record, and an abort
trims it to the new mark. An atom that edits two distant places stores every
line between them.

A pending range that disagrees with the buffer's line count is replaced by the
whole buffer. Deleting every line is the known case: Neovim reports zero lines
left while the buffer shows one empty line.

Records and the mark are per buffer: an atom extends only the records of its
own buffer, and an abort reseeds the mark from the post-abort state so the next
record starts from text the chain can actually reach. A reload or an unload
invalidates earlier sequence numbers, so `buffer_history` resets and the store
drops that buffer's active recall records.

A record whose start falls inside a burst still gets the burst's final text as
its start, because every mark in the burst is taken after it.

## Journal

`capture/journal.lua` (`config.journal.enabled`, off by default) appends every
in-scope atom to `stdpath('data')/vimficiency/journal/<timestamp>-<pid>.vfj`,
one file per `setup()`. It shares recall's scope (`atom_stream.in_live_file_buffer`)
but keeps `skip` and `abort` atoms, with their decision, so a reader can cut
sessions itself. That scope leaves out an atom delivered after its buffer left
every window: it arrives in Neovim's temporary autocmd window, whose buffer may
be unloaded. Marking an unloaded buffer would seed an empty shadow that
`nvim_buf_attach` can never update, so `buffer_history` refuses it.

Buffer text is exact, not replayed from keys: the journal is the
`buffer_history` observer, so it receives the seed text and every folded delta
whichever listener's `mark` produced them. A snapshot is written on the first
atom in a buffer, after a reload or unload (the reseed), and every
`snapshot_every` atoms; the last is only a checkpoint that bounds how far back a
reader must scan.

The file is protobuf (`proto/journal.proto`): one single-record `vf.journal.File`
appended per record, so the whole file decodes as one `File`. It is binary
because the protos are `LITE_RUNTIME`, which has no JSON, and both Lua and C++
have to read it. Rows and line indexes are 0-indexed.

Every record has `ms` (monotonic milliseconds since the header) and `tick`.

- `header` — `version`, `started_wall_epoch`. A second header in one file is a
  restart: discard all buffer state.
- `snapshot` — `buf`, `bufname`, `filetype`, `lines` (the whole text).
- `delta` — `buf`, `first`, `old_count`, `lines`: replace `old_count` lines at
  `first` with `lines`. Applies to the text built so far for `buf`.
- `resync` — `buf`, `win`, `view`: the state of a window that no atom accounts
  for, taken in Normal mode once the event queue drains. Written when a window
  or buffer is entered, or the cursor or text changed with no atom behind it.
- `atom` — `buf`, `win`, `keys`, `kind` (empty in older journals), `typed`,
  `decision`, `lossy`, `abort_reason` (abort only), `mode`, and the post-atom `view`.

The text at an atom is the snapshot plus every delta written before it. Records
written in one drain of the event queue share a `tick`: deferred delivery means
every atom of a tick carries the tick's final cursor and the first carries the
whole delta. Only the last state of a tick is real, so a reader never splits one.

The raw event fields that `VF.Atom` drops (`lhs`, `count`, `changed`, `moved`)
are not in the journal. Files are never rotated; the sampler deletes old ones
once rolled up (§ Sampling).

### Sampling

The journal is scored by `vimfy_journal` (`src/Journal/`), which
`stats/sampling.lua` runs in the background at the lowest priority: once
`STARTUP_DELAY_MS` after setup, then on `CursorHold` when the `.vfj` files have
grown, at most every `RUN_INTERVAL_MS` (`journal.sampling.auto` turns it off).
It is a separate process, not a thread, because a failed `CHECK` in a search
aborts; there that costs one window, recorded as `crashed` and skipped on the
next run. Stats never start or wait for a run.

`Replay` rebuilds the text at every atom and `WindowCutter` cuts back-to-back
windows of `journal.sampling.window_sizes` recorded atoms, one series per size.
A window only grows across ticks the journal vouches for end to end, and
anything else ends every open window:

- a `resync`, a header, or text the deltas cannot explain
- an `abort` atom that shows in the text or cursor (saving a modified buffer is
  an `abort` that does not, because it bumps `changedtick`)
- an atom of another buffer or window, or a state outside Normal mode
- a cursor row past the end of the text, which only a capture bug leaves behind
- a pause longer than `idle_gap_ms`

`Evaluate` runs each window through the same `sliceSearch` / `searchInputs` /
`analyze` path as a live session (`src/Optimizer/Analyze.cppm`), costing the
window's pressed keys against the best sequence found. Nothing is replayed,
since the journal holds the real state at both ends.

Results go to `<journal>.samples`, a `vf.journal.SampleFile` appended the same
way. Each sample keeps the window's commands (`{keys, kind}`) and the best
sequence found. The journal's last tick is never cut, which keeps the windows of
a growing journal a prefix of its later windows and lets a rerun resume by count.

A samples file is current when its stored `SampleParams` equal the request's,
completed by `stampSampleParams` (`src/LuaExports/Samples.cppm`): the stamp of
`dev/lua/session-storage.md` § The session log, plus `SAMPLES_FORMAT`. Bump
that when a sample gains content, so older files are redone. The sampler
recomputes a file that is not current. `:Vimfy stats` reads samples through
`vf_stats_summary` and `vf_stats_runs` (`src/Stats/`), which skip such files
until then, so two scorings never mix. It also reports the journal bytes the current samples cover,
shown as progress.

The library keeps every samples file it has read in memory, as `Stats::Run`s
(`src/Stats/JournalRuns.cppm`), and parses only what was appended since; a file
that shrank or no longer starts with the same record is read again.

At the end of a pass, a journal unmodified for `journal.keep_days` whose
samples cover all of it under the current params is rolled up: its samples are
reduced per local day, window size, nav/edit and outcome (`Stats::RunTotals`,
`src/Stats/Rollup.cppm`) into a `vf.journal.Rollup` appended to `rollup.vfr`,
then the journal and its samples are deleted. Rollups merge exactly, so the
summary feeds them and samples through one reduction. They keep the params they
were scored under and are never rescored: under other params they only count as
`other_rollup_runs`.

- The rollup names its journal, and a journal already rolled up is only deleted:
  a pass that dies between the append and the deletion is finished by the next,
  and the summary skips the journal meanwhile.
- `keep_days` is not a param, so changing it rescores nothing.

Windows are scored like sessions (`dev/lua/session-storage.md` § The session
log). Windows that end where they started score a best of 0. `unbeaten` windows
(`AnalyzeOutput::unbeaten`: the search drained with only effort-cap drops, as
after `p`, `u` or `:s`) score `best = user` and are counted apart. `too_large`,
`no_result`, `unsupported` and `crashed` windows are counted but carry no cost.

## Testing

`nvim -l` never flushes `CmdAtom`, so the live wiring test
(`tests/lua/capture/atom_stream.lua`) is a standalone async script run as its own
`run.sh` phase. The pure decision table is tested through `atom_stream._classify`.
`tests/lua/capture/journal.lua` drives the journal listener directly and checks
that replaying the file rebuilds the buffer at every atom.
`tests/Unit/Journal/` covers replay, each cut rule, evaluation and resume;
`tests/lua/stats/sampling.lua` runs the real `vimfy_journal` over a journal
written in the test.
