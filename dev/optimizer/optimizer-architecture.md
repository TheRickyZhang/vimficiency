# Optimizer Architecture and Logic

We have three different Optimizers. Here is a short description:
- NavOptimizer: navigation optimizer. Solves cursor-only state transitions from start -> (finish or finishRange) over a constant buffer
- TransformOptimizer: historical code name for the transform optimizer. Solves buffer/mode-changing state transitions; the current implementation mostly models them as delete old text + type goal text
- CompositionOptimizer: orchestrates NavOptimizer and TransformOptimizer to solve full transitions. Determines how to break up transform regions and chain navigation and transform search to satisfy them in order. Also pre-computes J (join lines) plans for diffs where source has more lines than target, offering them as alternative transform transitions in the A* search (see `dev/optimizer/composition-optimizer.md` § J Plans).

Terminology note:
- Motion/Edit are direct command families
- Navigation/Transform are search/state-transition families
- `NavOptimizer` already matches the broader navigation concept; `TransformOptimizer` keeps its historical name even though it is really the transform layer

## Dependence
NavOptimizer: independent
TransformOptimizer: calls NavOptimizer very briefly for specific visual delete.
CompositionOptimizer: calls TransformOptimizer and NavOptimizer

They all share:
- Configuration settings
- Effort for typing the sequence
- Boundary of the subbuffer received. Navigation can have padding to allow for the possibility of overshoot + revert, while transforms must be exact.
- Usage of Endpoint/Range functions for checking if a motion would cross outside the boundary.
- Counted-command cognitive penalties (see `dev/optimizer/count-penalty.md`)
- Return optimizer-specific containers of results. Each `Result` stores a
  single `Sequence`, its cost, its search penalty, and its entry curswant
  requirement. See `src/Optimizer/Result.cppm`.

For more details about how boundaries are handled, see `dev/core/boundary-logic.md`.

### Exploring Valid Commands

All commands use the endpoint/range methodology. First, we search the span effect that a command will have, breaking early (not searching at all) if it would cross our boundary. Then, for a motion we move the cursor to that endpoint, and for a transform candidate we apply the buffer/mode effect associated with that endpoint or range. This ensures we do not duplicate work.


```cpp
// Forward word
CursorPos endpoint = VimCore::wordMotionEndpoint(
    cursor, lines, spec.target, spec.big, boundary);
if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

// Text objects
CharRange range = VimCore::wordTextObjectRange(
    cursor, lines, VimCore::WordTextObjectKind::Inner, isBig, boundary);
if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
    range.end == POSITION_OUTSIDE_BOUNDARY) continue;
```


| Motion Type | Boundary Check | Endpoint Function |
|-------------|----------------|-------------------|
| Simple line (h, l, 0, ^, $) | Col check | N/A (same-line only) |
| Vertical (j, k) | Line bounds check | Inline |
| Scroll (<C-d/u/f/b>) | Edge line check | `scrollEndpoint()` |
| Word (w, b, e, W, B, E, ge, gE) | Col offset + hasLinesOutside | `wordMotionEndpoint()` |
| Paragraph ({, }) | Edge line check | `motionParagraphEndpoint()` |
| Sentence ((, )) | Edge line check | `sentenceMotionEndpoint()` |


### Special Cases (No Endpoint Check)

These motions use different boundary handling:

- **f/F motions**: Same-line only, check if they don't go into prefix/suffix. Explored via `generateFMotions()`.
- **Count searches**: `BufferIndex` covers word/bigWord/paragraph landings. Counted sentence motions replay `sentenceMotionEndpoint()` per count because Vim's sentence scan is stateful.

### Templated Motion Specs

Motion exploration uses templated functions with direction-split spec vectors for compile-time dispatch:

```cpp
// Word motions use semantic target tables.
exploreWordMovements(Movement::FORWARD_WORD_MOVEMENTS, base);  // w, W, e, E
exploreWordMovements(Movement::BACKWARD_WORD_MOVEMENTS, base); // b, B, ge, gE

// Paragraph/Sentence - templated on Forward
exploreParagraphMovements<true>(Movement::FORWARD_PARAGRAPH_MOVEMENTS, base);   // }
exploreParagraphMovements<false>(Movement::BACKWARD_PARAGRAPH_MOVEMENTS, base); // {

// Scroll - templated on Forward
exploreScrollMovements<true>(base);   // <C-d>
exploreScrollMovements<false>(base);  // <C-u>
```

See `MovementToSpec.cppm` for the spec definitions.


### A* vs Dijkstra Trade-off

Our distance heuristics are generally **inadmissible** (overestimates true cost), which makes A* loses its optimality guarantee, but for greater efficiency:
- First path found to a goal state may not be optimal
- One starting position can dominate exploration (its children have lower `f = g + h`)
- Other starting positions get "starved" — never popped from the priority queue

However, if we use pure effort, with no distance heuristic, then we can have a guaranteed correct Dijkstra exploration.

We expose distanceWeight and effortWeight in baseOptimzerParams to get a balance between these two. Transform's `distanceWeight` defaults to 0.35 instead of the shared 1; see `transform-optimizer.md`.

## Search Stats Policy

Search stats are split into two categories:

- Core counters: cheap aggregate values such as `nodesExplored`, `totalPops`, `resultsFound`, and `queueSizeAtStop`
- Trace payloads: heavier debug data such as `exploredStates`

Core counters are part of normal optimizer outputs and benchmark workflows, so they should stay cheap and always available. Trace payloads should be gated carefully because they allocate memory and often construct strings.

`SearchStats` now centralizes mutation so callers do not write arbitrary fields directly. This gives us a single place to control expensive collection paths and to add aggregate helpers used by benchmark/reporting workflows.

### Compile-Time Gating

`SearchStats::maybeRecordExploredState` compiles out its recording body when
`SEARCH_TRACE_STATS_ENABLED` is false. With tracking enabled, it records
states up to `MAX_TRACED_STATES` per search. Keep expensive payload
construction inside the gate because callers still evaluate function arguments.

### Workflow Guidance

- `vimfy_unit_tests` and `vimfy_benchmarks` can rely on core counters in
  any build.
- Configure with `-DVIMF_TRACK_STATES=ON` to collect trace payloads for
  `vimfy_debug` or build the `vimfy_explore` export tool. Tracking defaults
  to off and is independent of the general debug stream.

If we later add more expensive stats, they should follow the same rule: keep aggregate reporting cheap and move payload-style diagnostics behind an explicit gate.

## State Equivalence Keys and the targetCol Omission

`TransformStateKey` and `SuffixKey` deliberately omit `targetCol` (Vim's curswant) from their equality and hash. The invariant that makes this safe — and the four concrete trip wires that would invalidate it — are documented in the comment block above `TransformStateKey` in `src/Optimizer/TransformOptimizer/TransformState.cppm`. If you're adding a search arm, a planned-edit kind, or a cached suffix that emits curswant-preserving motion (j/k/gg/G/scroll), read that comment before assuming the keys are still sufficient. Adding `targetCol` to both keys (and their hashes) is the fix; the comment explains when you'll need to.

`CompositionStateKey` does include `targetCol`, because Nav lands Composition at clamped columns. How Transform results are applied from such a cursor is covered under Composition Logic in `composition-optimizer.md`.
