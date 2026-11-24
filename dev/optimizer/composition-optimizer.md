# CompositionOptimizer
- Finds the best way to perform a buffer transformation by breaking it down into a series of transform regions to be completed in order.
- Heuristic: Estimated suffix cost of transforms not completed + distance to next transform (+ effort)

## Diff Generation
- Our first step is to generate planned edit regions. The **default is
  `VimDiff`** (`composition:diffAlgorithm=0`), a Vim-costed partition planner.
  It searches at character granularity and prices approximate keystrokes:
  inserted-text effort plus insert entry + `<Esc>` per typed region, and
  counted-tiling movement and deletion oracles (`{k}w`/`{k}dd`-shaped commands
  with a concave count penalty), plus a fixed penalty per region. Details in
  `dev/optimizer/diff-generation.md` § VimDiff.
- `composition:diffAlgorithm=1` switches back to the historical character-level
  `MyersDiff` analysis (similar algorithm to git). It is kept as a fast fallback
  and baseline, not as the preferred planner. It separates individual diffs
  by local heuristics:
  - Match count >= MIN_MATCH_LENGTH -> separate, but otherwise merge
  - Don't count matches across new lines as much (likely share much tab whitespace)
  - Don't include boundary at end, cut off exactly since no more content
  - Have exceptions for well-formed short content inside "", (), etc. (To be expanded upon)
- Using the selected generator, we track intermediate buffer states, compute
  suffix cost sums, and calculate a transform result for each planned edit
  region.

## Composition Logic
- By abstracting away direct edit commands into planned edit transitions over diffs, our search alternates between call types:
  - Intra-transform: refer to TransformResult to apply the current planned edit
  - Inter-transform: move from one transform end to the next transform start (`NavOptimizer::optimize` with a `CharInterval` goal)
- The motion goal is the edit's solved start positions, not the whole diff region, split at the cursor. NavOptimizer never expands a start that already lies in its goal, so a cursor inside the region but past the solved starts would otherwise dead-end. Solved starts have so far always formed one run from the region's beginning (112/112 journal regions, uniform keyboard), but nothing guarantees it.
  - Trip wire: a gap between solved starts. Landings in the gap dead-end and use up nav result slots, so a real start past the gap can be missed.
  - If that shows up, give NavOptimizer a goal set rather than one interval. A hull interval plus excluded positions keeps its heuristic and directional pruning unchanged; a union of ranges would rewrite both.
  - `JournalNoResult.StartDistribution` in `tests/Debug/JournalNoResult.cpp` measures the run shape over a journal directory.
- Nav can land with `col < targetCol`, while Transform results are computed with `targetCol == col`. Each `Result` carries an `EntryCurswant` class, set when its first command is explored:
  - `Ignored`: the first command does not read curswant (charwise edits, joins, changes).
  - `Followed`: a lone `dd`-style delete; it lands at `min(entry curswant, lastCol)` and resets curswant there.
  - `Required`: such a delete followed by more commands; usable only when `targetCol == col`.
- `TransformResult::forEachEditFrom(cursor, linesAfter, visit)` is the one place that applies this. Movement is explored only when it visits nothing.
- By storing the previous edit command, we can also quickly check if . will work. (TODO)

### Explore boundary

`Explore::Session` is intentionally coupled to a narrow per-edit contract from
composition, not to the composition optimizer's full internal shape. The
supported boundary is `CompositionResult::plannedEditAt(i)`, which bundles:

- the diff for planned edit `i`
- the pre-transform fencepost
- the post-transform fencepost
- the `TransformResult` for that same planned edit

Raw `CompositionPlan` / diff vectors still exist for diagnostics and tests, but
they are lower-level surfaces. Refactors that preserve final optimizer results
while changing this per-edit bundle can still break Explore.

## J (Join Lines) Plans

When a diff has more source lines than target lines, the `J` command can collapse lines more cheaply than retyping content. The composition optimizer pre-computes `JoinPlan`s for eligible diffs and offers them as alternative transform transitions in the A* search.

### Why J lives at the composition level

TransformOptimizer already searches normal in-diff `J`, `gJ`, and counted join edits. A composition-level join plan exists only for a boundary-crossing `J` that must be executed from outside the diff-local transform start positions.

Example: changing `aaa\nbbb` to `aaa bbb` can produce the scoped diff `\nbbb` -> ` bbb`. The physical `J` command must start on the `aaa` line, not inside the scoped deleted text. Composition owns that activation point because it operates on the full intermediate buffer.

Invariant: every `JoinPlan` sequence starts with a join (`J`, `gJ`, or a counted one). If the useful sequence would start with a residual edit, movement, or any non-join action, it is not a composition join plan; normal transform/composition search owns it.

### Algorithm: Partition -> J per Group -> TransformOptimizer Residual

For a diff with N source lines -> M target lines (N > M):

1. **Partition** N source lines into M contiguous groups, where group k maps to target line k
2. **Match quality check** -- if joined group content is too different from target (common prefix+suffix ratio < 0.3), skip J for this diff. A single-target-line diff with a non-empty prefix and suffix skips this check: text survives on both sides of the deleted newlines, so a join is required
3. **Entry gate**: skip the plan unless the first group needs at least one `J`
4. **Per group**: simulate both `J` and `gJ` joins using `VimCore::joinLineRange` semantics, run TransformOptimizer on each single-line residual (joined line vs target line), and keep the cheaper of the two; when both leave the same line, only `J` is tried
5. **Assemble**: concatenate all groups' sequences (joins + residual + `j0` between groups) into one `JoinPlan`

A group's count is not searched: the partition fixes it, since a group of `n` source lines is exactly `n - 1` joins. Only the spelling is chosen, the cheapest of `JJJ`, `J..`, and `4J`, which all leave the same text and cursor. The counted form is priced by the same `buildCountedEffort<CountClass::Join>` the transform explorer uses.

A count cannot span planned edits. When the planner splits a join run into one region per newline, each region still contributes its own `J`.

**Partition finding:**
- M=1 (most common): trivial -- all N lines form one group
- M>=2: DP in O(N^2 * M), minimizing length mismatch between joined groups and target lines

### JoinPlan struct

```cpp
struct JoinPlan {
  Sequence sequence;
  CursorPos goalPos;
  double effort;   // includes penalty
  double penalty;  // count penalties of the joins and residual edits
  int entryLine;
};
```

Because the first action is a join, activation is column-insensitive: Vim joins the cursor line with the next line and lands at the join point regardless of the starting column on `entryLine`.

### Integration into A* search

J plans are explored as **additional** transform transitions, independent of the regular TransformResult path:

```cpp
// Regular transform path
if (res) {
    ctx.exploreEditTransition(s, res->sequence, ...);
}

// J plan path: offered from any column on the entry line
if (joinPlan && pos.line == joinPlan->entryLine) {
    ctx.exploreEditTransition(s, joinPlan->sequence, ...);
}
```

When the cursor is not on the J plan's entry line, a dedicated motion search finds paths to the entry line (full line range, not just the transform region). This handles cases where the transform region has virtual positions that regular motion search can't reach (e.g., `endPos` past end-of-line for `\n` -> ` ` diffs).

A* naturally picks whichever path (regular transform, J plan, or text object) produces the lowest cost.

### J simulation fidelity

J plan computation uses `VimCore::joinLineRange` which matches Neovim's semantics:
- Strips leading whitespace from the joined line
- Adds a space between lines (unless current line ends with whitespace or joined line is empty)
- Cursor lands at original line length (position where join occurred)
- `joinspaces` option: adds 2 spaces after `.`, `!`, `?`

### Suffix cost integration

J plan efforts are included in `computeSuffixEditCosts()` alongside regular transform costs, improving the A* heuristic by reflecting cheaper J alternatives.

### Files

| File | Role |
|------|------|
| `JoinPlan.cppm` | JoinPlan struct definition |
| `CompositionSearchContext.cpp` | `computeJoinPlans()` implementation |
| `CompositionOptimizer.cpp` | A* integration (J plan exploration + entry line motion search) |

## Quote/Bracket Motions
- Motions like ci"/cab allow us to combine an Inter-transform with the next Intra-transform.
- Since we are moving in order, quotes are only valid if the first quote on this line, and the quote after that are within the next transform region.
(Note "within" could be one outside region, as doing iw -> still only affect what is inside. Greedily picking inside if matches is optimal)
- Brackets are a bit more complex because they must form a MATCHING pair, but we can simply track bracket depth with a stack (balance of 0 = will search right), and we can leverage the fact that actions will match with the outermost bracket in region
- Thus, we can use a bitmask prefix for quotes, matching with FIRST pair in region, and count prefix for brackets, matching with OUTERMOST pair in region.
- Since each planned edit can introduce new destructive content, we must calculate this context per planned edit.

## Pure Insertions
- Because a pure insertion has no starting point, we must handle it from the higher composition level.
- We perform a similar movement search, but augmented with the option of using o/O if we need a new line, and I/A in place of a final $/^ movement, and i/a otherwise.
- Each strategy defines a range of valid cursor positions from which its mode-entry command produces the correct edit:
  - `o`: any column on the line above (new-line insertions, whether anchored as a trailing `\n` at col 0 or a leading `\n` at end-of-line)
  - `O`: any column on the line below the new line (same anchorings; the only line-open entry above line 0)
  - `i`: exact insertion column only
  - `a`: the column before the insert point (col 0 on an empty line)
  - `I`: any column on the target line, when the insert point is where `I` lands (first non-blank, or past the indent on an all-whitespace line)
  - `A`: any column on the target line (appends at end-of-line)
- Strategies are enumerated in that order, so on an exact cost tie the lowercase entry wins (`Analyze` sorts stably).
- Line-open entries only apply when the planner anchors the insert at a line boundary. A new line sharing the next line's indent may be anchored mid-line instead (`"b\n    "` at col 4 rather than `"    b\n"` at col 0), which rules out `o`/`O`.
- The mode-entry command determines the actual insert position independent of where in the range we land, so the final cursor position after typing + Esc is always `transformResult.goalPos`.
- When navigating to the valid range, the `NavBoundary` must use the full subset extent, not the target range (see `dev/optimizer/buffer-slicing.md` § Boundary vs Target Range).

### Autoindent in Pure Insertions

For `o`/`O`/`A`/`I`/`i` with multi-line insertions, `buildTypedCommands` (from `src/Optimizer/BuildTypedCommands.cppm`) applies autoindent strip/backspace/clear logic (see `dev/optimizer/transform-optimizer.md` § Autoindent Handling):

| Command | Source indent | Line prefix |
|---------|--------------|-------------|
| `o` | `leadingWhitespace(currentLines[targetLine])` (the line above) | (empty) |
| `O` | `leadingWhitespace(currentLines[targetLine])` (the line below) | (empty) |
| `A` | (empty) | current line |
| `I` | (empty) | text before first non-blank |
| `i` | (empty) | text before cursor |

The source indent is provided by autoindent on the first typed line after mode entry. The line prefix is used to compute continuation autoindent for subsequent lines after `<CR>`.
