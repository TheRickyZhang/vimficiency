# Script usage

## Dependency graph

See internal dependency graph:

Prerequisites:
```bash
sudo pacman -S graphviz
luarocks install luafilesystem
```

```bash
lua scripts/dep-graph.lua > deps.dot
dot -Tsvg deps.dot -o deps.svg
```

## Generating `:help vimficiency` (vimdoc)

Edit the chapter Markdown under `doc-src/`. `build-vimdoc-source.sh` concatenates filenames
matching `[0-9][0-9]-*.md` into `build/vimficiency.md`, strips frontmatter, flattens relative
links, and preserves external URLs. Letter-suffixed chapters such as `07a-session-storage.md`
fall outside that glob. Panvimdoc renders `doc/vimficiency.txt`. The postprocessor adds the
`*vimficiency*` alias, normalizes chapter tags, and checks that tag references resolve.

On a pull request, CI checks that the vimdoc and the docs site build. On a qualifying push to
`main`, the vimdoc workflow opens a PR with the regenerated help. Both workflows pin the same
panvimdoc version and options. [PR checks](../.github/workflows/vimdoc-check.yml) ·
[Generation workflow](../.github/workflows/vimdoc.yml)

### Running it locally

Install Pandoc and clone the panvimdoc version pinned by CI. Run the wrapper from the repo root.

```bash
git clone --depth 1 --branch v4.0.1 https://github.com/kdheepak/panvimdoc.git /tmp/panvimdoc
./scripts/build-vimdoc.sh
```

Clone once. Set `PANVIMDOC_DIR` to use an existing checkout elsewhere. When changing the
conversion options, update the local wrapper and both workflows together. Keep one help file
while chapter tags make navigation practical. Revisit splitting if a topic needs a standalone
help page or the file grows past roughly 2,000 lines and becomes difficult to navigate.
