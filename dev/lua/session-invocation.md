# Session kinds and output

A session has one `kind` (`VF.Session.Kind` in `session/store.lua`), set when
the record is created and changed only by auto-suggest's promotion at finish:

| Kind        | Started by                      | Ended by                        |
|-------------|---------------------------------|---------------------------------|
| `mark`      | `:Vimfy start <alias>`          | `:Vimfy finish <alias>`         |
| `watch`     | `:Vimfy watch <alias>`          | idle timer (`config.watch`)     |
| `recall`    | every atom, rolling ring        | `:Vimfy recall <N\|Ns>`         |
| `suggest_*` | a `recall` record               | the `auto_suggest` trigger that fired (`idle`, `keys`, `cost`) |

The kind is decided where the trigger lives, never in `subcommands.lua`:
`session.start`/`watch` create `mark`/`watch`, the ring creates `recall`, and
`auto_suggest` passes `suggest_<trigger>` to `store.convert_to_finished`, which is
the only path that may change a kind (recall → suggest_* only). `finish` and
`recall` take no kind argument. `store.session_type(kind)` folds the three
suggest triggers into `suggest` for pickers and stats.

## Handles

Every user-typed handle is an **alias** or a **filename**; there is no third
word. An alias is anything `store.lua` resolves to a session id, in one place
(`convert_alias_to_id`), by kind (`alias.parse`):

| Alias kind      | Form   | Resolves to                                  |
|-----------------|--------|----------------------------------------------|
| `named`         | `a`    | `named_to_id`                                |
| `recall_key`    | `5`    | ring entry N commands ago                    |
| `recall_time`   | `3s`   | youngest ring entry at least N seconds old   |
| `last_finished` | `@`    | `last_finished_id`                           |
| `last_suggest`  | `$`    | `last_suggest_id` (set only on a toast)      |

A filename names a result on disk, a pin or a saved session
(`alias.is_valid_filename`, `^[%w_][%w._-]*$`). Each command declares the alias
kinds it accepts with `alias.expect(alias, kinds)`.

`view`/`explore`/`pin` take an alias or a filename and try the store,
then the pins, then the saved sessions (`archive.resolve_result`). Parameters
are named `alias`, `filename`, or `alias_or_filename` after the grammar they
accept.

Named aliases are alphabetic (like named registers). Reusing one replaces the
previous record; `NAMED_CAPACITY = 100` is only a runaway guard (eviction is the
real bound),
and finished records keep their aliases without counting against it. The in-memory
Recall ring drops its oldest entry only when its count exceeds
`config.KEY_SESSION_CAPACITY` and that entry is older than
`config.MAX_RETENTION_SECONDS`.

Active mark/watch sessions are evicted when their window closes, the cursor drifts
past `config.MAX_SEARCH_LINES`, or they idle too long (`compute.manual_should_evict`).

## Output

`:Vimfy finish` notifies the full result: positions, the user's tokenized
sequence with its effort, and the ranked optimal sequences. The bracketed tag
after the position is the kind.

```
vimficiency finished [a] (0,0) -> (2,2) [mark]
  user: cw xxx <Esc> jj ci yyy <Esc> (15.00)
  1. 3rx <C-d> ciw yyy <Esc> (8.00)
```

An auto-suggest regression shows a terse, replaceable toast instead
(`auto_suggest.lua` `render_suggestion`); `$` resolves to that regression.

```
vimfy regression · 17 keys · 3s ago · 15 → 8
:Vimfy view $  ·  explore $
```

Sequences are tokenized by the C++ `SequenceParser`. On failure the tokenizer
export returns a protobuf error envelope, and the Lua wrapper returns an empty
token list plus an error string. User cost is `getEffort(keyseq, config)`.
A finished result is then active in memory, saved to disk and optionally
pinned; see `session-storage.md`.
