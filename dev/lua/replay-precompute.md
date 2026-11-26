# Replay precompute (`simulate/precompute.lua`, `simulate/tokenize.lua`)

View indexes precomputed `{ lines, cursor, mode }` states when scrubbing.
The user pane expands the recorded line replacements from the search slice's
initial text. Cursor-only steps share the preceding text. A delivery tick
contributes one step, labeled with its captured presses when available and otherwise its resolved keys. A change
observed at finish after the last command contributes a final unlabeled step.
Journal runs expose the same steps when their window is opened.

## Probe window

Each optimizer suggestion is driven through `nvim_feedkeys` in a hidden 1×1 float that is
made the real current window. `nvim_buf_call` is not enough: feedkeys queues
input that drains later into whichever window is current, so keys could leak
into the user's buffer. `noautocmd`, `nofile`, and `bufhidden=wipe` keep the
probe invisible. Sequences run one at a time; `precompute_gen` invalidates stale
callbacks when a new replay starts first.

## Tokenization

The tokenizer splits each suggestion into feedable steps. Typed text is
chunked into four-character steps and `<...>` keys stay whole. A parse
failure shows the rejected key in the suggestion pane.

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
