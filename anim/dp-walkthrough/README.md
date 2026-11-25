# DP walkthrough animation

`aa b cc` → `xx b zz` shows how the planner fills the normal and insert
tables. At (5, 5), a move and an insert exit tie on cost. The exit wins on the
fewer-regions tie-break. Following the final backpointers yields one change
with score 11. Keeping the middle and editing both ends separately costs 14,
including the two region charges.

In the larger `main()` example a 62-character sealed span separates the two
blocks. The inset shows their cell format at the first edit. The planner hands
four regions to search, with a total estimated cost of 56.6.

## Regenerate the traces

From the repo root, run the production trace exporter with the uniform keyboard
and project its output to the fields used on screen. The winner and its
backpointers come from `VimDiff::trace()`. The two-edit comparison uses the
exported span prices, typed-key costs, exit cost and region charge.

```bash
cmake --build build --target vimfy_debug
mkdir -p anim/.cache
VIMFY_TRACE_OUT=anim/.cache/planner-small.json \
  ./build/tests/vimfy_debug --gtest_filter=VimDiffTraceExport.ExportExample
jq -L anim/dp-walkthrough 'include "walkthrough"; small' \
  anim/.cache/planner-small.json > anim/dp-walkthrough/trace.json

VIMFY_TRACE_INITIAL="$(jq -r .example.initial anim/dp-walkthrough/trace2.json)" \
VIMFY_TRACE_GOAL="$(jq -r .example.goal anim/dp-walkthrough/trace2.json)" \
VIMFY_TRACE_OUT=anim/.cache/planner-large.json \
  ./build/tests/vimfy_debug --gtest_filter=VimDiffTraceExport.ExportExample
jq -L anim/dp-walkthrough 'include "walkthrough"; large' \
  anim/.cache/planner-large.json > anim/dp-walkthrough/trace2.json
```

## Render

Both scenes use Manim Community and `anim/manim.cfg`. Set up the shared
environment once from `anim/`:

```bash
uv venv .venv --python 3.12
uv pip install --python .venv/bin/python manim
```

Run `anim/dp-walkthrough/render.sh` from the repo root to render the 1080p60
master and encode `web/docs/public/planner-walkthrough.mp4` for the docs player.
To update the README player, attach that MP4 to GitHub and replace the planner
attachment URL under **How it works**. Keep the URL on its own line.
