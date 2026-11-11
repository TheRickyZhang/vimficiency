# Key normalization (`capture/keynorm.lua`)

Any key bytes that reach the C++ tokenizer must first pass through
`keynorm.normalize`. Skipping it trips `assert(false && "Malformed key
sequence")` on the first Ctrl-letter.

## Contract

Input is raw bytes as Neovim delivers them: plain/UTF-8 text, single-byte
Ctrl codes (`\x15`), or K_SPECIAL form (`\x80kb`). Never printable `<...>`
notation: `keytrans` would re-escape the `<` and yield `<lt>C-u>`.

Output is `vim.fn.keytrans(raw)` with the Ctrl modifier letter lowercased,
because the token tables in `src/Keyboard/ToKeys/MovementToKeys.cpp` register
`<C-u>`, not `keytrans`'s `<C-U>`.

## No `nvim_replace_termcodes` pre-pass

It runs the opposite direction (printable → raw). Fed raw input, it re-escapes
the `0x80` K_SPECIAL byte, and `keytrans` then renders `<BS>` as `<80>kb`. An
earlier version had this pass "for flexibility" and it corrupted every special
key.

## Call sites

- `capture/atom_stream.lua` — CmdAtom `keys` and `lhs`.
- `explore/handlers.lua` — Explore's own `vim.on_key` stream, before
  `explore_accept_snapshot`.

Any new consumer that reaches `getEffort` or the tokenizer must normalize too.
