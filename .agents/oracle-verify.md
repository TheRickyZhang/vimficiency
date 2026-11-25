# oracle-verify

Use when implementing or modifying any Vim/Neovim semantic — motions,
operators, text objects, edit operators, exclusive/linewise/blockwise
behavior, autoindent, backspace, sentence/paragraph/word boundaries, or
anything that must match Neovim's runtime. Also use when writing or updating
tests that assert Vim behavior.

Vimficiency's correctness floor is "behaves identically to Neovim." Guessing
from `:help` or memory is not enough — Neovim's source and a live oracle are
the two authorities.

## Workflow

1. **Find the Neovim source reference.**
   The behavior you are implementing or fixing is defined in Neovim's C source
   (typically `src/nvim/edit.c`, `ops.c`, `normal.c`, `textobject.c`,
   `memline.c`, `mark.c`, `search.c`). Locate the function and read it. Note
   the file and key line range in your reasoning so the reader can follow.
   Common references already documented in memory:
   - `inc/incl/dec/decl` — `memline.c` ~4096–4168 (NUL-terminator position model)
   - `findsent()` — `textobject.c` (sentence motion)
   - `startPS()` — paragraph / nroff macro boundaries

2. **Match Neovim's position model, not C++ string indexing.**
   Neovim treats the end-of-line as a NUL position with `cls() == 0`. Several
   functions (`fwdWord`, sentence/paragraph scans) only behave correctly if
   our cursor model can sit at `col == line.size()`. If your port stops one
   position short, re-read `inc()` / `incl()` — that is almost always the bug.

3. **Compare against NeovimOracle.**
   For a complex mismatch, start in `tests/Debug` and compare Vimficiency
   and Neovim side by side. Put a permanent regression test in the matching
   suite under `tests/Unit/`. Assert that the resulting buffer and cursor
   match, along with the mode when relevant.

4. **Run the focused regression test.**
   Use `scripts/vimfy_tests unit '<Suite.Test>'`.

## Oracle gotchas (high-frequency footguns)

- **`<BS>` byte:** send as `\x08` (ASCII BS), NOT `\x7f` (DEL). DEL silently
  fails to delete autoindent whitespace; BS works. See `NeovimOracle.cpp`.
- **Oracle options.** `NeovimOracle::Impl::configureVimOptions()` sets
  `startofline`, `joinspaces`, and `autoindent` to match
  `src/VimCore/VimOptions.cppm`. It also clears `comments` to match
  Vimficiency's join behavior. See `tests/Utils/NeovimOracle.cpp`.
- **Step vs. full sequence divergence.** Calling `nvim_input` once per command
  (step trace) can give different results than one combined `nvim_input` call,
  because autoindent "pending" state is stripped between calls. If a test
  passes step-by-step but fails as a full sequence (or vice versa), this is
  why — pick the trace mode that matches what production does.
- **`<BS>` in autoindent whitespace** deletes to the previous `shiftwidth`
  boundary, not one column. With sw=8: indent 1–8 → 0 in one BS, indent 9 → 8
  in one BS. Use `bsCountForIndent()` in `src/Optimizer/Indentation.cppm`. If BS
  cannot land exactly on the target indent, fall through to `<C-u>`.

## A* / replay consistency

If the change touches anything the optimizer can emit (motions or edits used
inside `exploreCountedWordEdits`, `exploreCountedCharEdits`,
`replayAndCacheSuffix`, etc.), the new semantic must hold under **both**:

- Direct application via `Edit::applyEdit`.
- A* state transitions used during search.

A divergence here surfaces as a replay crash, not a wrong answer. The fix is
almost never "tweak A*" — it is "make A* call the same primitive that
`applyEdit` calls." Existing precedent:

- `exploreCountedWordEdits` uses raw `motionE / motionW / motionB / motionGe`,
  not `motionWordEndpoint`.
- `dd`-from-last-effective-line clamps cursor to `max(0, size-2)` to match
  what `deleteRangeLinewise` + `k` produces during replay.

## When this does NOT apply

- Pure infrastructure: build, logging, telemetry, formatting.
- Optimizer-internal heuristics that do not change observable Vim semantics
  (e.g. cost-function tuning).
- Lua / FFI session-layer changes that do not call into Vim emulation.
