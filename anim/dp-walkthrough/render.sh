#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

.venv/bin/manim render -qh dp-walkthrough/scene.py DPWalkthrough
master=out/1080p60/DPWalkthrough.mp4
work=$(mktemp -d .cache/walkthrough.XXXXXXXX)
trap 'rm -r -- "$work"' EXIT

ffmpeg -v error -i "$master" -c:v libx264 -threads 8 -preset slow -crf 26 \
  -pix_fmt yuv420p -movflags +faststart "$work/planner.mp4"
mv -- "$work/planner.mp4" ../web/docs/public/planner-walkthrough.mp4
