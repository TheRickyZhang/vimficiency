---
title: "Recall (auto start, manual end)"
---

# Recall (auto start, manual end)

Recall lets you look backward without any previous marking setup by querying a continuously maintained queue. It is always on — the bounded, RAM-only ring is foundational enough that there's no toggle.

```vim
" ... edit normally ...
:Vimfy recall 6       " Look back 6 recorded command atoms
:Vimfy recall 3s      " Analyze the last 3 seconds
```

`:Vimfy finish <alias>` stays reserved for manual handles (Mark / Watch);
retrospective windows go through `:Vimfy recall`.

## Window aliases: two ways to index the queue

| Syntax   | Meaning                    | Use when                                     |
|----------|----------------------------|----------------------------------------------|
| `N`      | Last N recorded command atoms | You can count the actions you took |
| `Ns`     | Last N seconds of activity | You remember when you started |

A command atom is one Vim action reported by Neovim. For example, `dw`
counts as one atom. Recall counts only the atoms Vimfy records. Both forms
look back from now and end at the current cursor.

Mixing forms (`3m`, `3ms`, `2h`, etc.) is not supported. Seconds is the
only time unit; if you want 90 seconds, use `90s`.

The same grammar is reused by [Suggest](06-suggest.md) triggers (e.g.
`window = "3s"` / `"50"`) — anywhere Vimfy wants a slice of the
queue, `N` and `Ns` are how you name it.

## Implementation Details

Each command is tagged with a timestamp. `:Vimfy recall Ns` resolves to
the most recent commands run within the last `N` seconds. If a command or
time cutoff falls inside a burst delivered together, Recall includes the
whole burst. For example, `recall 1` can include both deletions in `dwdw`
when Neovim reported only their combined result.

The ring shares command observations and changed lines across overlapping
recall windows. Finishing a window submits its observed range to the optimizer.


## Capacity

The queue is bounded by two caps, applied with **union semantics** — a
session is kept as long as EITHER cap still holds it, and only evicted
when BOTH say drop:

| Knob                       | Default | Meaning                                |
|----------------------------|---------|----------------------------------------|
| `KEY_SESSION_CAPACITY`     | 200     | Count floor: keep at least N sessions. |
| `MAX_RETENTION_SECONDS`    | 120     | Age floor: keep sessions newer than N. |

So at defaults, the queue always retains at least 200 sessions AND at
least the last 120 seconds of activity.
```lua
require('vimficiency').setup({
  KEY_SESSION_CAPACITY  = 500,   -- keep more sessions
  MAX_RETENTION_SECONDS = 300,   -- keep a longer time window
})
```

If `recall Ns` asks for a window older than the queue retains, the
alias fails to resolve and you get "No recall session found within
'Ns'…" — raise either cap.

The recall ring is discarded when Neovim exits. A window you finished with
`:Vimfy recall` is saved to disk like any other session.

## Recall aliases move

Recall windows rotate out of the queue as you type, so an alias like `3s`
soon names something else. `@` refers to the most recently ended session,
which makes it the handle to pin one by:

```vim
:Vimfy recall 3s
:Vimfy pin @ nested-refactor
```


## See also

- [6. Suggest](06-suggest.md) — surface results automatically
  without calling `finish`.
- [7. Inspecting results](07-results.md) — what the output looks like
  and how to replay it.
