# Architectural TODO

Deferred improvements that are justified in principle but not worth the churn today. Each item lists a concrete **threshold** — the change in the codebase that tips the cost/benefit toward doing it. When you touch an area near one of these items, re-read the threshold and decide whether the condition has become true.

Items are ordered by priority, highest first. Items that depend on each other are merged into one.

Re-evaluate this file:
- Whenever you add a new build-time flag or touch `CMakeLists.txt` / `src/BuildConfig.h.in`
- Whenever you modify `.github/workflows/bench.yml`
- When onboarding someone new to the repo
- At minimum once per major refactor

Completed items should be deleted, not crossed out.

---

## 1. Stop blocking the Neovim main thread (top priority)

**What.** Nothing vimficiency does should hold the main thread for more than
0.2 s. Auto-suggest and Explore open already run their search on a C++ worker
(`AsyncJob`, polled by `async/poller.lua`), but four paths still run a search or
a thread join on the main thread:

| path | main-thread work | bound |
|---|---|---|
| `:Vimfy finish`, `:Vimfy recall`, Watch's idle auto-finish (`session.lua` `finish_active` → `vf_analyze`) | full analyze search under `SearchControl::NEVER_STOP` | none |
| Explore settings change that affects the plan (`explore/state.lua` `reconfigure_plan` → `View::computePlan`) | full composition search under `NEVER_STOP` | none |
| every accepted key, undo, redo and restore in Explore (`explore/state.lua` `fetch` → `vf_explore_recommendations`) | one Nav or Transform search for the current phase | natural search caps |
| cancelling a live worker (`vf_analyze_cancel`, `vf_explore_cancel`) | `~AsyncJob` joins the worker | the worker's next cancel check |

The cancel flag is polled per pop in the Composition and Nav loops, and between
composition setup phases. `VimDiff::calculate` and one diff's Transform
precompute are uninterruptible, so a join waits for whichever is running.
`compute_deadline_ms` does not abort setup either, so a worker can outlive its
deadline and lengthen that join.

Fixes, in order:

1. Route `finish_active` through `compute_result_for_active_async`. Finish,
   recall and Watch then share auto-suggest's worker path.
2. Give `reconfigure_plan` the start/poll shape Explore open uses.
3. Do not join at cancel. Hand the cancelled job to a reaper so the worker
   finishes in the background; the job must own everything it touches.
4. Measure the recommendations path with `vim.g.vimficiency_explore_slow_ms`
   lowered. If it trips, give those searches a deadline or make them async with
   a stale list shown meanwhile.

The audit should also cover what this pass skipped: the per-atom capture path
(`capture/atom_stream.lua`, `capture/buffer_history.lua` `mark`) on large
buffers, and the `VimOptions::shiftwidth` no-reconfigure invariant in
`AsyncJob.cppm` once more than one worker kind can be live.

**Status (2026-11-15).** Found by reading the code; nothing was timed. Which
paths exceed 0.2 s in practice is unmeasured, and the two `poller.cancel` sites
in `capture/auto_suggest.lua` were not traced to their triggers.

**Threshold.** None; do this next. Done when no FFI call reachable from a
keymap, autocmd or timer runs a search or a join.

---

## 2. Replace the optimizer-params X-macros with C++26 reflection (key priority)

**What.** `src/Optimizer/OptimizerParamFields.h` is the single source of truth
for every optimizer param field, expanded five ways: struct members, `with*`
setters, the per-struct `Field` enum, the override patch ops in
`OptimizerParamOverrides.cpp`, and the defaults wire format in
`vf_get_optimizer_defaults`. With P2996 static reflection all five collapse
onto the plain struct definitions: iterate
`nonstatic_data_members_of(^^Params)`, match `identifier_of(m)` against the
override key, assign through `p.[:m:]`, and pick the parser/type tag from the
member type. The `Field` enum and presence bitset become a bitset sized by the
member count, and the `with*` setters can go too if designated init reaches
base-class fields by then. This is the highest-value reflection site in the
repo: it is pure boilerplate keyed on member names, and every new param today
means touching a macro row plus trusting the lint to catch missed consumers.

**Why deferred (2026-11-13).** Local GCC 16.2 compiles P2996 with
`-std=c++26 -freflection`, but the project is C++23 and the flag has to apply
to the whole module graph. The documented from-source floor (`dev/decisions.md`:
GCC 16+ / Clang 19+libc++) and the Clang-only fuzzing tree
(`dev/tests/fuzzing.md`) have no `<meta>`; Clang 23 locally ships none. A
`#if __cpp_reflection` fallback would mean keeping both implementations.

**Threshold.** Clang ships `<meta>` in a release and the macOS/Emscripten
toolchains the CI matrix uses pick it up, so the project can move to
`-std=c++26` everywhere. Then do this item first; it is the proof case for
retiring X-macros elsewhere.

---

## 3. Generate the Lua types from the FFI schema (high priority)

**What.** Every FFI request and result is a protobuf message
(`src/LuaExports/proto/`, see `dev/lua/ffi-payloads.md`), but the LuaLS classes
in `lua/vimficiency/types.lua` that describe the same shapes are still
hand-written. A field renamed in the schema shows up as a nil at the Lua use
site, caught only by a Lua test through the real library.

1. Generate the LuaLS classes from the descriptor with a C++ build tool, into
   a committed file that CI regenerates and diffs. The Lua lint then flags
   reads of fields that no longer exist.
2. Hash that generated file into the library next to the `Api.def` hash and
   check it in `ffi/core.lua`, so Lua code written against another schema
   fails at load.

The lite runtime has no descriptor reflection, so the tool needs either the
full runtime or to run as a `protoc` plugin.

Step 2 is a fork. The `Api.def` hash no longer changes when a message does, so
a Lua/library pair with different schemas loads today: a request field the
library lacks raises in `core.encode`, a result field it lacks reads as nil.

- **Lock (now):** the hash in step 2 rejects any mismatch.
- **Evolve (later):** allow mismatches under protobuf's compatibility rules,
  once users run prebuilt libraries that lag a plugin-manager Lua update. Needs
  add-only fields with `reserved` numbers, Lua that tolerates a missing newer
  field, a policy for `core.encode` on a field the library does not know, and a
  CI run of current Lua against an older released library.

**Why not done yet (2026-11-15).** The migration to protobuf landed first; the
generator is separable and needs the build decision above.

**Threshold.** Before the next release that changes a message, or the first
bug traced to `types.lua` disagreeing with the schema.

---

## 4. Composition consumes only `plans.front()` — evaluate the tied set

**What.** `CompositionSearchContext.cpp:77` takes the planner's single best
plan. Since the shared concave count model and the to-boundary oracle chunks
(2026-08-26), `VimDiff`'s top-K plans tie *exactly* in most `PlanRegret`
inversions (inversions where plan 1 and plan 2 have equal planner cost: 3 → 64
of 96 cases), and K-best returns ties in generation order, so plan 1 among
equals is arbitrary. The harness's tie-adjusted metric (tied plans treated as
interchangeable) is 10/96 inversions, mean regret 0.35 keystrokes; the raw
metric is ~73/96, mean ~4. The entire gap is tie-breaking, not the cost model.

Two non-heuristic fixes, in preference order:

1. Hand composition the whole tied set (usually 2–4 plans) instead of
   `front()` and keep the real best. Exact by definition; costs a small multiple
   of composition time only when the planner is genuinely indifferent.
2. Give the planner the effects that actually separate the ties — a
   newline-inclusive region costs `<CR>` plus Transform's autoindent fixups
   (`<Space>`/`<Del>`), `cc` vs `D`+retype — so it stops tying. More work, and
   it chases Transform behavior.

**Status (2026-11-02).** K-best was removed from `VimDiff` when the planner
was simplified, so fix 1 now needs a tied-set surface reintroduced in a simpler
shape (e.g. enumerate equal-cost backpointers at the root only) rather than the
old per-cell candidate lists. The tie-adjusted numbers above are from the
removed harness mode; the current `PlanRegret` compares against Myers.

**Threshold.** The next change to `VimDiff` or to how composition consumes
plans. Success is the raw `PlanRegret` numbers converging to the tie-adjusted
ones. Note `PlanRegret` is nondeterministic run to run (multi-threaded
composition; ±5 inversions), so compare tie-adjusted regret, not inversion
counts.

---

## 5. Rebalance diff generation against composition search

**What.** A three-step investigation, in order:

1. **Simplify diff generation.** Done 2026-11-02 (see `dev/decisions.md`
   *VimDiff simplified*): one plan, per-key typing, three step families. The
   remaining exactness is with respect to the surrogate, not to real Vim cost;
   whether even that is buying anything is what step 2 measures.
2. **Measure.** (a) Time split between diff generation and composition search on
   realistic slices — `VIMFY_TIME_PHASES` already prints per-phase and per-diff
   timing; a healthy split is something like 10/90. (b) How well the K plan
   rankings correspond to the best sequence actually findable from each
   (`tests/Debug/PlanRegret.cpp` via the `forcedDiffs` seam). (c) Whether the two
   stages can be combined into one globally optimal search rather than
   partition-then-optimize.
3. **Complexity.** Derive the time complexity of both stages and check that
   measured scaling matches. A mismatch means either the model is wrong or a cap
   is binding.

**Why it matters.** Production consumes the one plan `calculate` returns, so it
must be right. Whether the planner's surrogate ranks partitions better than
Myers has never been measured properly: `PlanRegret` now compares the two by
real composition cost, and its first run (2026-11-02) shows large regrets that
are too large to be partition quality: something downstream breaks down or
misses a case completely (real cost 5x the estimate on a 7-region plan). It is a
bug to find, not a calibration signal — triage that
first.

**Complexity as currently understood** (`N` = initial buffer chars, `n_k`/`m_k` =
block cells per axis, `Σ n_k ≤ N`, `mxcnt` = count cap, `E` = planned edits,
`P` = pops, `B` = branching per pop):

| stage | cost | note |
|---|---|---|
| VimDiff | `O(Σ n_k·(n_k+m_k)·mxcnt)` | move table + per-column deletion sweeps + O(1) type steps; diff-bound since seals stop sweeps |
| Composition construction | `O(E · P_t · B_t)` | `E` Transform A* runs; dominates the `O(E·N)` buffer rebuild and `O(E·L²)` text-object scan |
| Composition search | `O(P_c · P_n · B_n)` | **nested**: each pop can fire up to ~9 NavOptimizer A* runs (8 ranked start positions + the diff-range search) |

The headline is that composition is not one A* but two levels — a nav search
sits inside the expansion step. All three searches are hard-capped
(`maxNodesPopped` 50000), so wall time is governed by *where the caps bind*, not
by the asymptotics; that is exactly why step 2 has to be empirical.

**Concrete candidate found while deriving the above.** The `starts` loop in
`CompositionOptimizer.cpp:578` issues up to 8 single-point NavOptimizer searches
from the same cursor, each rebuilding its own `Lines` slice and `BufferIndex`.
`startPositions()` is clipped to the diff range by `restrictValidStartRegion`
(`PlannedEditArtifacts.cpp:243`), so it is a holey subset of that range — the
gaps come from start starvation under the shared Transform queue, which is why it
cannot be one `CharInterval`. But the *bounding* range is just the diff range,
which the fallback search twenty lines below already targets. Replacing the loop
with that single range search plus a filter of landings against
`startPositions()` gives one slice, one `BufferIndex`, one A*, and the same
reachable set.

Caveat: not a pure win. Today the 8-cap plus the `costToGoal` sort bounds
successors per pop at 8 ranked landings; the range search returns whatever nav's
`maxResults` / `maxResultsPerEndPos` allow, so composition branching — and hence
pop counts and results — can change. Measure before and after, same as the rest
of this item.

---

**Decision rule.** If diff generation is a small share of end-to-end time, stop —
the current design is fine and the effort belongs in composition. If it is a
large share, run `PlanRegret` with a cheap approximate oracle against the exact
one; equal regret means exactness can be dropped and the `N` factor disappears.

---

## 6. Share Transform search states across start positions

**What.** `TransformStateKey` includes `startIndex`, so every start searches its
own copy of the state space. Ten starts with no solution explore the same dead
end ten times. Keying states without the start, with per-start paths joined onto
shared completions, would remove that duplication.

**Why deferred (2026-11-13).** `startWorkFactor` bounds the waste: a start goes
inactive after 8x the nodes the hardest solved start needed (see
`dev/optimizer/transform-optimizer.md`). Sharing is a redesign of the search,
not a stop rule.

**Threshold.** A real region loses a start's result to `startWorkFactor`, or
Transform time dominates a profiled session.

---

## 7. Make the optimizer mapping-aware

**What.** Capture (via `CmdAtom`, since 2026-11-10) records the *resolved*
`keys` of each action and discards the user's own mapping LHS. So for a user
who has `Z`→`dd`, the cost comparison and every suggestion are in terms of the
underlying edits (`3dd`), never their shorthand (`3Z`). That is correct for the
beat/regression metric — costing physical `3Z` against optimal `3dd` would make
any efficient mapping look like it beats the optimizer — but it means we can
never *suggest* a mapping the user already has, nor credit them for it.

The `VF.Atom` already carries both sides: `keys` (resolved) and `lhs`+`count`
(as typed). The forward path is to thread the user's active mappings (the
`lhs`→`keys` relation, available from `nvim_get_keymap`) into the optimizer as
context, so it can (a) recognize when a candidate sequence is reachable by a
cheaper bound mapping and surface *that*, and (b) optionally cost the user's
input by what they physically typed when a mapping was involved.

**Why deferred.** The optimizer's effort model and search operate on the
canonical key vocabulary; mappings are an open-ended per-user alphabet. Feeding
them in means a second cost lane (mapped-LHS effort) and a surface for the
search to emit mapping tokens — neither exists today, and the resolved-keys
behavior is the right default in the meantime.

**Threshold to revisit.** A concrete request to have suggestions respect or
propose user mappings, or activity-log evidence that resolved-keys suggestions
are routinely worse for users who lean on custom mappings. Pointer:
`lua/vimficiency/capture/atom_stream.lua` (`VF.Atom.lhs`/`count`) and wherever
`user_seq`/`keyseq` is costed in `AnalyzeExports.cpp`.

---

## 8. Re-introduce visual-delete shortcuts to TransformFrontier

**What.** `tryVisualDelete` is still invoked by the batch TransformOptimizer
(`TransformOptimizer.cpp` in the `PureDeletion` finalization block) but the
TransformFrontier comment at the `emitReplaceCharAction` call site
explicitly skips it because visual-delete sequences are multi-token
structural macros (`v{motion}d`) that don't fit the single-action invariant
the rest of the frontier now enforces.

**Why deferred.** The frontier contract is "one immediately executable Vim
action per recommendation, and Explore re-derives phase from observed
state". `v)hd` is three structural tokens; supporting it in the frontier
requires a continuation/macro abstraction Explore doesn't have today.
Composition's text-object lane already covers the most common cases that
visual-delete used to surface.

**Threshold to revisit.** Either (a) Explore gains a `Macro` phase that can
track a multi-step structural token across `acceptSnapshot` calls, or
(b) a benchmark / activity-log case demonstrates that a real session is
losing ground because the visual-delete shortcut isn't surfaced
interactively. Pointer to the current skip site:
`src/Optimizer/TransformOptimizer/TransformFrontier.cpp` ("Visual deletion
is a multi-token structural macro" comment).

---

## 9. Recall records taken through the temporary autocmd window

**What.** `atom.win` is `nvim_get_current_win()` inside the deferred `CmdAtom`
callback, and Neovim makes a window showing `ev.buf` current for that call. When
the buffer is no longer displayed at delivery (burst `wx<C-w>cx`), that is a
temporary window (`win_gettype == "autocmd"`, cursor `(1,0)`). The check in
`capture/recall.lua` passes, so the record gets a window id that dies right
after and a wrong cursor.

The fix is in the recall listener: on an autocmd-type window, remove the
buffer's recall records and clear `recall_prev_state[buf]`. Do not reuse
`store.abort_recall`, which re-captures state through the same window. It needs
a live burst case in `tests/lua/capture/atom_stream.lua`.

**Why deferred (2026-11-13).** Hardening, not a bug fix. It needs a window
change inside one deferred burst, and every snapshot in a burst is already the
final text. `compute.lua` falls back to the current window when a session's
window is invalid. What the user sees from such a record was not traced.

Not fixable the same way: one buffer shown in two windows reports the final
window for every atom in the burst, so the cursor comes from the wrong one.

**Threshold.** A wrong recall traced to a window closed or switched mid-burst,
or the next change to recall's window scoping.

---

## 10. Route `VIMF_DEBUG` / `VIMF_LEGACY_VIM` through `BuildConfig.h`

**What.** Both flags are wired with the wrong name, so neither has ever worked:

| CMake defines | source checks | effect today |
|---|---|---|
| `VIMF_DEBUG` | `#ifdef VIMFICIENCY_DEBUG` in `src/Utils/Debug.cppm` | `DEBUG_ENABLED` is always `false` |
| `VIMF_LEGACY_VIM` | `VIMFICIENCY_LEGACY_VIM` in `src/VimCore/VimOptions.cppm` | `-DVIMF_LEGACY_VIM=ON` is a no-op |

Both are surfaced via `target_compile_definitions(... $<$<BOOL:${VIMF_DEBUG}>:VIMF_DEBUG>)`
and read with `#ifdef`, the same "typo silently takes the disabled branch"
footgun that caused the original track-states bug. Move both to `#cmakedefine01`
in `BuildConfig.h.in` (the `VIMF_TRACK_STATES` pattern) and read them with
`#if VIMF_DEBUG` / `#if VIMF_LEGACY_VIM`, in one PR.

**Why deferred.** Fixing the wiring changes behavior, which deserves its own PR
rather than riding along with another fix:

- `VIMF_DEBUG` defaults `ON`, so `DEBUG_ENABLED` flips to `true` for local
  developer builds and every `debug(...)` call site starts producing output.
  That was the original intent, but the call sites need a tour first.
- `VIMF_LEGACY_VIM` would retroactively enable legacy-Vim defaults
  (`startofline`, `joinspaces`, `Y`-yanks-line) for anyone who set the flag
  expecting it to work. No current workflow sets it; it needs an audit of the
  `VimOptions.cppm` blocks and test coverage for both branches.

**Threshold to revisit.**
- Someone needs `debug()` output to work locally, **or**
- A user or test case needs legacy-Vim defaults, **or**
- Someone adds a new `#ifdef VIMFICIENCY_*` check (bundle the cleanup so the whole class goes away at once).

---

## 11. Move the CI bench workflow onto `CMakePresets.json`

**What.** Replace the ad-hoc `-D` flags in `bench.yml` (e.g. `-DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF -DVIMF_TRACK_STATES=ON ...`) with named presets (`release-track`, `release-bench`, etc.) defined in `CMakePresets.json`. Preset names are validated against the file, so a typo fails loudly at `cmake --preset` time rather than being silently accepted as an unused cache variable.

**Why deferred.** Today the workflow has two real configurations (release-no-tracking, release-tracking) and three CMake options (`VIMF_DEBUG`, `VIMF_TRACK_STATES`, `VIMF_LEGACY_VIM`). `BuildConfig.h` + the `static_assert` in `vimfy_explore` + the `jq`-based validation step + the differentiated dashboard message already give layered protection, and are cheaper and more localized than presets. Presets would add a second config file, a learning curve for contributors, and wouldn't meaningfully harden the current setup further.

**Threshold to revisit.**
- The CMake option list grows past ~5 build-time flags, **or**
- A third distinct build configuration appears (e.g. a separate ASan build, a profiling build, or a per-architecture matrix), **or**
- The workflow grows a third `cmake -B ...` invocation pattern.

Any of those makes the "one preset per config" structure pay for itself.
