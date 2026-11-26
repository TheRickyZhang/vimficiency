---
title: "Inspecting results"
---

# Inspecting results

## Reading the notification

When a session completes, you get something like:

```
vimfy finished [a] (0,0) -> (2,2)
  user: cw xxx <Esc> jj ci yyy <Esc> (15.00)
  1. 3rx <C-d> ciw yyy <Esc> (8.00)
  2. 3rx w <C-d> 3ry (11.00)
  3. 3rx Wlj ciw yyy <Esc> (13.00)
```

- `(0,0) -> (2,2)` — cursor moved from row 0 col 0 to row 2 col 2 (0-indexed)
- `user:` — what you typed for reference
- Numbered lines — the top optimal sequences in order of cost.
- The decimal value in parentheses is the calculated effort for the corresponding sequence

Cost is a keyboard-effort score (lower is better), not a keystroke count.
See [10. The effort model](10-effort-model.md) for how it's computed and
how to tune it to your layout.

## Simulating

`:Vimfy play <alias> [count]` opens a new tab with side-by-side windows and
shows your recording alongside simulated optimizer suggestions.
`count` sets the number of panes, up to four. The default layout pairs
your recording with the best suggestion.

Replay opens after a brief precompute phase. Step forward with
`<Right>`, back with `<Left>`. Close the replay with `q`.

The `[you] Recorded` pane shows the text, cursor and mode Neovim reported.
A Visual deletion such as `Vd` shows the line disappearing in one step.
When several commands arrive together, one step shows their combined result.
Plugin edits appear as they happened, even when a mapping’s reported keys
omit some of the text it inserted.

Each numbered `Simulated` pane runs a suggestion in a hidden Neovim buffer.
You can compare the two even when you used the same keys as the top suggestion.
Both panes use the same slice, including lines you visited and temporary edits
you later undid. A step advances each pane to its next available state.

Opening an older v1 save shows a warning. Its user pane has one step between
the two states that file contains. Suggestions still animate normally.

Each replay window shows a virtual header above the buffer:

- progress through the global and local step counts, plus the current
  editor mode
- the replay sequence itself, wrapped to the window width, with the
  current token highlighted

The `[N]` label at the start of the currently-focused window's header is
colored distinctly, so a glance at the tab tells you which buffer your
cursor is in. The cursor highlight also changes with the simulated
mode, so insert and visual segments are visible even in unfocused
windows.

`<Tab>` / `<S-Tab>` cycle between the sim sequences: in the split
layout they move the cursor to the next/previous sim window (wrapping);
in focus mode they swap the displayed buffer in place. `<CR>` toggles focus on
the window under the cursor — it collapses the tab to just that window
full-screen, and `<CR>` again from inside restores the side-by-side
layout. The same toggles are also available as `:Vimfy focus <N>` /
`:Vimfy escape` for typed invocation.

## Replay buffer keys

Press `:h g?` in a replay buffer to open this section directly.

- `<Left>` — step backward
- `<Right>` — step forward
- `<CR>` — focus or unfocus the current replay buffer
- `<Tab>` / `<S-Tab>` — cycle to the next or previous replay sequence
- `<leader>y` — yank the current window's sequence into the unnamed and `+` registers
- `?` — show the short keymap summary popup
- `g?` — open the full help section for replay buffers
- `q` — close the replay tab

## Listing what's around

```vim
:Vimfy list
```

Shows the active sessions and the pins.

## Saved sessions and pins

Every finished session is active in memory until it rotates out, and is saved
to disk as it finishes. `:Vimfy pin` keeps a copy under a filename you choose.

`:Vimfy play <alias>|<filename>` works against all three: it replays from
memory first, then a pin, then a saved session. See
[7a. Session storage](07a-session-storage.md) for the full model and a worked
example.

## Viewing a saved result

```vim
:Vimfy view my-refactor
```

Reads back a result textually (without replaying): a pin here, or any alias
or saved session. Tab-complete on `:Vimfy view` lists the aliases and pins.

## Saved result buffer keys

Press `:h g?` in a saved-result buffer to open this section directly.

- `q` — close the current saved-result view
- `?` — show the short keymap summary popup
- `g?` — open the full help section for saved-result buffers
