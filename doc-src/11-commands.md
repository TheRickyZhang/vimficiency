---
title: "Commands reference"
---

# Commands reference

| Command                                    | Purpose                                              |
|--------------------------------------------|------------------------------------------------------|
| `:Vimfy start <alias>`                     | Mark a session start (a named alias: letters only, e.g. `a`). |
| `:Vimfy watch <alias>`                     | Start a Watch session (auto-end on idle).            |
| `:Vimfy finish <alias>`                    | Finish a Mark or Watch session and optimize.         |
| `:Vimfy recall <N\|Ns>`                    | Finish a retrospective recall window.                |
| `:Vimfy close <alias>`                     | Discard a session without optimizing.                |
| `:Vimfy pin <alias>\|<filename> [<filename>]` | Keep a copy of a finished result. The filename defaults to the one it was saved under. |
| `:Vimfy unpin <filename>`                  | Delete a pin.                                        |
| `:Vimfy view <alias>\|<filename> [count]`   | Replay results side-by-side (memory, then pins, then saved sessions). |
| `:Vimfy focus <N>`                         | Focus the active replay on the Nth buffer (full-screen).|
| `:Vimfy escape`                            | Restore the side-by-side replay layout.              |
| `:Vimfy rm <filename>`                     | Delete a saved session's result. Its stats row stays. |
| `:Vimfy list`                              | Open the interactive session picker (see below).     |
| `:Vimfy stats`                             | Open the stats tab (see below).                      |
| `:Vimfy stats refresh`                     | Score the journal now, without opening the stats.    |
| `:Vimfy suggest <on\|off\|toggle>`         | Runtime toggle for auto-suggest (works with zero config). |
| `:Vimfy config`                            | Show the current configuration.                      |
| `:Vimfy appearance`                        | Customize selection highlighting.                    |
| `:Vimfy reload`                            | Rebuild and hot-reload Vimfy with live build output. |
| `:Vimfy version`                           | Show the plugin version.                             |
| `:Vimfy help`                              | Show the command list.                               |

## Aliases and filenames

Every handle you type is either an **alias** (a session in memory) or a
**filename** (a result on disk: a pin, or a saved session).

| Alias      | Means                                                                |
|------------|----------------------------------------------------------------------|
| `a`, `foo` | Letters only → a named alias, like a named register.                 |
| `6`        | Digits → recall N commands ago.                                      |
| `3s`       | Digits + `s` → recall N seconds ago (see [5. Recall](05-recall.md)). |
| `@`        | The most recently finished session.                                  |
| `$`        | The most recent auto-suggest regression.                             |

Which aliases a subcommand accepts:

| Subcommand                   | Accepts                                             |
|------------------------------|-----------------------------------------------------|
| `start` / `watch` / `finish` | A named alias.                                      |
| `recall`                     | `N` or `Ns`.                                        |
| `close`                      | Any alias.                                          |
| `view` / `explore` / `pin`   | Any alias or a filename (memory first, then pins, then saved sessions). |
| `unpin`                      | A pin's filename.                                   |
| `rm`                         | A saved session's filename.                         |

`@` is handy when the session you want to keep is a recall window whose alias
(`3s`) is moving out from under you:

```
:Vimfy recall 3s
:Vimfy pin @ nested-refactor
```

`:Vimficiency` is accepted as a full-name alias for `:Vimfy`.

## Active, saved, pinned

A finished session is **active** in memory under its alias, and **saved** to
disk under a filename made from the alias and the finish time. `pin` keeps a
copy that stays until you `unpin` it.

The same text can be an alias and a filename. See
[7a. Session storage](07a-session-storage.md) for the full model.

## Session picker (`:Vimfy list`)

Opens a centered float with two panes — **Active** (in-memory sessions) and
**Pinned** (files under `stdpath("data")/vimficiency/pinned`) — and a
live-filter prompt at the bottom. `<Tab>` toggles between panes.

Ongoing sessions always sit in a pinned **Ongoing** section at the top,
regardless of sort mode. Auto-generated recall ring entries (one per
command) are folded into a single `recall ring (N)` summary row — the
per-entry aliases drift, so they're not individually actionable from the
picker; resolve a specific window with `:Vimfy recall N` or `:Vimfy
recall Ns` instead.

| Key        | Action                                                |
|------------|-------------------------------------------------------|
| `/`        | Fuzzy-filter by alias (prompt at bottom, live)        |
| `<CR>`     | Open (default action, see below)                      |
| `<Tab>`    | Switch Active ↔ Pinned pane                           |
| `sn`/`sN`  | Sort by name (A→Z / Z→A)                              |
| `sc`/`sC`  | Sort by category (A→Z / Z→A)                          |
| `st`/`sT`  | Sort by time (newest / oldest)                        |
| `s`        | Sort hint popup (also on `s?`)                        |
| `d`        | Delete the current entry                              |
| `m`        | Toggle mark on the current entry                      |
| `D`        | Delete all marked entries                             |
| `r`        | Rename (see below)                                    |
| `y`        | Duplicate (see below)                                 |
| `?`        | Show the keymap popup                                 |
| `q`, `<Esc>` | Close                                               |

Inside the search prompt, `<C-n>` / `<C-p>` (and `<Down>` / `<Up>`) move
the list cursor without leaving insert mode; `<CR>` opens the current
entry; `<Esc>` returns to the list.

**Per-pane action semantics:**

- `<CR>` — on either pane, opens it as `:Vimfy view` does: a finished
  session on Active, the pin on Pinned.
- `d` — on Active, discards the session (equivalent to `:Vimfy close`);
  on Pinned, unpins (equivalent to `:Vimfy unpin`).
- `r` / `y` — on Pinned, renames/duplicates the file. On Active, renames
  the named alias / registers a duplicate finished record under a new
  alias. Only entries with a stable name alias are renameable this
  way; for recall and auto-suggest results, pin first (`:Vimfy pin
  <alias> <filename>`) and then rename from the Pinned pane.

**Blocked on in-progress sessions:** `<CR>`, `r`, `y` all refuse to run
on a still-accumulating session. Finish it with `:Vimfy finish` (or let
Watch's idle trigger fire) and try again.

## Session stats (`:Vimfy stats`)

Opens a tab with a header over one full-width pane:

- **Header** — the scorings counted, the weight and the charted run length,
  over the path of the session log the stats are read from.
- **Stats** — a Sessions band over a Journal band, with a faint line between
  them (`VimficiencyStatsRule`). Each holds its efficiency chart, its
  optimizer times and its own Commands tables.

In a wide window a band's Commands tables sit to the right of its chart, and
in a narrow one under it. `l` from the left side goes to that band's first
command, and `h` from a command goes back to its chart.

The stats are laid out as items: rows that list runs or expand command lists,
charts, and selector rows. One item is always current. It is highlighted whole and
the cursor itself is hidden.

`h`, `j`, `k` and `l` move to the item on that side, and the other keys act on
the current one. Any other motion takes the nearest item.

Keys in the stats (`?` lists them):

| Key             | Action                                                      |
|-----------------|-------------------------------------------------------------|
| `j` / `k`       | The item below / above; a count moves that many.            |
| `h` / `l`       | Change the active option on a selector row, or move to the item to the left / right. The arrow keys work the same way. |
| `<CR>`          | Open a row's runs, open a full-screen chart, or expand or collapse a command list. |
| `w`             | Switch the efficiency weight between effort and uniform for this view. |
| `v`             | Count every scoring, or only the current one (see below).   |
| `gs`            | Open `:Vimfy appearance`.                                   |
| `q`             | Close the tab.                                              |

`<Esc>` or `q` closes the float opened by `<CR>`. A
chart fills its column in the stats and the whole screen in its float.

The stats keep each band's left side to its headline: the Sessions efficiency,
the Journal's run lengths, a chart each and the optimizer times. The rest is
under the chart in its float.

The chart takes up most of the float. Scroll down with `<C-d>` or the mouse
wheel to see the details below it. Moving back up to a chart bucket brings
the entire plot into view. The chart draws two lines:
the efficiency, solid, against the left axis, and the number of samples
behind it, dotted, against the right. Each bucket's own value is a larger dot, placed to the nearest row, and
the lines only join them; the float's title has the exact value. A line breaks where there are no samples. Their colors
are `VimficiencyStatsEfficiency` and `VimficiencyStatsVolume`. `e` and `s`
hide and show the efficiency and the samples line. `+` shows more history
and `-` shows less, from 15 minutes to 30 weeks. These keys change the
history scale of the float. `?` lists the chart keys.

In the chart's float each bucket with runs is an item, named with its efficiency in the
title. `h` and `l` move between buckets and `<CR>` lists the selected bucket's runs. The
rows under the chart list their runs the same way.

The run list floats over the tab at its full width, one line per session or
journal run: when, your effort, the best found, the saving, the extra
characters you typed in insert mode and the search time. Its title holds the row it came from, the sort and the page. It takes
two fifths of the screen and the preview under it the rest.

The preview follows the current run. Its first line is the run's id: a
session's saved filename, which `view` and `pin` take, or a journal run's file
and position.

Under the id are your keys and the best sequence, spaced by command and cut to
two lines each. Then the run's text before and after, side by side, with the
changed lines marked and the cursor's place shaded in each.

A session's text comes from its saved result. A session finished before
results were saved, or with `sessions.save` off, shows its keys and why the
text is not available.

Runs from closed journals that have been idle for
`journal.keep_days` and fully scored count toward totals after rollup. Only
individual runs still on disk appear in the list.

Every cell of the list is an item, as in the stats:

| Key           | Action                                                        |
|---------------|---------------------------------------------------------------|
| `j` / `k`     | The run below / above, in the same column.                    |
| `h` / `l`     | The column to the left / right. The arrow keys move the same way. |
| `s`           | Sort by the current column: largest first, then smallest first, then off. Unsorted, the worst runs come first (your effort minus the best). |
| `<CR>`        | Replay the run as `:Vimfy view` does, your keys beside what was found, highlighted as the file was. A session shows every sequence found, a journal run the best one. `q` there returns to the list. |
| `p`           | Pin the current session. Asks for the filename, which defaults to its saved one. |
| `]` / `[`     | Next / previous page of runs.                                 |
| `<Esc>` / `q` | Back to the stats.                                            |

Sections:

- **Sessions** — your percent efficiency: the best effort found over the effort
  you spent, `min(best / yours, 1)`, averaged over sessions. It never passes
  100%. A session that beats the optimizer and any unbeaten session with
  positive user effort each count as 100%. By default, sessions are
  weighted by the effort you spent. Set `stats.weight = "uniform"` to
  count each once (see
  [configuration](09-configuration.md)). The Look back selector sets both
  overview efficiency charts and their Commands tables to the same period
  of 30 minutes, hours, days, or weeks. The average and sample count above
  each chart cover that period.
  Its float adds the total finished sessions by type (mark / watch / recall /
  suggest) and the commands recorded in them. Its **Not scored** and
  **Unbeaten** headings have selectable categories underneath, just as in
  the Journal float.
  *You beat the optimizer* counts the scored sessions where your keys cost
  less than the best sequence found, and `<CR>` lists them. Each is a
  sequence the search missed or one the cost model underprices.
- **Journal** — the same efficiency over everything you typed, not only
  finished sessions. Needs the [journal](09-configuration.md) turned on.
  See below.
- **Commands** — one set of tables per band, never added together: the
  Sessions band counts your scored sessions, and the Journal band your scored
  journal runs of the selected length, over the same Look back period as the
  chart. The line over the tables counts comparisons that have a best
  sequence. The efficiency chart also counts unbeaten runs.
  For every command that appears at least ten times across those and the
  optimizer's best sequences for them, the ratio
  `your_share / optimizer_share`, with the two shares beside it. Three
  lists: *Underused commands*, which you use less than the optimizer
  (candidates to practice); *Overused commands*, which you use more
  (potential inefficient habits); and *Never used by the optimizer*. Each
  list starts with five rows. Press `<CR>` on
  *… and N more* to reveal the remaining rows in place or on *Show fewer*
  to collapse them again. The target of `f`/`F`/`t`/`T`/`r`/`R` is
  collapsed to `_`, an insert counts its entry command and editing keys
  such as `<BS>` (typed characters are only totalled), and `ce`/`cw`
  share a row, as do `cE`/`cW`; the row's run list names your split. A `gj` or `gk`
  that crossed to another line shares a row with `j` or `k`; one that
  moved along a wrapped line is listed apart, as `gj (same line)`.
  Under the lists, *Extra typed text* is how much more you typed in insert
  mode than the best sequences did; `<CR>` lists the runs where you typed
  more.
- **Optimizer times** — under each band's chart. Sessions: *Optimizer
  response*, the median response time over the last 30 days; `<CR>` lists
  those sessions, slowest first. Journal: *Movement search* and *Composition
  search*, the search time for retained runs of the selected length; `<CR>`
  opens the chart. These timing views use their own samples independently
  of Look back. The Movement chart groups queries by lines from start to
  goal. The Composition chart groups queries by buffer positions in the larger
  before/after search slice. Each chart shows median milliseconds and
  sample count for searches with a recorded duration and size, including
  searches that finished without a solution. Select a bucket for its exact
  values and any deadline hits. `<CR>` lists its runs, slowest first. p95
  appears from 100 samples.

A scoring is the plugin's scoring version plus the settings a score depends
on. The stats count sessions and rolled-up journal runs from every scoring.
`v` counts only the current one; the stats then say how many runs it hides.

### The Journal section

The journal is cut into back-to-back runs of 4, 8 and 16 commands, and each
run is scored as if you had ended a session there. A run's score is the
effort of the best sequence found over the effort of what you typed.

In the stats:

- **Run length** — each available length in brackets, with the selected length in bold
  (8 commands by default). Use `h` or `l` on the row to switch lengths. Longer runs give the
  optimizer more to merge, so they usually score lower.
- **Percent efficiency** — the selected length’s average and sample count for the visible period. Minute and hour bars use timestamped samples. Archived daily totals also contribute to day and week bars.

If some of the journal still needs scoring under the current settings, its
coverage percentage appears above the chart in the float. The float title
shows the overall efficiency, run count and run length. A run that ends
where it started counts as all waste. Below the chart:

- **Movement only / With edits** — one bar, split by whether the text
  changed. Each caption shows efficiency, then the run count. The decimal in
  parentheses is the group's share of all scored runs of the selected length.
  Their colors are `VimficiencyStatsMovement` and
  `VimficiencyStatsEdit`.
- **Size** — the label to the left of the ranges under the second bar,
  which uses the same size groups as the search-time charts. For movement and
  edits, size is the larger of the before and after position counts in the
  unpadded region. That region covers both
  cursor rows and all net changes. Matching rows are trimmed from the top and
  bottom up to the cursors, so inserting or deleting a line leaves an unchanged
  tail out of the count. Each byte in a line counts as one position, and an
  empty line counts as one. Runs without a recorded size are drawn shaded (`░`).
- **Not scored** — runs left out of the score, in `VimficiencyStatsNotScored`.
  The rows under it say why: the changed region was too large, the search
  found nothing, or the search failed.
- **Unbeaten** — runs where no modeled sequence came close to your keys,
  scored as 100%, in `VimficiencyStatsUnbeaten`. The rows under it count the
  runs with a bare paste (`p`, `P`, `gp`, `gP`) or undo or redo (`u`, `U`,
  `<C-r>`). A run with both counts under each. *Other* counts runs with
  neither. Rolled-up runs contribute only to the heading total because
  their command details are gone.

`<CR>` on a row under either heading lists its runs.

Every segment of either bar is an item: `h` and `l` move along a bar, `j`
and `k` between them, and the float's title has the segment's efficiency and
run count. `<CR>` opens the run list for the selected segment.

A run is dropped, not scored, when the journal cannot account for everything
in it: you switched buffer or window, an ex command or mouse click changed
something, or you paused for more than 30 seconds.

The first background scoring check runs 30 seconds after startup or on an
earlier idle event. After that, Neovim checks for new journal data when idle
and starts another low-priority run at most once every five minutes.
While the worker runs, the Journal section shows the percentage of recorded
journal bytes scored under the current settings, for example `Scoring journal… 42%`.
The open Stats view checks progress once a second. The efficiency average
returns when scoring finishes.

After a settings change, the journal is rescored under the new settings. Until
that finishes, runs scored under the old settings are left out, never mixed in.
Rolled-up runs keep the scoring they were rolled up under, since their journal
is gone.
`:Vimfy stats refresh` scores right away, which is also how to score with
`journal.sampling.auto = false`.

### Where the data lives

Every finished session (mark, watch, recall, suggest) is appended to
`stdpath("data")/vimficiency/sessions.vfs`, with your keys, its cost and the
single best sequence found.

Its full result, with the text it changed and every sequence found, is saved
beside the log in `sessions/` (see
[7a. Session storage](07a-session-storage.md)). The log record names that
file, which is how a run list finds a session's text.

Each session is stamped with the scoring version and every setting its score
depends on: keyboard, effort weights and optimizer overrides. The stats count
every stamp's sessions; `v` in the stats tab keeps only the current stamp's.

The JSON log of earlier versions, `sessions.jsonl`, predates stamps. It is
moved to `legacy/sessions.v1.jsonl` and no longer read.

## Tab completion

Works on:

- Subcommands (`:Vimfy s<Tab>` → `start`, `stats`, `suggest`).
- Named aliases for `start`, `watch`, and `finish`.
- `3s`/`5s`/`10s`/`30s` hints for `recall`.
- Live aliases plus time hints for `close`.
- Every alias (including `@` and `$`) plus pins for `view`, `explore` and
  `pin`. Saved sessions are left out: there is one per finished session.
- Pins for `unpin`, saved filenames for `rm`.
- `on` / `off` / `toggle` for `suggest`.
- `refresh` for `stats`.

## Scratch output buffer keys

Press `:h g?` in a Vimfy scratch output buffer to open this section
directly. This applies to buffers opened by `:Vimfy help` and other
detail panes that show Vimfy text output in a split.

- `q` — close the current Vimfy output buffer
- `?` — show the short keymap summary popup
- `g?` — open the full help section for scratch output buffers
