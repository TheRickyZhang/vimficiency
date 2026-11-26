---
title: "Session storage"
---

# Session storage

A finished session's result has three lifetimes:

| Lifetime | Where | Named by | Ends when |
|----------|-------|----------|-----------|
| **Active** | memory, in this Neovim | its alias: `a`, `@`, `$`, `5`, `3s` | it is closed or rotates out, or Neovim exits |
| **Saved** | `sessions/<filename>.json`, written as it finishes | a filename made for it | `:Vimfy rm <filename>` |
| **Pinned** | `pinned/<filename>.json`, a copy | the saved filename, or one you give | `:Vimfy unpin <filename>` |

Both directories are under `stdpath('data')/vimficiency/`.

## Every finished session is saved

When `:Vimfy finish`, `:Vimfy recall` or an auto trigger finishes a session,
its result is written to `sessions/` under a filename of its own:

```
<alias>-<YYYYmmdd>-<HHMMSS>        a-20261126-031401
```

- A Mark or Watch session uses its alias. A Recall or Suggest session uses
  `recall` or `suggest`, because its alias (`5`, `3s`) is a position that
  moves.
- The time is when the session finished, in local time. Two sessions that
  would share a filename get `-2`, `-3`, and so on.
- Nothing is removed on its own. There is one file per finished session,
  including auto-suggest runs that never showed a hint.

A saved result holds the lines the session ran over, so `sessions/` contains
text from the files you edited. Turn saving off with:

```lua
require('vimficiency').setup({ sessions = { save = false } })
```

## Pins

A pin is a copy you keep on purpose.

```vim
:Vimfy pin   <alias>|<filename> [<filename>]
:Vimfy unpin <filename>
:Vimfy rm    <filename>
```

| Command | Effect |
|---------|--------|
| `pin a` | copies the result to `pinned/a-20261126-031401.json` |
| `pin a important` | copies it to `pinned/important.json` |
| `pin a-20261126-031401` | pins a saved session from an earlier Neovim |
| `unpin important` | deletes `pinned/important.json` only |
| `rm a-20261126-031401` | deletes `sessions/a-20261126-031401.json` only |

- Pinning onto an existing pin replaces it, with a warning.
- A session that is still capturing cannot be pinned. Finish it first.
- With saving off, a session has no filename of its own, so `pin` needs one:
  `:Vimfy pin a <filename>`.
- `rm` leaves any pin of that session in place. Its row in `:Vimfy stats`
  keeps its numbers and says the text was removed.

## What an alias or filename resolves to

`view`, `explore` and `pin` take an alias or a filename, and look it up in
this order:

1. an alias in this Neovim's memory
2. a pin
3. a saved session

The same text can be an alias and a pin. The alias wins while it is live; the
pin stays reachable from `:Vimfy list`.

## Memory rotates

Aliases are short-lived:

- **Mark** and **Watch** sessions drop on their own when their window closes,
  the cursor drifts away, or they idle too long. Reusing a name replaces the
  previous session. A `NAMED_CAPACITY = 100` guard refuses further starts if
  something runs away.
- **Recall** / **Suggest** entries age out of the rolling ring by the
  union of `KEY_SESSION_CAPACITY` and `MAX_RETENTION_SECONDS` — drop
  only when both say drop.
- `@` and `$` are unset after a restart.

Once an alias is gone, the session is still reachable by its saved filename.

## Worked example

```vim
:Vimfy start a
" ... edit ...
:Vimfy finish a              " saved as sessions/a-20261126-031401.json

:Vimfy pin a                 " pinned/a-20261126-031401.json
:Vimfy pin a important       " pinned/important.json

" ... later, even after a restart ...
:Vimfy view important
:Vimfy view a-20261126-031401
```

## Finding a session

- `:Vimfy list` shows the active sessions and the pins.
- The run lists in `:Vimfy stats` show every logged session with its saved
  filename. `<CR>` replays the current one and `p` pins it.
- Tab completion offers aliases and pins for `view` / `explore` / `pin`, pins
  for `unpin`, and saved filenames for `rm`.

## Coming from `save`, `store`, `fetch` and `play`

`save`, `store` and `fetch` are gone: every session is saved, and `view` reads
disk directly. `rm` used to delete a named save; that is `unpin` now.

`play` is now `view`, and the `play = { ... }` setup block is `view = { ... }`.
The older `view`, a static before/after window, is removed.

Files in the old `saved/` directory become pins. The directory is renamed to
`pinned/` the first time pins are used, and every old filename still opens.
