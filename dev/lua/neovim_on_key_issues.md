# `vim.on_key` hazards (Explore only)

Capture moved to the `CmdAtom` autocmd (`atom-stream.md`), which reports one
resolved unit per command and has none of these problems. Explore still runs its
own `vim.on_key` listener (`explore/key_capture.lua`) and reduces the raw events
with `capture/key_tracking.lua`, so the hazards below still apply there.

## Operator-pending duplication

Completing an operator re-evaluates the motion key: `cw` arrives as `c, w, w`,
`d2w` as `d, 2, w, w`. `build_sequence` drops a consecutive duplicate only when
both copies are in operator-pending mode and keeping the first already forms a
complete edit. `dgg` is kept (`dg` is not an edit); `cWW` is kept (the second
`W` arrives in insert mode). Text-object prefixes have a second shape
(`d`, `no:i`, `n:i`, `n:w`) handled the same way.

## Missing text-object key

The final key of a text object (`w` in `ciw`, `"` in `yi"`) is consumed
internally and never reaches `on_key`. There is no fix within `on_key`; Explore
relies on the physical buffer snapshot instead of the key stream. Upstream
request for a `v:motion` variable: neovim/neovim#19426.

## Mapping LHS leakage

At human typing speed a multi-key mapping LHS fires one pending event per key
(`typed == key`) plus a resolution event (`#typed > 1`, `typed ~= key`). The
pending events look like real motions. `strip_matching_tail` walks back from the
resolution event and pops the tail that matches its `typed`; a mismatch is
skipped since the strip is best-effort. A partial LHS with no resolution stays
recorded, which matches what the user actually typed.

Macro recording (`q{reg}`) was tried and rejected: it records the stop command,
shows "recording" in the statusline, blocks the user's own macros, and flattens
`<leader>`.
