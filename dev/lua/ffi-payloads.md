# FFI payloads

Code of record: `src/LuaExports/Api.def`, `src/LuaExports/Msgpack.cppm`,
`src/LuaExports/Common.cppm`, `lua/vimficiency/ffi/core.lua`.

**Rule.** Lua to C++ is typed C structs. C++ to Lua is msgpack. Neither side
has a hand-written parser, and none should be added. The rationale is in
`dev/decisions.md`.

## Lua to C++: typed inputs

Scalars are direct cdef parameters. Byte strings are a pointer plus a length, or
a `VFByteSlice`. Lists are a pointer to an array of structs plus a count.

```lua
local slices, count = core.byte_slices(lines)
local slice = lib.vf_explore_accept_snapshot(view_id, slices, count, ...)
core.keep_alive(lines, slices)
return core.unwrap(slice)
```

**Lifetime.** cdata anchors nothing: not the Lua strings a slice points into,
and not a cdata array stored in a struct field. Keep the FFI call out of tail
position and pass the source tables and cdata arrays to `core.keep_alive` after
it.

`optimizer_overrides` is the one text input (`scope:key=value` lines, parsed in
the core). A search request carries no boundary columns; C++ derives the
whole-slice boundary (`sliceBoundary`).

## C++ to Lua: one envelope

Every export returning `VFByteSlice` returns a msgpack map with exactly one key:

| Map | Meaning |
|---|---|
| `{value = <payload>}` | Success |
| `{error = "<message>"}` | An `Expected` error or an escaped exception |
| `{pending = true}` | Async job still running |

`core.unwrap(slice)` raises the error and returns the value, or nil while
pending. `core.decode(slice)` returns `value, err` instead of raising.

**Exceptions.** Unwinding into LuaJIT is undefined behaviour, so every export
body runs inside `guarded` (exception becomes `{error}`) or `noUnwind` (no
error channel; terminates). `AsyncJob` catches a worker's exception and the
`*_poll` export returns it.

## Writing a payload

Emit the exact shape of the LuaLS class the wrapper returns, with the same
snake_case field names. `packState` in `InteractiveExports.cpp` and
`VF.Explore.State` in `types.lua` are the pattern.

- `map(n)` and `array(n)` must be followed by exactly `n` entries; a wrong
  count corrupts everything after it.
- Never write nil: `vim.mpack.decode` turns it into `vim.NIL`, a truthy
  userdata. Omit the key instead.
- Output shapes are not covered by the ABI hash. A renamed field shows up as a
  nil at the use site, so cover the shape with a Lua test through the real
  library (`tests/lua/ffi_analyze.lua`).
