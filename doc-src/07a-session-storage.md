---
title: "Session storage"
---

# Session storage

A finished session has two places it can live:

- **Session memory** (the "workspace") — indexed by its alias. This is
  where a session lands the moment `:Vimfy finish`, `:Vimfy recall`, or an
  auto trigger finishes it. Cheap, immediate, ephemeral.
- **Disk** (the "archive") — a file under
  `stdpath('data')/vimficiency/saved/<filename>.json`. Durable across
  restarts. Indexed by filename.

The two are separate namespaces. The same text can name both an alias
and a saved file.

## Memory rotates; disk is durable

Session memory is small and churns on its own:

- **Mark** and **Watch** sessions drop on their own when their window closes,
  the cursor drifts away, or they idle too long. Reusing a name replaces the
  previous session. A `NAMED_CAPACITY = 100` guard refuses further starts if
  something runs away.
- **Recall** / **Suggest** entries age out of the rolling ring by the
  union of `KEY_SESSION_CAPACITY` and `MAX_RETENTION_SECONDS` — drop
  only when both say drop.

Anything on disk is durable: it stays until you `:Vimfy rm` it.

## Four verbs

```vim
:Vimfy save  <alias>    [<filename>]
:Vimfy store <alias>    [<filename>]
:Vimfy fetch <filename> [<alias>]
:Vimfy rm    <filename>
```

| Verb    | Moves             | Workspace after        | Disk after          |
|---------|-------------------|------------------------|---------------------|
| `save`  | workspace → disk  | unchanged              | written (overwrite) |
| `store` | workspace → disk  | alias removed          | written (overwrite) |
| `fetch` | disk → workspace  | alias added            | unchanged           |
| `rm`    | deletes disk file | unchanged              | removed             |

`save` is a **copy**; `store` is a **move**. Pick whichever matches
your intent — `store` when you're done with the alias in-session,
`save` when you want to keep working with it.

## The no-`as` rule

Omitting the second argument means "repeat the first":

- `:Vimfy save foo`    ≡ `:Vimfy save foo as foo`
- `:Vimfy store foo`   ≡ `:Vimfy store foo as foo`
- `:Vimfy fetch foo`   ≡ `:Vimfy fetch foo as foo`

Every name and recall alias (`a`, `3s`, `5`) is also a legal filename, so the
first argument is reusable. `@` and `$` are not filenames; for them the default
is the alias the session was finished under:

```vim
:Vimfy recall 3s
:Vimfy save @                " saves as 3s.json
:Vimfy save @ my-refactor    " or pick the filename yourself
```

A suggest result was not finished under an alias you typed, so `:Vimfy save $`
defaults to `suggest-<timestamp>` rather than overwriting the same `3s.json`
on every fire.

## Collision rules

- **`save` / `store` onto an existing disk file** → overwrites, emits a
  WARN. No refusal; filesystem-style "save over" semantics.
- **`fetch` into an occupied workspace alias** → refuses. Close the
  alias first (`:Vimfy close <alias>`) or fetch under a different alias.

## Replay: `play` covers both

`:Vimfy play <alias>|<filename>` looks the argument up in this order:

- **Workspace only** → replays.
- **Disk only** → implicitly `fetch <filename> as <filename>` into the
  workspace, then replays. After the command, the session is in your
  current workspace; the disk copy is untouched. This keeps the mental
  model "what I just replayed is in my current session."
- **Both** → WARN that the disk copy is being shadowed; replays the
  workspace copy. To force a refetch, `:Vimfy close <alias>` first, then
  `:Vimfy play <alias>`.

The implicit fetch only works when the filename is also a valid named
alias (letters only). For filenames with digits/dots/hyphens, use an
explicit `:Vimfy fetch <filename> <alias>` first.

## Worked example

```vim
:Vimfy start a               " mark a session start
" ... edit ...
:Vimfy finish a              " finishes session; workspace has alias `a`

:Vimfy save a                " disk now has saved/a.json; workspace still has `a`
:Vimfy store a fix           " workspace drops `a`; disk updated to saved/fix.json

" ... later, even after a restart ...
:Vimfy fetch fix a           " workspace has alias `a` again, populated from saved/fix.json
:Vimfy play a                 " replay as usual
```

## Listing

```vim
:Vimfy list
```

Shows both sides: active sessions in memory and saved files on disk.
Tab completion for `sim` / `view` / `rm` / `fetch` uses whichever side
is relevant for that command.
