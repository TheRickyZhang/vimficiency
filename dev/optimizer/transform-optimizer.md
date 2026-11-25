# TransformOptimizer (Transform Layer)
- Historical name retained. Conceptually this is the transform optimizer: it searches for buffer/mode-changing transitions from any valid start position to a goal state.
- It is broader than direct "edit commands" in the glossary. Replace, paste, and join-style changes still belong to the transform layer even when they are not direct edits.
- The current implementation mostly models transforms as deleting all initial text and typing all goal text (with minor exceptions).
- Heuristic: `distanceWeight` × how much is left to delete, sum(effective columns) + 2 * sum(additional lines), plus the effort of typing the goal text (`typedWeight`) (+ effort)
- Goal states already carry the typing effort. Without the typed term, a finished answer waits behind every unfinished state within that effort.

## Result Structure
There are special transform shapes we note:
- *Pure Deletions*: when goalLines == {""}
- *Pure Insertions*: when initialLines = {""}
Otherwise it is a *Regular Transform*. The following describes regular transform search:

```cpp
struct TransformResult {
  vector<Result> typeAllResults; // Indexed by flat position (no newlines in count)
};
```

## Goal Check Timing
- Because we are dedicated to typing our result text, it is optimal to only go into insert mode at the very end. The best way to have this is to convert the last delete to a change, but since it is messy to do retrospectively, we have slightly different timing to anticipate.
- In Navigation/Composition, we pop state -> check if goal position -> explore commands.
But here, we pop state -> explore deletions -> check if goal, so that we can convert to change before it is committed.
- This is a bit more costly but not significant.
- For pure deletion, a linewise command may remove every line of the slice only when the slice covers the whole buffer. With neighboring lines, the goal retains one line inside the slice. A linewise delete can leave the cursor on a line made entirely of protected text. That line still borders an editable newline, so the search can move to the line above the newline and try a join that preserves the boundary text.

## Multi-source Handling
Every start position searches independently: `startIndex` is part of the state key, so one start never consumes another's states.

### Early Stopping
A start goes inactive once it holds `maxResultsPerStartPos` results, and its remaining queued states are skipped. Ignoring small differences in running effort patterns, this is optimal.

### Per-start Work Bound
A start with no solution would otherwise search its whole space; a 13-position region reached 33k nodes this way. Once any start is solved, a start goes inactive after exploring more than `startWorkFactor` times the nodes the hardest solved start needed before its first result.

The default is 8. On generated multi-line deletions, 4 lost no results and 3 did, and 8 costs about 5% more nodes than 4.

## Distance Weight

`TransformOptimizerParams` replaces the shared `distanceWeight` default of 1 with 0.35. Clearing a character costs well under one unit of effort, so at weight 1 a finished answer pops before cheaper paths that are one command short. A four-line delete returned `4dd` (5.5 on `qwerty`) and never reached `dj.` (3.8).

On the user's history (`HeuristicShapes.Policy`, `uniform` / `qwerty`), with loss as mean effort above the best known answer per edit:

| Weight | Loss | ms per edit | Slowest edit (ms) |
|---|---|---|---|
| 1 | 0.275 / 0.238 | 0.7 / 0.6 | 63 / 62 |
| 0.35 | 0.135 / 0.094 | 1.1 / 1.7 | 80 / 334 |
| 0.12 | 0.079 / 0.049 | 6.0 / 5.3 | 577 / 553 |

The step to 0.35 costs 6–18 ms of search per unit of effort saved; the next step, to 0.25, costs about 35. The extra search sits in multi-line regions that begin and end mid-line, and most other edits would still gain from a lighter weight.

Measured and not adopted:
- A weight per size band: no gain on held-out edits.
- Concave shapes (`colExponent` or `breakExponent` below 1): more nodes for the same answers.
- A lighter weight for regions starting at column 0: it holds out, but only as a stand-in for "the light search is cheap here".
- Another `breakCost`: 1.2 to 2 are indistinguishable, and loss rises just above 2.
- Not built: try a lighter weight first and rerun at 0.35 when it exceeds a node budget. Simulated, 0.18 under a 5k-node budget gives 0.080 / 0.059 loss at 2.3 / 3.6 ms per edit, against 1.0 / 1.7 ms for 0.35 in that run.

`shared:` overrides do not reach a planned edit's transform search, only `transform:` ones do, so the `optimizer` config table leaves this weight alone.

## State Hashing (CostMap Keys)

The A* search tracks queued states in an `unordered_map<TransformStateKey, QueuedCost>` (the costmap): the best cost queued for a state, and how many arrivals at that cost were admitted. Each state is keyed by `(buffer content, cursor position, mode, startIndex)`. Since the buffer mutates during search (deletions, joins), the key must capture buffer identity.

A state admits at most `maxResultsPerStartPos` arrivals at its best cost, since more cannot give the start another result. Admitting all of them expands every ordering of equal-cost commands (`x`, `X`, `.`), which triples per depth.

`TransformEditorState` carries the editor snapshot used by both simulation and search: `Lines`, cursor, mode, and a precomputed 64-bit FNV-1a hash over all buffer content. This hash is:
- Computed when an editor snapshot is constructed
- Refreshed by `TransformSimulator` after buffer mutations (`afterDeletion`, `afterLinewiseDeletion`, `afterJoin`, and counted variants)

`TransformState` wraps that editor snapshot with A* path/ranking data: sequence, running effort, cached effort, cost, dot-repeat context, and `startIndex`. Queue-ready states are created through `TransformStateFactory`, which records the editor snapshot, command effort, and heuristic cost together. This keeps pure editor simulation separate from ranked search states and avoids half-valid states with stale cost or repeat metadata.

`TransformStateKey` stores `(linesHash, lineCount, line, col, mode, startIndex)` instead of the full `Lines` object. Both the hash function and equality operator use only these scalar fields — no buffer copying or content comparison.

The same pattern applies to `SuffixKey` in the suffix cache (see below).

**Collision risk**: 64-bit FNV-1a over ~10^4 states gives collision probability ~5×10^-12 per search. A hash collision would cause a state to be incorrectly pruned as "already visited," potentially missing a better path for one starting position — a minor quality degradation, not a correctness violation.

## Entry Curswant

Every start state has `targetCol == col`, but callers may enter a result from a clamped cursor (`col < targetCol`). Each `Result` therefore records an `EntryCurswant` class.

Every edit writes curswant, so only a path's first command can read the caller's value. `TransformStateFactory` applies one rule as commands are appended:

- First command: take that command's class.
- Any command after a `Followed` state: the path becomes `Required`.
- Otherwise the class is unchanged.

| First command | Class |
|---|---|
| Charwise delete, join, any change-mode goal emission (`dd` is emitted as `cc`/`0C`) | `Ignored` |
| Linewise delete placed by `deleteLineRangeAndUpdatePos` (`dd`, `{n}dd`) | `Followed`, or `Ignored` when the line it lands on is no longer than the entry column |
| Delete a motion promoted to linewise (`d}`, `dw`, `dge`, …) | `Ignored` or `Followed`, decided by replaying it from an end-of-line curswant; `Required` if neither fits |
| Boundary-escape `j`/`k` | `Required` |
| Visual delete spanning lines (`vj…d`) | `Required` |

Conservative choices, all safe but possibly skipping usable edits at clamped columns:

- A change-mode result that ends right after a `Followed` delete is stored as `Required`, because the fixed `goalPos` cannot follow curswant.
- A suffix-cache hit is `Required` unless its prefix is `Ignored`.

Under `startofline` nothing reads curswant, so `Followed` becomes `Ignored`.

Consumers apply the class through `TransformResult::forEachEditFrom`; see Composition Logic in `composition-optimizer.md`.

## Suffix Cache

When a goal state pops, the suffix cache walks the structured `TransformPathStep` chain recorded on it, caching the remaining suffix at each intermediate buffer state. This avoids reparsing/replaying generated command strings inside the optimizer.

### Filled when a goal pops

A state keeps the first suffix cached for it, so the walk waits until the search has ranked the goal first. Goals are generated in exploration order: filling at generation leaves each state with whichever completion its expansion tried first, and every later start that hits the state inherits it.

On the user's history at `distanceWeight` 1, filling at pop instead of at generation cut mean effort above the best known answer from 0.36 to 0.29 per edit (`uniform`) and from 0.37 to 0.25 (`qwerty`), for 1% more nodes. On edits that type, starts answered exactly as a search without the cache answers them rose from 79–92% to 97–99%.

The goal carries what the walk needs as a `GoalCompletion` on its last path step: the plain completion command, its penalty, and the typed text. A normal goal and its dot variant share one record, because the cache stores the plain completion whichever of the two pops. A goal without a record (a start already on the cleared shell) caches nothing.

### Path-walking cost model

Every `TransformState` carries a `std::shared_ptr<const TransformPathStep>` chain recording the search edges that produced it. Each step stores only cache-key-relevant metadata: `linesHashAfter`, `lineCountAfter`, `posAfter`, `modeAfter`, `lastEditAfter`, and the emitted command string. No full `Lines` snapshot is held.

The chain is a tree, not a linear list: branches that share a parent share the upstream nodes by reference count. As losing branches are discarded by the search, their leaf `shared_ptr`s drop and unreferenced parents free. Only popped goals pay the full walk cost (one walk each, populating intermediate suffix-cache entries).

Cost characteristics:

- **Per-state overhead**: ~one small heap allocation + ~3 strings of bounded length. Allocated at successor-state creation regardless of whether the state ever wins.
- **Goal-reaching walk**: O(path length) suffix-cache entries per popped goal. Never fires during exploration.
- **Memory churn during search**: bounded by live states; goes back as branches die.

Replacing the chain with replay (parse the generated sequence, simulate via the interpreter) was considered and rejected: replay requires a second interpretation layer for what the optimizer already has structurally, and the state metadata is cheaper than parsing.

**Key**: `SuffixKey = (linesHash, lineCount, pos, mode)` — deliberately excludes `startIndex` to enable sharing.

**Dot-context handling**: A cached suffix is ambiguous if it reaches `.` before any edit that resets dot-repeat context. That first dependent `.` is expanded to the explicit command it repeats (e.g., `h.s` → `hxs` when `.` means `x`). Both variants are stored in `SuffixValue`:

- Expanded variant: first dependent dot replaced by the recorded command. Always correct regardless of caller dot context.
- Dot override: original compact suffix. Lower cost, only valid when caller dot context matches the recorded command.

At lookup: if caller dot context matches, use the dot override; otherwise use the expanded variant.

### Hits record early

A hit records a result when the prefix state pops, which is on the prefix's cost and not the completion's. That early result is load-bearing: it is what lets the per-start work cutoff close the other starts, and queueing completions at full cost instead made a four-line change exhaust `maxNodesPopped` with no results.

The hit is therefore only a trigger. `cheapestQueuedGoalByStart` holds the cheapest goal state already queued for each start, and a hit records that goal instead when it beats the cached completion. Without it, a chain of cheap deletions reached a cached suffix first and `x...ceomega<Esc>` beat the queued `Comega<Esc>`.

The goal state is still in the queue afterwards; `commitResult` drops it when it pops, since a bucket never holds one sequence twice.

Subsequent dots (after the first explicit command) are unambiguous.

## Boundary Shift Handling
- Since transforms are naturally the only exact-position constrained region, we have particular method of resolving among flat indices.
- Consider an original buffer with:
Line 5: abchello
Line 6: world
Edit Region: hello world

Then:
`firstLine = 5
firstCol = 3
flatIndex = lineBaseIndex[bufferLine - firstLine] + bufferCol`
With
`lineBaseIndex[0] = -3 (only subtract firstCol on first line)
lineBaseIndex[1] = 5 (afterwards consider cumulative character count)`

Then, converting a real position to flat index takes only 3 operations. This improves over a 5-operation resolution without tracking lineBaseIndex, and a 2-operation resolution would require a lot of storage for looking up region -> flatIndex directly.

## dw/dW → dwi/dWi (not cw/cW)

The delete→change conversion (`deleteToChange`) converts `dw`/`dW` to `dwi`/`dWi` instead of `cw`/`cW` (Note: uppercase variants implicitly included starting from now). This is because vim treats `cw` like `ce` — they don't include trailing whitespace, while `dw` does (GapEdge vs WordEdge).

This applies to full optimizer sequences. `TransformFrontier` does not surface
`dwi` as one recommendation; it suggests `dw`, then Explore handles the residual
insert phase after the deletion is observed.

We unconditionally use `dwi` without checking whether `cw` would be equivalent, because the exploration order makes the check unnecessary. In `exploreAllDeletions`, WordEdge edits (`de`) are explored **before** GapEdge edits (`dw`). When `dw` and `de` produce the same deletion range (no trailing whitespace), `de` reaches the goal first and stores its result. The `dw` callback then hits the early-out `if (result.results[idx].isValid()) return` and is skipped. So `dw` only reaches the goal when `de` didn't, meaning the trailing whitespace is what made `dw` reach the goal.

This is a case where **exploration order affects correctness** of the delete→change conversion. If GapEdge were explored before WordEdge, we would need a per-call equivalence check.

## dd→cc Conversion

Since `cc` keeps the line but `dd` removes it, the conversion must account for the difference in line count.

As `buildCollapseSequence` function generates `<BS>`/`<Del>` keystrokes to collapse extra lines before/after the cursor
TODO may need more elaboration

## Auto-indent Handling

When the buffer enables `autoindent`, a linewise change preserves the original indent bytes. Each subsequent `<CR>` rebuilds the indent according to `copyindent`, `expandtab`, and `tabstop`. The `buildTypedCommands` function (in `src/Optimizer/BuildTypedCommands.cppm`, shared by TransformOptimizer and CompositionOptimizer) accounts for this:


| Case | Condition | Action |
|------|-----------|--------|
| No autoindent | autoindent empty | Type full line |
| Goal matches | goal starts with autoindent | Strip autoindent, type remainder |
| Goal has less indent | autoindent starts with goal's indent | `<BS>` × excess, type remainder |
| Mismatch | neither is prefix | `<C-u>` + type full line |

### Autoindent Source by Line

- **Line 0**: `initialAutoindent` param (from cc's source line, or empty for char-wise)
- **Line 1**: `leadingWhitespace(linePrefix + goalLines[0])` — buffer line 0 is prefix + goal
- **Line 2+**: `leadingWhitespace(goalLines[i-1])` — previous goal line

### Parameters

- `initialAutoindent`: Indent provided on first typed line (cc preserves source line indent)
- `linePrefix`: Text before edit region on first line (for computing line 1 autoindent)
- `suffixLeadingSpaces`: Whitespace to restore on last line for multi-line char-wise edits

### `<BS>` Optimization

`<BS>` in autoindent context **deletes to the previous `shiftwidth` boundary** (default 8), meaning `<BS>` can only land on multiples of `shiftwidth`:

```
autoindent = "                " (16 spaces, sw=8)
goal line  = "        foo" (8 spaces + "foo")
<C-u> + type "        foo" = 2 + 11 = 13 keys
1× <BS> + "foo"             = 1 + 3  = 4 keys  ← better! (16 → 8 in one BS)
autoindent = "     " (5 spaces, sw=8)
goal line  = "  foo" (2 spaces + "foo")
BS would go 5 → 0 (overshoots past 2) — cannot use <BS>, fall through to <C-u>
```

`bsCountForIndent(from, to, sw)` computes the number of `<BS>` presses needed, returning -1 if `<BS>` overshoots past the target (i.e., the target is not reachable via shiftwidth boundaries).

When the target indent is a byte prefix of the source indent, the builder compares backspacing to its display column with clearing and retyping it.

**Important:** Neovim requires `\x08` (ASCII BS) for backspace in insert mode, not `\x7f` (ASCII DEL)

### Pure Deletion

The primary reason we handle pure deletions separately is because we do not need to handle delete -> change conversions. It is implemented very similarly (difference triggered via template), but crucially:
- Positions after the last deletion may be different than the last position, so we add a positions mapping in PureDeletionEditResult:
```
struct PureDeletionEditResult {
  TransformResult transformResult;
  // Per-start goal cursor positions in buffer coordinates (same flat index as TransformResult::getResults()).
  std::vector<Position> goalPosByStart;
};
```
- We can use a normal processing order, ie not duplicate goal checks in anticipation of delete -> change conversion

### Pure Insertion
Handled at the CompositionOptimizer level
