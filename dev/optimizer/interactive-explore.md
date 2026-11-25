# Interactive Explore

Explore opens a finished session result in a scratch tab. Its starting text and cursor come
from the recording, even if the user has since edited the original buffer. The goal is the
recorded final text and cursor. Closing the tab discards the scratch edits.
[Lua entry point](../../lua/vimficiency/explore/init.lua)

## Buffer model

Lua owns the scratch buffer, its windows, input handlers, and a registry keyed by tab. It
copies the source's editing and display options and disables native scratch undo. C++ owns the
accepted buffer, cursor, key sequence, cost, phase, and history.
[Scratch layout](../../lua/vimficiency/explore/runtime/layout.lua) ·
[Copied options](../../lua/vimficiency/ui/buf_window.lua)

### Async session open

Lua opens the tab while `vf_explore_start_async` computes a traced composition plan on a
worker. A timer polls `vf_explore_poll`. Once the plan is ready, Lua receives the view ID,
attaches input handlers, and displays recommendations. Closing a primary pane or failing to
activate the view tears it down.
[View lifecycle](../../lua/vimficiency/explore/runtime/init.lua)

`compute_deadline_ms` bounds search after setup. Zero uses the natural search caps.
Composition and its nested Nav searches consult `SearchControl` while popping states and can
return the best result found so far. Cancellation is also checked between setup stages.
`vf_explore_cancel` joins the worker, so an in-flight planning or precomputation stage can
delay teardown.

## Planned edits

`CompositionResult::plannedEditAt(i)` gives Explore the diff, Transform result, and buffer
fenceposts before and after edit `i`. The Transform result associates executable starts with
possible post-edit landings. A navigation target can cover positions where no edit is
executable.

Deviating from a suggested movement keeps the same planned edit. Changing planner settings
calls `View::reconfigurePlan`. It resets progress and history if the resulting composition
plan differs. The traced plan also carries byte spans so the header can align explored and
optimal sequences by planned edit.
[Composition result](../../src/Optimizer/CompositionOptimizer/CompositionOptimizer.cppm)

## Phase machine

- `Navigate(i)`. Suggest movements toward executable starts and any Composition action valid
  here. A movement onto an executable start enters `Transform(i)`. Moving away stays in
  Navigate.
- `Transform(i)`. Offer edits at the current cursor, including delete-first alternatives for
  replacements. After partial progress, compute the phase from the remaining edit and the
  actual landing. Moving off an executable start returns to Navigate.
- `Insert(i)`. Suggest the remaining typed input, accounting for text already present after
  mode entry or autoindent. On exit, matching the post-edit fencepost and reporting a valid
  cursor advances to edit `i + 1`. Abandoning the insert restores the preceding state.
- `Navigate(totalEdits)`. Move to the final cursor. A pure-motion session starts here.
  `View::isCompleted()` becomes true when the cursor reaches `goalPos`.

For an ordinary edit, `resultsAt(line, col)` determines executable starts. When the residual
diff is a pure insertion, the start is its insertion point. Advancing an edit installs its
post-fencepost, so reaching the final navigation phase already accounts for the text changes.
[Phase transitions](../../src/Explore/Explore.cpp)

### Frontier contract

Each Normal-mode frontier suggestion is one executable action. After it runs, Explore derives
the next phase from the observed buffer, cursor, and mode.

- Nav ranks movements toward a landing, with `maxResultsPerEndPos` limiting candidates per
  position.
- Composition can offer an insertion command at a valid insertion column, a bracket or quote
  text object at its enumerated location, or a join that makes progress.
- Transform offers edit tokens such as `dw`, `cl`, and `rX`. A join that reaches the fencepost
  can complete the edit. A replacement can also expose its deletion first and offer the
  residual insertion on the next query.

Duplicate tokens within an emitter are an invariant failure checked in Release builds. Explore
merges suggestions from different start queries by token. Batch optimization can return a full
multi-token sequence, including visual-delete shortcuts.
[Frontier emitter](../../src/Optimizer/FrontierCommon.cppm)

## Snapshots, rejection, and history

`View::applyEdit` accepts eligible edits before Explore ranks and limits the menu. It can also
apply a complete Transform result from its original buffer and start position. Live input reaches `acceptSnapshot`
with the observed lines, cursor, mode, and normalized keys. Insert entry must be in scope. A
wrong insert exit rolls back, and Lua restores the scratch buffer from the backend. If the
live buffer, cursor, or mode still disagrees after recovery, Lua shows the invariant warning
in the panel. [Snapshot recovery](../../lua/vimficiency/explore/input/handlers.lua) ·
[Key normalization](../lua/key-normalization.md)

Undo and redo use accepted checkpoints. Entering Insert or beginning a partial replacement
saves the preceding checkpoint. Intermediate Insert and delete-prefix states stay out of
history. Thus deleting a character and inserting its replacement can undo back to the state
before the deletion. Accepting a new action clears redo. The internal `View::commit` records a
state transition within the scratch session.
