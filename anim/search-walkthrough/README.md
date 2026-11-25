# Search walkthrough animation

Companion to [dp-walkthrough](../dp-walkthrough/README.md), picking up at its
closing line about realizing a plan with Vim commands. The recorded search uses
an earlier five-region partition of the same `main()` example. The current
planner selects four regions.

- Act 1: the five planned regions on the buffer.
- Act 2: transform pre-solve for region 2 (`10` → `n`) — deletions explored
  with their costs, delete→change conversion, and the resulting per-start menu
  (`cen⎋` · 4 vs `Xciwn⎋` · 6).
- Act 3: the composition A* over (cursor, regions done) states — live buffer,
  frontier queue ordered by f, per-pop candidates, and the three
  dominated-branch moments, each with the total cost that branch would have
  completed to.
- Act 4: zoom into the inner NavOptimizer call for the 6-line hop — the
  47-pop motion frontier with a reached-state cost sample, then the
  counted-motion rule that lands `0jE5j` exactly on the `+`.
- Act 5: the ranked finale — the 45-key winner plus the three pruned branches
  completed to full sequences (46, 46, 48 keys), differing segments
  highlighted, and search stats.

## Data source

`trace.json` is exported by `tests/Debug/CompositionTraceExport.cpp`. It runs
the production `CompositionOptimizer`, then rebuilds the search tree from the
recorded pop order by re-deriving every pop's transitions through the same
public helpers the real loop uses (transform-result buckets, insertion
strategies, inner NavOptimizer calls — whose own pop traces are captured).
Transform pops are replayed through the interpreter for buffer snapshots.
The exporter checks the reconstructed pops and results against the optimizer
used for that recording.

The finale's runner-up candidates come from the dominated branches: a dropped
child shares its state key with a node on the winning path, so the winner's
remaining transitions complete it verbatim. Each completed sequence (winner
included) is replay-verified through the interpreter to reach the goal buffer
and cursor.

The scene uses the transform zoom for edit 1 and the inter-edit navigation call
from node 4. To reproduce the checked-in snapshot from the original recording,
run these commands from the repo root:

```bash
git show e423e1b5:anim/search-walkthrough/trace.json | jq -S --indent 1 \
  '. + {navZoom: (.navCalls[] | select(.fromNode == 4 and .purpose == "inter-edit")),
        transformZoom: (.transformZooms[] | select(.editIdx == 1))}
   | del(.navCalls, .transformZooms, .nodes[].children[].navCall)' > anim/search-walkthrough/trace.json
```

## Render

Shared venv and `manim.cfg` under `anim/` (see the dp-walkthrough README for
one-time setup); render from `anim/`:

```bash
cd anim
.venv/bin/manim render -qh search-walkthrough/scene.py SearchWalkthrough
```

Output lands at `anim/out/1080p60/SearchWalkthrough.mp4`. For a README embed,
transcode a smaller upload copy and drag-drop it into the GitHub web editor:

```bash
ffmpeg -i out/1080p60/SearchWalkthrough.mp4 -c:v libx264 -preset slow -crf 28 \
  -pix_fmt yuv420p -movflags +faststart out/1080p60/SearchWalkthrough-upload.mp4
```
