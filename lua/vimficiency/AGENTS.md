# Lua Layer Documentation

The Lua layer is a Neovim plugin that captures user editing sessions and calls the C++ optimizer for analysis.

## Layout

Top-level modules are the plugin entry and cross-cutting infrastructure:

| File | Purpose |
|------|---------|
| `init.lua` | Plugin entry point: `setup()`, the `:Vimfy` dispatcher and its tab completion, teardown and reload. |
| `session.lua` | Live capture lifecycle: start, watch, finish, recall, close. Argument errors raise to the `:Vimfy` boundary. |
| `session/archive.lua` | Finished results as named things: selector resolution (`@`, `$`, alias, recall window), save/store/fetch/rm, view. |
| `simulate.lua` | Side-by-side animation of motion sequences. Required as `vimficiency.simulate`. |
| `explore.lua` | Interactive result exploration view. Required as `vimficiency.explore`. |
| `play.lua` | Play-screen settings and modal wiring. Required as `vimficiency.play`. |
| `stats.lua` | Stats aggregation and display. Required as `vimficiency.stats`. |
| `subcommands.lua` | The subcommand table: one `VF.Subcommand` record (desc, usage, min_args, complete, run) per `:Vimfy` verb. Depends on the domain modules; only `init.lua` depends on it. |
| `dev_reload.lua` | `:Vimfy reload`: cmake rebuild, then hot-reload the `.so` and every Lua module. |
| `config.lua` | Shared configuration constants (Lua-side only). |
| `config/validate.lua` | Validators for the structured `watch` and `auto_suggest` blocks; the shape itself stays in `config.lua`. |
| `ffi.lua` | Compatibility surface for LuaJIT FFI bindings to `libvimficiency.so`. |
| `util.lua` | State capture, ID generation, UI helpers. |

Subdirectories hold supporting modules for each domain:

### `session/` — records and lifecycle

| File | Purpose |
|------|---------|
| `session/alias.lua` | Alias grammar validation for Mark / Watch / Recall handles. |
| `session/compute.lua` | Active-session capture normalization and optimizer invocation. |
| `session/disk.lua` | Saved-result JSON encoding, validation, and disk operations. |
| `session/playback.lua` | Converts stored results into replay sequence pools. |
| `session/result_view.lua` | Lua adapter for C++ result report formatting shared by `finish` and auto-suggest. |
| `session/result_window.lua` | Full-screen side-by-side viewer (`:Vimfy view`): initial/final panes, diff + cursor highlights. |
| `session/store.lua` | Canonical session records + manual/recall indexing. |

### `session/picker/` — session picker support

| File | Purpose |
|------|---------|
| `session/picker/model.lua` | Active/saved item construction, filtering, sorting, and row formatting. |
| `session/picker/preview.lua` | Preview-pane content for active and saved items. |
| `session/picker/popups.lua` | Sort/help popup windows. |
| `session/picker/keymaps.lua` | Picker and prompt keymap installation. |

### `simulate/` — replay support

| File | Purpose |
|------|---------|
| `simulate/actions.lua` | Replay key handlers and keymap table. |
| `simulate/pool.lua` | Sequence pool tokenization and layout-slot planning. |
| `simulate/precompute.lua` | Hidden Neovim probe used as the replay oracle. |
| `simulate/render.lua` | Virtual headers, cursor highlights, status bar, and snapshot rendering. |
| `simulate/tokenize.lua` | Parser-backed animation tokenization plus fallback splitting. |
| `simulate/windows.lua` | Replay tab/window/buffer construction, focus, escape, and relayout teardown. |

### `ffi/` — C ABI wrappers

| File | Purpose |
|------|---------|
| `ffi/core.lua` | Loads `libvimficiency.so`, validates ABI hash, and owns shared encoders. |
| `ffi/config.lua` | C++ configuration bridge. |
| `ffi/session.lua` | Session/capture helpers exposed by C++. |
| `ffi/optimizer.lua` | Optimizer analysis and optimizer-override encoding. |
| `ffi/sequence.lua` | Sequence tokenization, formatting, and movement simulation. |
| `ffi/display.lua` | C++ sequence-display and session-report formatter wrappers. |
| `ffi/explore.lua` | Explore-state and recommendation wrappers. |

### `capture/` — observation and triggers

| File | Purpose |
|------|---------|
| `capture/atom_stream.lua` | CmdAtom-driven capture: one atom per atomic user action, normalized and fanned out to named subscribers (manual/recall/triggers). See `dev/lua/atom-stream.md`. |
| `capture/key_tracking.lua` | Legacy `vim.on_key()` capture, now used only by interactive Explore's frontier replay. |
| `capture/recall.lua` | Rolling recall-ring capture driven by atom events. |
| `capture/end_trigger.lua` | Idle/command-count auto-end triggers (Watch / Suggest). |
| `capture/auto_suggest.lua` | Idle-trigger auto-suggest (runs optimizer on a recall window). |

## Session Architecture

### Session Types (session_store.lua)

1. **Manual** (aliases: arbitrary alphabetic strings — e.g. `a`, `refactor`, `bugfix`;
   capacity: 5 concurrent active sessions)
   User-controlled: `:Vimfy start <name>` / `:Vimfy finish <name>`. Grammar is
   strictly alphabetic (`^%a+$`); recall forms are rejected at the `finish`
   entry point with a redirect to `:Vimfy recall`.

2. **Recall** (rolling ring; queried via `:Vimfy recall N` or `:Vimfy recall Ns`)
   Creates a retained session on every captured atom (one atomic user action).
   Both indexing modes share one ring — `N` means "N commands ago", `Ns` means
   "as far back as N seconds ago". Because each atom is already a complete
   command boundary, no mode-based backward snapping is needed. Permanently on
   (started by `recall_capture.start_continuous_capture()` at setup). Retention is bounded
   under **union** semantics: evict the oldest session only when BOTH
   `KEY_SESSION_CAPACITY` and `MAX_RETENTION_SECONDS` say drop.

### Data Structures

- **SessionRecord**: Canonical per-session entry with `status` ("active" | "finished"),
  `stream_sub` (the atom-subscriber name, while active), the captured `atoms` list
  (while active), and `result` (after finish).
- **ResultSession**: Optimizer output + position + captured `start_time`/`key_count`,
  attached to the record at finish.
- **SessionSummary**: Normalized view-model for `:Vimfy list` / auto-suggest output.

One `session_records[id]` table holds everything; `manual_alias_to_id` and
`recall_id_order` are the two indexes into it. Finish flips `status` in place
and drops `atoms` — it does NOT unindex. This removes tombstones from
`recall_id_order` entirely: a finished session stays reachable via its recall
alias (for `:Vimfy play Ns`) until the ring slot rotates out.

## Atom stream (capture/atom_stream.lua)

Capture (manual sessions, recall, Watch/Suggest triggers) runs off Neovim's
`CmdAtom` autocmd (0.13-dev / nightly). The event fires once per atomic user
action, AFTER it runs, carrying the resolved post-mapping `keys`, the `lhs`
that produced it, the count, and whether the buffer changed or the cursor
moved. `atom_stream.lua` normalizes each event into a `VF.Atom`, classifies it
(record / skip / abort), and fans it out to named subscribers; buffer
and window scoping is each subscriber's job. See `dev/lua/atom-stream.md` for
the full design (admission policy, synthetic-count stripping, recall
snapshot-shift, the deferred-burst limitation).

Because the event already resolves operator-pending and mapping expansion,
none of the old `vim.on_key` hazards apply: no operator-motion duplication, no
mapping-LHS leakage, no missing text-object final char, no retraction. A
`:Vimfy` keymap produces an inert excmd/no-keys atom and is naturally excluded
without any scan — the obsolete `mapping_scan` warning was removed with the
migration.

`key_tracking.lua` (the former `vim.on_key` layer) survives only for
interactive Explore's frontier replay, which still consumes raw key events.

**Approximate motion conversions** (in session/compute.lua):
- `gj` → `j`, `gk` → `k` (screen-line motions unsupported by optimizer)

## FFI Interface (ffi/)

`ffi/core.lua` loads `libvimficiency.so` via LuaJIT FFI. Domain wrappers expose:

```lua
ffi_optimizer.analyze(lines, ...) → VF.Optimizer.Result[], user_cost, debug_str, VF.Diff.Region[]
ffi_config.apply_globals(user_config)  -- Push the C++ process globals (cost model, Vim options)
ffi_sequence.tokenize_sequence(seq) → VF.Sequence.Token[]  -- {text, kind} tokens
ffi_sequence.tokenize_movements(seq) → VF.Sequence.Token[]   -- all kind="motion"
```

**Position indexing:** C++ uses 0-indexed rows/cols. Neovim uses 1-indexed rows. Conversion happens before calls cross the FFI boundary, primarily in `session/compute.lua`.

## Configuration (config.lua)

Config keys are owned by exactly one side. C++ owns process globals (`VFConfig`: keyboard cost model, shiftwidth), pushed once by `ffi_config.apply_globals`. Lua owns everything read at call time, including per-search optimizer parameters passed as FFI arguments:

| Setting | Default | Description |
|---------|---------|-------------|
| `RESULTS_CALCULATED` | 20 | Max results from optimizer |
| `RESULTS_SAVED` | 5 | Results kept per session |
| `SLICE_PADDING` | 5 | Lines above/below cursor for search slice |
| `MAX_SEARCH_LINES` | 500 | Max lines in search region |
| `KEY_SESSION_CAPACITY` | 10 | Rolling key session limit |

## Simulation (simulate.lua)

Terminology: **step** means the small replay unit, usually one parsed token.
Interactive Explore's larger state-machine units are **phases** (`Navigate`,
`Transform`, `Insert`, `Completed`).

`simulate_compare(lines, row, col, sequences)`:
- Precomputes replay snapshots asynchronously by driving Neovim itself as the
  oracle (`nvim_feedkeys` into a hidden probe window), then opens a new tab
  with side-by-side windows
- Replay opens paused; `<Left>`/`<Right>` step, `<CR>` toggles auto-play, `q`
  closes the whole replay tab
- `:Vimfy focus <N>` / `:Vimfy escape` toggle the single-buffer view
- Uses virtual header lines above each replay buffer for progress, mode, and a
  wrapped/highlighted sequence display
- Uses C++ tokenizer with character fallback for unsupported motions; tokens
  keep feedable command units like `f;` / `rX` intact while leaving repeats
  like `;` as separate replay steps

Replay verification now lives in repo-native Lua tests:
- `tests/lua/simulate/tokenize.lua` covers tokenization / feedable replay steps
- `tests/lua/simulate/integration.lua` covers the async replay oracle and UI rendering

## Testing
After changing some lua sections, make sure to run ./scripts/lint-lua.sh to verify that all the lua semantics are correct.

## Commands

```
:Vimfy start <alias>    -- Start manual session
:Vimfy finish <alias>   -- Finish a manual session (Mark/Watch)
:Vimfy recall <N|Ns>    -- Finish a retrospective recall window
:Vimfy close <alias>    -- Discard session
:Vimfy play <alias>     -- Animate results
:Vimfy focus <N>        -- Focus replay on Nth buffer (full-screen)
:Vimfy escape           -- Restore side-by-side replay layout
:Vimfy view [name]      -- View saved results
:Vimfy list             -- Show active/saved sessions
:Vimfy suggest <on|off|toggle> -- Toggle auto-suggest (needs `auto_suggest = {...}` in setup)
:Vimfy config           -- Show C++ config state
:Vimfy reload           -- Rebuild C++ library
```
