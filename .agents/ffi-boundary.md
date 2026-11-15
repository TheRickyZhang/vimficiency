# ffi-boundary

Use when adding or modifying any code that crosses the Lua/C++ FFI boundary,
any file under `src/lua/` or the Lua plugin tree, or any C++ symbol exported
for FFI. Also use when deciding whether a new piece of logic belongs in Lua
or in C++.

The Lua side of Vimficiency is intentionally thin. It exists to do the things
only Lua can do; nothing else.

## The split — non-negotiable

Lua is allowed to own ONLY:

- Calls into Neovim's native runtime API (`vim.api.*`, `vim.fn.*`,
  `vim.notify`, autocmds, keymaps, `on_key`, etc.).
- Local persistent storage that requires Neovim's filesystem / `stdpath`
  conventions (session storage, on-disk caches scoped to the editor).
- The minimal mechanics needed to organize the above to and from C++ over FFI.

Everything else lives in C++.

If you find yourself reaching for nontrivial logic in Lua, stop. Either:
1. Move it to C++ and expose a single FFI entry point, or
2. Justify it explicitly

## Naming — same identifier, snake_case, every side

**The rule:** the same identifier appears in the `.proto` schema, in
`Api.def`, and in the Lua wrappers, in snake_case. No case conversion, no
translation.

- **Functions** are prefixed `vf_` in `Api.def` (always, on every export);
  the Lua wrapper strips it. `vf_explore_destroy` ↔ `M.explore_destroy`.
- **Messages** live in `src/LuaExports/proto/` under a `vf.<area>` package.
  Lua tables use the message's field names unchanged.
- **Enum values** that Lua reads by name keep the casing Lua compares
  against (`Phase.Kind`, `Token.Kind`).

C++ code behind the boundary keeps its normal camelCase.


## Encoding rules

- Data crosses as protobuf both ways: one serialized request message in, a
  `vf.Envelope` around the result message out. Never add a struct to
  `Api.def`, a text format, or a hand-written parser. See
  `dev/lua/ffi-payloads.md`.
- Job and view ids alone cross as plain `int` parameters.
- Encode requests only through `ffi/core.encode` (or `core.call`): it rejects
  a field the message does not have, which `pb.encode` would silently drop.
- Every export body runs inside `guarded` / `guardedRequest` (returns
  `VFByteSlice`) or `noUnwind` (no error channel); an exception must never
  unwind into LuaJIT.
- Buffer content and key sequences are `bytes` fields; do not assume UTF-8 or
  null-termination on the C++ side.
- Never send a number as text: `tostring(n)` on hrtime-scale numbers emits
  scientific notation (`1.7e+18`). Use an `int64` field.

## No dead fallbacks

- Do **not** wrap FFI calls in `pcall` to "be safe." If the C++ side errored,
  surface the real error — silent fallbacks hide bugs that only show up
  later as wrong answers.
- Do not branch on optional fields that the API contract guarantees are
  present. Trust the contract; fix it if it is wrong.
- This applies in both directions: C++ should not silently accept malformed
  input from Lua either.

## React to Neovim, do not predict it

The Lua side observes Neovim state via events and queries — it does not
parse the user's keystrokes to anticipate what Neovim will do.
