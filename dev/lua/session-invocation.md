# Session kinds and output

A session is `(start_kind, end_kind)`, each `manual | auto`:

| Kind    | Pair             | Started by                      | Ended by                        |
|---------|------------------|---------------------------------|---------------------------------|
| Mark    | (manual, manual) | `:Vimfy start <alias>`          | `:Vimfy finish <alias>`         |
| Watch   | (manual, auto)   | `:Vimfy watch <alias>`          | idle timer (`config.watch`)     |
| Recall  | (auto, manual)   | every atom, rolling ring        | `:Vimfy recall <N\|Ns>`         |
| Suggest | (auto, auto)     | a Recall record                 | an `auto_suggest` trigger fires |

Manual aliases are alphabetic; at most `MANUAL_CAPACITY = 5` are active at once,
and finished records keep their slot without counting against it. The recall
ring holds `config.KEY_SESSION_CAPACITY` records for `config.MAX_RETENTION_SECONDS`,
in RAM only. `N` means N commands ago, `Ns` means N seconds ago.

Active manual sessions are evicted when their window closes, the cursor drifts
past `config.MAX_SEARCH_LINES`, or they idle too long (`compute.manual_should_evict`).

## Output

`:Vimfy finish` notifies the full result: positions, the user's tokenized
sequence with its effort, and the ranked optimal sequences.

```
vimficiency finished [a] (0,0) -> (2,2)
  user: cw xxx <Esc> jj ci yyy <Esc> (15.00)
  1. 3rx <C-d> ciw yyy <Esc> (8.00)
```

An auto-suggest regression shows a terse, replaceable toast instead
(`auto_suggest.lua` `render_suggestion`); `$` resolves to that regression.

```
vimfy regression · 17 keys · 3s ago · 15 → 8
:Vimfy view $  ·  play $  ·  explore $
```

Sequences are tokenized by the C++ `SequenceParser`; `parseSequence` is
fallible and the FFI tokenizer exports return `ERROR: ...` strings so Lua can
tell a failure from an empty result. User cost is `getEffort(keyseq, config)`.
Finished results then live in the workspace or the archive; see
`session-storage.md`.
