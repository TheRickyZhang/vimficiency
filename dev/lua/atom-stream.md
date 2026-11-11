# Atom stream (`capture/atom_stream.lua`)

Read before changing capture, recall snapshotting, the admission policy, or any
subscriber (`capture/recall.lua`, `session.lua` manual capture,
`capture/end_trigger.lua`). Supersedes `neovim_on_key_issues.md` for
manual/recall/trigger capture; `key_tracking.lua` lives on only for interactive
Explore.

## Why CmdAtom

Capture runs off Neovim's `CmdAtom` autocmd (0.13-dev / nightly;
`vim.fn.exists("##CmdAtom") == 1`). The event fires once per atomic user action,
*after* the action runs, reporting the resolved post-mapping result rather than
raw keystrokes.

This dissolves every `vim.on_key` hazard the old layer fought. Operator-pending
motions no longer double-fire, mapping LHS bytes no longer leak into recordings,
single-key triggers need no retraction, and text objects carry their final char
(`ciw` is one atom, not `c i ?`). A `:Vimfy` keymap resolves to an inert
excmd/no-keys atom and drops out on its own — the old `mapping_scan` warning was
deleted with the migration.

## Event shape

The autocmd's `ev.data` carries the fields `atom_stream.lua` consumes:

- `type` — `operator | insert | motion | normal | visual | jump | scroll | excmd | mouse | mapping`.
- `keys` — the replayable post-mapping sequence (notation). `nil` means a lossy
  Lua callback, `""` an unreplayable resolution.
- `lhs` — the as-typed LHS (notation).
- `count` — the command's count; drives synthetic-count stripping.
- `changed`, `moved` — whether the action modified buffer text / moved the cursor.

Only `keys` is forwarded on `VF.Atom`, alongside the buffer/window scope. The
other event fields are consumed for the decision and synthetic-count stripping
and then dropped; `reg` and `undoseq` are never read.

Standard autocmd `ev.buf` carries the buffer; the focused window is read at
dispatch time.

## Synthetic count-1 prefix

Vim folds a *synthetic* count of `1` onto commands the user gave no count to:
`ifoo` arrives as `1ifoo` with `count == 1`, likewise `A`→`1A`, `rX`→`1rX`.
Genuine counts arrive as `count >= 2` (or `nil`), e.g. `2dd`, and uncounted `J`
arrives as `2J` (Vim's own join semantics, not a synthetic prefix).

`strip_synthetic_count` drops a leading `1` only when `count == 1`, so replay and
effort match what the user typed. Stripping just the first char keeps content
digits intact (`i1` → `1i1` → `i1`).

## Admission decisions

`classify` returns one of three decisions from the atom's intrinsic fields.
Buffer/window scoping is the subscriber's job (it compares against its own
session), never decided here.

- `record` — replayable keys; append to in-scope recordings.
- `skip` — unreplayable but provably inert: neither changed text nor moved the cursor.
- `abort` — unreplayable and had an effect; an in-scope session can no longer be trusted.

One rule: an atom is replayable when it carries real keys (`keys` is neither
`nil` nor `""`), is not an `excmd`, and is not a mouse token. Replayable records.
Anything else aborts if Neovim reports `changed` or `moved`, and skips otherwise.
The skip row is what lets `:w`, `:noh`, and `:Vimfy` commands pass through a live
session; it is a measured no-effect fact, not a tolerance.

## Subscriber fan-out

`add_listener(name, on_atom)` registers a named listener and lazily creates the
autocmd in augroup `VimficiencyAtoms`; `remove_listener(name)` removes the autocmd
when the last subscriber leaves. A name collision returns `false` so a
double-install surfaces instead of silently shadowing.

`add_listener` fails closed (returns `false`) when `##CmdAtom` is absent, so
watch / suggest / recall degrade to a no-op on pre-0.13 Neovim rather than
erroring. `setup()` emits one warning in that case.

## Deferred delivery and recall snapshot-shift

The event is *deferred*. Under fast typeahead, every atom for a burst of input
arrives back-to-back once control returns to the main loop, by which point the
buffer is already at its final state.

A recall record created at atom `i` therefore needs the buffer state that
*preceded* it, which is the state captured right after atom `i-1`. `store.lua`
keeps one `recall_prev_state[buf]` snapshot and shifts it forward each atom: the
first atom in a buffer seeds the snapshot without spawning a record (recalling
"before the very first command" is not meaningful), and each later atom spawns a
record framed by the shifted predecessor state.

The burst case has a known limitation: when many atoms land in one drain, every
record's `start_state` is reconstructed from the shift chain, not snapshotted
live. This is accepted — recall targets command boundaries, and the shift chain
reproduces them.

## Test harness constraint

`CmdAtom` is deferred and only flushes when Neovim's main loop runs. `nvim -l`
never consumes `nvim_input` typeahead and never flushes the queue, so the
wiring smoke test (`tests/lua/capture/atom_stream.lua`) is a standalone async
script run via `-c luafile` with real `vim.defer_fn` timers, dispatched as its
own `run.sh` phase. The decision table itself is pure and is exercised directly
through `atom_stream._classify`.
