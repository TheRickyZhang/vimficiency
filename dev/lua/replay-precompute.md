# Replay precompute (`simulate/precompute.lua`, `simulate/tokenize.lua`)

Replay shows `{ lines, cursor, mode }` after every token of a captured sequence.
Scrubbing must be O(1), so every step is precomputed up front; `:normal` is not
usable as the oracle because it flattens the mid-insert and mid-visual states the
view exists to show.

## Probe window

Each sequence is driven through `nvim_feedkeys` in a hidden 1×1 float that is
made the real current window. `nvim_buf_call` is not enough: feedkeys queues
input that drains later into whichever window is current, so keys could leak
into the user's buffer. `noautocmd`, `nofile`, and `bufhidden=wipe` keep the
probe invisible. Sequences run one at a time; `precompute_gen` invalidates stale
callbacks when a new replay starts first.

## Tokenization

C++ `tokenize_sequence`, then `tokenize_movements`, then a per-character Lua
fallback. Two rules matter for replay: a command that consumes one following key
(`f`, `t`, `r`, `m`, `'`, `` ` ``, `@`) keeps it in the same token, and plain
insert text is chunked into 4-char steps while `<...>` keys stay whole. Token
`kind` comes from the C++ `SequenceParser` over the FFI, so Lua does not keep a
second grammar; the fallback's `classify_fallback` tables cover only the
char-by-char path.

## Draining typeahead

`nvim_feedkeys(k, "n")` returns before the key is processed. Waiting two
`defer_fn(0)` ticks is probabilistic: an input-drain pass is not guaranteed to
run between two timer callbacks, which surfaced as an occasional first-token
dropout under live load.

Ordinary tokens use `"nx"`, which drains synchronously, whenever the probe is not
in Insert/Replace and `enters_modal_state(token)` (`kind == change | visual`) is
false. Modal tokens cannot use `x` because replay needs the Insert/Visual
snapshots, so they keep the two-yield path. The yields still run after `nx` too;
they are free when nothing is queued and still cover the one-tick
`nvim_get_mode()` lag seen on `i` → `<Esc>`.

## Edit transactions

A `change typed* escape?` run is replayed as growing prefixes from the pre-edit
snapshot (`ciw`, `ciw2`, `ciw2<Space>`, ...). Feeding `ciw` then `2` as separate
inputs can leave the operator pending in headless Neovim and capture stale
snapshots. After the closing `<Esc>`, precompute lets InsertLeave settle and
reapplies the recorded snapshot, since the probe can inherit a delayed cursor
adjustment. A final `<Esc>` is always fed so the probe is torn down from Normal
mode.

## Debugging

The UI is a pure index into the precomputed states, so a precompute bug shows up
as a wrong stored snapshot, not a wrong render. The `D` key dumps rendered cursor
beside `snapshot[k]`. If the first-token regression test
(`tests/lua/simulate/integration.lua`) goes intermittent, the `nx` guard is no
longer sufficient; if modal snapshots drift, the yield path is.
