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
| `:Vimfy save <alias> [<filename>]`         | Copy a finished result to disk (keeps the workspace copy). Filename defaults to the alias. |
| `:Vimfy store <alias> [<filename>]`        | Move a finished result to disk (removes from workspace). |
| `:Vimfy fetch <filename> [<alias>]`        | Copy a saved result from disk into the current workspace. |
| `:Vimfy play <alias>\|<filename> [count]`   | Animate results side-by-side (memory first, disk fallback). |
| `:Vimfy focus <N>`                         | Focus the active replay on the Nth buffer (full-screen).|
| `:Vimfy escape`                            | Restore the side-by-side replay layout.              |
| `:Vimfy view [<alias>\|<filename>]`         | View a finished result full-screen (no arg lists saved files). |
| `:Vimfy rm <filename>`                     | Delete a saved result from disk.                     |
| `:Vimfy list`                              | Open the interactive session picker (see below).     |
| `:Vimfy stats`                             | Open the stats tab (see below).                      |
| `:Vimfy stats refresh`                     | Score the journal now, without opening the stats.    |
| `:Vimfy suggest <on\|off\|toggle>`         | Runtime toggle for auto-suggest (works with zero config). |
| `:Vimfy config`                            | Show the current configuration.                      |
| `:Vimfy reload`                            | Rebuild and hot-reload Vimfy with live build output. |
| `:Vimfy version`                           | Show the plugin version.                             |
| `:Vimfy help`                              | Show the command list.                               |

## Aliases and filenames

Every handle you type is either an **alias** (a session in memory) or a
**filename** (a saved result on disk).

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
| `close` / `save` / `store`   | Any alias.                                          |
| `play` / `view` / `explore`  | Any alias or a filename (memory first, then disk).  |
| `fetch` / `rm`               | A filename (`fetch` also takes the new named alias). |

`@` is handy when the session you want to keep is a recall window whose alias
(`3s`) is moving out from under you:

```
:Vimfy recall 3s
:Vimfy save @ nested-refactor
```

`:Vimficiency` is accepted as a full-name alias for `:Vimfy`.

## Workspace vs. storage

Finished sessions live in two places: **workspace** (in-memory,
indexed by alias) and **storage** (on disk, indexed by filename). The
namespaces are separate; the same text can name both. Move results
between them with `save` / `store` / `fetch`; see
[7a. Session storage](07a-session-storage.md) for the full model and
collision rules. Omitting the second argument on `save` / `store` /
`fetch` repeats the first argument verbatim; `fetch` requires the alias when
the filename is not letters-only.

## Session picker (`:Vimfy list`)

Opens a centered float with two panes — **Active** (in-memory workspace
sessions) and **Saved** (files under `stdpath("data")/vimficiency/saved`)
— and a live-filter prompt at the bottom. `<Tab>` toggles between panes.

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
| `<Tab>`    | Switch Active ↔ Saved pane                            |
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

- `<CR>` — on the Active pane, opens a `:Vimfy play`-style view of a
  finished session; on the Saved pane, fetches the file into the
  workspace and switches focus to Active.
- `d` — on Active, discards the session (equivalent to `:Vimfy close`);
  on Saved, deletes the file on disk.
- `r` / `y` — on Saved, renames/duplicates the file. On Active, renames
  the named alias / registers a duplicate finished record under a new
  alias. Only entries with a stable name alias are renameable this
  way; for recall and auto-suggest results, save to disk first (`:Vimfy
  save <alias> <filename>`) and then rename from the Saved pane.

**Blocked on in-progress sessions:** `<CR>`, `r`, `y` all refuse to run
on a still-accumulating session. Finish it with `:Vimfy finish` (or let
Watch's idle trigger fire) and try again.

## Session stats (`:Vimfy stats`)

Opens a tab with a header over one full-width pane:

- **Header** — the scorings counted, the weight and the charted run length,
  over the path of the session log the stats are read from.
- **Stats** — Sessions, Journal, the Commands tables, then Dev.

In a wide window the Commands tables sit to the right of Sessions and Journal.
`l` past the right edge of the left side goes to the first command, and `h`
from any command goes back to the chart.

The stats are laid out as items: each row that lists runs, each chart,
and each selector row. One item is always current. It is highlighted whole and
the cursor itself is hidden.

`h`, `j`, `k` and `l` move to the item on that side, and the other keys act on
the current one. Any other motion takes the nearest item.

Keys in the stats (`?` lists them):

| Key             | Action                                                      |
|-----------------|-------------------------------------------------------------|
| `j` / `k`       | The item below / above; a count moves that many.            |
| `h` / `l`       | Change the active option on a selector row, or move to the item to the left / right. The arrow keys work the same way. |
| `<CR>`          | Open the runs for a result row or expand a chart over the whole screen. |
| `w`             | Switch the efficiency weight between effort and uniform for this view. |
| `v`             | Count every scoring, or only the current one (see below).   |
| `q`             | Close the tab.                                              |

`<Esc>` or `q` closes the float opened by `<CR>`. A
chart fills its column in the stats and the whole screen in its float.

The stats keep each section to its headline: the Sessions efficiency, the Journal's
run lengths, and a chart each. The rest of a section is under the chart in its
float.

The float's chart is as tall as the screen allows and draws two lines:
the efficiency, solid, against the left axis, and the number of samples
behind it, dotted, against the right. Each bucket's own value is a larger dot, placed to the nearest row, and
the lines only join them; the float's title has the exact value. A line breaks where there are no samples. Their colors
are `VimficiencyStatsEfficiency` and `VimficiencyStatsVolume`.

In the chart's float each day with runs is an item, named with its efficiency in the
title. `h` and `l` move between the days and `<CR>` lists a day's runs. The
rows under the chart list their runs the same way.

The run list floats over the tab at its full width, one line per session or
journal run: when, your effort, the best found, the saving, the extra
characters you typed in insert mode and the search time. Its title holds the row it came from, the sort and the page. It takes
two fifths of the screen and the preview under it the rest.

The preview follows the current run. It shows your keys and the best sequence,
spaced by command and cut to two lines each. Under them is the run's text
before and after, side by side, with the changed lines marked and the cursor's
place shaded in each.

A session keeps no text, so its preview is the keys alone. Runs older than
`journal.keep_days` are rolled up: they count in the total but are not listed.

Every cell of the list is an item, as in the stats:

| Key           | Action                                                        |
|---------------|---------------------------------------------------------------|
| `j` / `k`     | The run below / above, in the same column.                    |
| `h` / `l`     | The column to the left / right. The arrow keys move the same way. |
| `s`           | Sort by the current column: largest first, then smallest first, then off. Unsorted, the worst runs come first (your effort minus the best). |
| `<CR>`        | On a journal run: replay it as `:Vimfy play` does, your keys beside the best sequence, highlighted as the file was. `q` there returns to the list. |
| `]` / `[`     | Next / previous page of runs.                                 |
| `<Esc>` / `q` | Back to the stats.                                            |

Sections:

- **Sessions** — your percent efficiency: the best effort found over the effort
  you spent, `min(best / yours, 1)`, averaged over sessions. It never passes
  100%: a session that beats the optimizer counts as 100%. By default each
  session counts by the effort you spent on it, so long sessions count more;
  set `stats.weight = "uniform"` to count each once (see
  [configuration](09-configuration.md)). Look back groups the chart into 30 minutes, hours, days, or weeks. The average and sample count above the chart cover that period.
  Its float adds the total finished sessions by type (mark / watch / recall /
  suggest), the commands recorded in them, and the sessions that were
  unbeaten or had no solution, which are counted but not scored.
  *You beat the optimizer* counts the scored sessions where your keys cost
  less than the best sequence found, and `<CR>` lists them. Each is a
  sequence the search missed or one the cost model underprices.
- **Journal** — the same efficiency over everything you typed, not only
  finished sessions. Needs the [journal](09-configuration.md) turned on.
  See below.
- **Commands** — for every command that appears at least ten times
  combined across your scored sessions and journal runs and the
  optimizer's best sequences for them, the ratio
  `your_share / optimizer_share`, with the two shares beside it. Three
  lists: *Underused commands*, which you use less than the optimizer
  (candidates to practice); *Overused commands*, which you use more
  (potential inefficient habits); and *Never used by the optimizer* (the
  five you use most). The target of `f`/`F`/`t`/`T`/`r`/`R` is
  collapsed to `_`, an insert counts its entry command and editing keys
  such as `<BS>` (typed characters are only totalled), and `ce`/`cw`
  share a row, as do `cE`/`cW`; the row's run list names your split. A `gj` or `gk`
  that crossed to another line shares a row with `j` or `k`; one that
  moved along a wrapped line is listed apart, as `gj (same line)`.
  Under the lists, *Extra typed text* is how much more you typed in insert
  mode than the best sequences did; `<CR>` lists the runs where you typed
  more.
- **Dev** — how long the optimizer took per session over the last 30 days.

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

Under the chart in its float:

- **Efficiency** — the Sessions efficiency, for runs of the charted length, and how
  many runs it covers. A run that ends where it started counts as all waste.
- **Scored under the current settings** — how much of the journal that is.
- **Movement only / With edits** — the charted runs split by whether the
  text changed.
- **Not scored** — runs left out of the score: the changed region was too
  large, the search found nothing, or the search failed.

A run is dropped, not scored, when the journal cannot account for everything
in it: you switched buffer or window, an ex command or mouse click changed
something, or you paused for more than 30 seconds.

The first background scoring check runs 30 seconds after startup or on an
earlier idle event. After that, Neovim checks for new journal data when idle
and starts another low-priority run at most once every five minutes.
The Journal section shows `Scoring journal…` in place of the average while
the worker runs and refreshes the average when it finishes.

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

Each session is stamped with the scoring version and every setting its score
depends on: keyboard, effort weights and optimizer overrides. The stats count
every stamp's sessions; `v` in the stats tab keeps only the current stamp's.

The JSON log of earlier versions, `sessions.jsonl`, predates stamps. It is
moved to `legacy/sessions.v1.jsonl` and no longer read.

## Tab completion

Works on:

- Subcommands (`:Vimfy s<Tab>` → `save`, `start`, `stats`, `store`, `suggest`).
- Named aliases for `start`, `watch`, and `finish`.
- `3s`/`5s`/`10s`/`30s` hints for `recall`.
- Live aliases plus time hints for `close`.
- Every alias (including `@` and `$`) for `save` and `store`.
- Every alias plus saved filenames for `play`, `view`, and `explore`.
- Saved filenames for `rm` and `fetch`.
- `on` / `off` / `toggle` for `suggest`.
- `refresh` for `stats`.

## Scratch output buffer keys

Press `:h g?` in a Vimfy scratch output buffer to open this section
directly. This applies to buffers opened by `:Vimfy help` and other
detail panes that show Vimfy text output in a split.

- `q` — close the current Vimfy output buffer
- `?` — show the short keymap summary popup
- `g?` — open the full help section for scratch output buffers
