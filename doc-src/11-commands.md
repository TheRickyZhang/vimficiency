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
| `:Vimfy stats`                             | Show lifetime session stats (see below).             |
| `:Vimfy stats refresh`                     | Score the journal now, without opening the stats.    |
| `:Vimfy suggest <on\|off\|toggle>`         | Runtime toggle for auto-suggest (works with zero config). |
| `:Vimfy config`                            | Show the current configuration.                      |
| `:Vimfy reload`                            | Rebuild the C++ library (needs restart).             |
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

Opens a read-only float summarizing your lifetime activity. Close with
`q` or `<Esc>`.

Sections:

- **Lifetime** — total finished sessions, broken down by type (mark /
  watch / recall / suggest), and the commands recorded in them.
- **Efficiency** — a score out of 100: the best effort found over the
  effort you spent, `min(best / yours, 1)`, averaged over sessions. By
  default each session counts by the effort you spent on it, so long
  sessions count more; set `stats.weight = "uniform"` to count each once
  (see [configuration](09-configuration.md)). Sessions with no solution
  are counted but not scored. The 30-day block below is the daily score
  in local time (▁ = 0, █ = 100; blanks = no sessions that day).
- **Journal** — the same score over everything you typed, not only
  finished sessions. Needs the [journal](09-configuration.md) turned on.
  See below.
- **Commands** — for every command that appears at least ten times
  combined across your scored sessions and journal runs and the
  optimizer's best sequences for them, the ratio
  `your_share / optimizer_share`. Two lists: what you use *more* than
  the optimizer (potential inefficient habits) and what you use *less*
  (candidates to practice). The target of `f`/`F`/`t`/`T`/`r`/`R` is
  collapsed to `_`, an insert counts its entry command and editing keys
  such as `<BS>` (typed characters are only totalled), and `ce`/`cw`
  share a row, as do `cE`/`cW`, with your split shown.
- **Dev — Optimizer beats queued** — count of finished sessions where
  your sequence came in cheaper than the optimizer's best result.
  **Beats are interesting-to-study, not ground truth**: many are
  cost-model artifacts or unexplored regions, so they are *not* fed back
  into training without manual review. Optimizer time covers the last
  30 days.

Scores are only compared under one scoring: the plugin version and the
settings a score depends on. Sessions scored otherwise are counted, and
`<Tab>` switches to them.

### The Journal section

The journal is cut into back-to-back runs of 4, 8 and 16 commands, and each
run is scored as if you had ended a session there. A run's score is the
effort of the best sequence found over the effort of what you typed.

- **Score** — the Efficiency score, for runs of 8 commands. A run that ends
  where it started counts as all waste.
- **30-day chart** — the same number per day.
- **By run length** — the score at each run length. Longer runs give the
  optimizer more to merge, so they usually score lower.
- **Movement only / With edits** — the 8-command runs split by whether the
  text changed.
- **Not scored** — runs left out of the score: the changed region was too
  large, the search found nothing, or the search failed.

A run is dropped, not scored, when the journal cannot account for everything
in it: you switched buffer or window, an ex command or mouse click changed
something, or you paused for more than 30 seconds.

The journal is scored in the background at low priority: once shortly after
startup, then whenever Neovim is idle and the journal has grown, at most every
five minutes. Opening the stats never waits for it. The section shows what is
scored so far and how much of the journal that covers, and redraws when a run
in progress finishes.

After a settings change, the journal is rescored under the new settings. Until
that finishes, runs scored under the old settings are left out, never mixed in.
`:Vimfy stats refresh` scores right away, which is also how to score with
`journal.sampling.auto = false`.

### Where the data lives

Every finished session (mark, watch, recall, suggest) is appended to
`stdpath("data")/vimficiency/sessions.vfs`, with your keys, its cost and the
single best sequence found.

Each session is stamped with the plugin version and every setting its score
depends on: keyboard, effort weights and optimizer overrides. Scores only
compare under the same stamp, so the Efficiency section shows one stamp at a
time, the current one first. `<Tab>` in the stats window steps through older
stamps. Lifetime counts include every stamp.

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
