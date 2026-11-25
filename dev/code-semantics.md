# Code Semantics

## Core terms

A key is physical, a token is a parsed unit, and a sequence is a Neovim command string. A
`KeyedSequence` pairs a sequence with its physical keys. A sequence binding adds the running
effort for a keyboard configuration.

A movement changes cursor position while preserving text. An edit changes text, mode, or both.
Nav and Transform describe optimizer goals. Nav can search movements, jumps, and finds to
reach a position. Transform searches for text changes. Keep Vim's own motion names at the
simulation layer, as in `VimMotionUtils`.

## Ranges, positions, and coordinates

| Names | Contract |
|---|---|
| `begin`, `end` | Half-open `[begin, end)` for diff, edit, and slice bounds. The end can be a virtual position. |
| `first`, `last` | Inclusive `[first, last]` for motion targets. A point goal has `first == last == goalPos`. |
| `Pos` | Zero-based line and column for geometry, including diff endpoints and indexed landings. |
| `CursorPos` | Adds Vim's desired column (`targetCol`) for simulation, search state, and Nav's cross-line distance heuristic. |
| `local`, `global` | Coordinates in a slice and in its parent buffer. Add `lineOffset` to map a local line to the borrowed index. Subtract it on return. |

Convert a half-open `CharRange` to an inclusive `CharInterval` at the Nav boundary. Keep the
endpoint convention visible in names such as `rangeBegin`/`rangeEnd` and
`rangeFirst`/`rangeLast`.

An exclusive endpoint at `(line, line.size())` stops before the newline. An endpoint at
`(line + 1, 0)` includes the newline in the range. This difference determines whether a
deletion keeps the following text on its original line. `POSITION_OUTSIDE_BOUNDARY` is a
separate sentinel for an attempted crossing.

Use `setCol(c)` when a horizontal action establishes a desired column. Use
`clampColPreservingTarget(c)` for a vertical move that may land on a shorter line. Keep buffer
text in `Line` and `Lines`.

## Goal, target, and boundary

- `goalPos` is the exact post-action cursor position. A pure insertion can have several valid
  starts and one final landing.
- The **target** is the set of positions that count as success. The **boundary** describes the
  surrounding buffer that a search can traverse. Clamping traversal to the target changes what
  `$`, `0`, `^`, or a paragraph motion can reach.
- A **window** is a chosen search extent. A **subset** is the sliced `Lines` passed to an
  optimizer. **Padding** supplies context outside the target. An **indexed range** describes
  the lines covered by a helper such as `BufferIndex`.
  [Buffer slicing](optimizer/buffer-slicing.md)

`Forward` means toward larger buffer positions and `Backward` toward smaller ones. Left/right
and up/down describe one axis. Ahead/behind and undershoot/overshoot are relative to the
current target.

## State and progress

Use `initial`, `current`, `next`, and `goal` for time or execution order. A **step** is a
replay token or immediate user action. A **phase** belongs to the Explore state machine.
Completion is derived from the final navigation phase and cursor.
[Explore phase machine](optimizer/interactive-explore.md#phase-machine)

A **planned edit** advances from fencepost `i` to `i + 1`. Use `plannedEdit` or
`plannedEditAt(i)`. `editsCompleted` counts finished edits and `linesAfterNEdits_[n]` holds
the buffer after exactly `n`. A plan with `N` edits has `N + 1` fenceposts, including the
initial buffer.

## Effort, counts, and search accounting

| Term | Meaning |
|---|---|
| Effort | Typing difficulty of the key sequence alone. |
| Distance | Estimated closeness to a target. |
| Heuristic | Estimate of remaining search work. |
| Cost | The score assigned to a state or result. A search priority can combine effort with heuristic distance. |
| Penalty | An adjustment for a specific rule, such as a count prefix. |

For parsed movements and edits, count zero denotes an omitted count and executes once. A
**prefix count** is emitted in the command. A **repeat count** describes repetitions. A
counted edit can have different semantics from repeating an edit. Search parameters
`minPrefixCount` and `maxPrefixCount` bound the prefixes the optimizer explores. The usual
minimum is two, and a minimum above the maximum disables that range.
[Counted edit semantics](core/counted-edit-semantics.md)

`totalPops` includes stale queue entries. `nodesExplored` counts processed nodes.
`statesSkipped` records skips, `resultsFound` counts results, and `uniquePositionsFound`
counts distinct endpoint positions in a range search.

## Validity and naming

An **invalid position** is malformed or carries a sentinel. An **outside-boundary** move
crosses a traversal limit. A **stale** queue entry has been superseded. A **no-op** is a valid
Vim command that leaves state unchanged.

| Type suffix | Meaning |
|---|---|
| `Params` | Caller-controlled tuning settings |
| `Context` | Derived environment or state reused across calls |
| `State` | One search node or mutable machine state |
| `Result` | Output in coordinates the caller can use |
| `Plan` | Precomputed executable strategy |
| `Boundary` | Traversal constraints |
| `Flags` | Compact capability mask |

Use `is*` for classification, `has*` for presence, `can*` for legality, and `should*` for a
policy decision. `use*`, `allow*`, and `track*` name caller-configurable options.
