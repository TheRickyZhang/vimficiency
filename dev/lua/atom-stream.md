# Atom stream (`capture/atom_stream.lua`)

Read before changing capture, the admission rule, recall snapshotting, or any
listener (`capture/recall.lua`, `session.lua`, `capture/end_trigger.lua`).
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
- `keys` — replayable sequence. `nil` is a Lua callback, `""` an unexpressible resolution.
- `lhs`, `count` — as typed; `count` drives synthetic-count stripping.
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

`skip` is a measured no-effect fact, not a tolerance: it is what lets `:w`,
`:noh`, and `:Vimfy` commands pass through a live session. `abort` means the
recording can no longer be replayed to the end state, so every listener drops
what it was recording: the named-session listener removes its session, and the
recall listener removes the buffer's active recall records
(`store.abort_recall`). An aborting atom also carries `abort_reason`, a
one-line description (`describe_abort`) naming the ex command, mouse token, or
mapping `lhs` and whether it changed text or moved the cursor; the
named-session listener surfaces it in the drop notice.

## Deferred delivery

Atoms are delivered when control returns to the main loop, so a typing burst
arrives back to back with the buffer already at its final state. A recall record
at atom `i` therefore takes its start state from the snapshot kept after atom
`i-1`; `store.lua` shifts one `recall_prev_state[buf]` forward per atom, and the
first atom in a buffer only seeds it. Records and the snapshot are per buffer:
an atom extends only the records of its own buffer, and an abort reseeds the
snapshot from the post-abort state so the next record starts from text the
chain can actually reach.

## Testing

`nvim -l` never flushes `CmdAtom`, so the live wiring test
(`tests/lua/capture/atom_stream.lua`) is a standalone async script run as its own
`run.sh` phase. The pure decision table is tested through `atom_stream._classify`.
