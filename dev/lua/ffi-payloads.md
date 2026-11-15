# FFI payloads

Code of record: `src/LuaExports/Api.def` (input structs), `src/LuaExports/Msgpack.cppm`
(`MsgpackWriter`, `Packed`), `src/LuaExports/Common.cppm` (`storeBytes`,
`storePending`, `guarded`, `noUnwind`), `lua/vimficiency/ffi/core.lua` (`unwrap`,
`decode`, `byte_slices`, `set_slice`, `keep_alive`).

**Rule.** Lua to C++ is typed C structs. C++ to Lua is msgpack. Neither side
has a hand-written parser, and none should be added.

The split is asymmetric because each direction has one half that is free. LuaJIT
builds C structs natively and C++ reads them natively; Neovim ships a msgpack
decoder and writing msgpack is append-only.

## Lua to C++: typed inputs

Scalars are direct cdef parameters. Byte strings are a pointer plus a length, or
a `VFByteSlice`. Lists are a pointer to an array of structs plus a count.

Declare the struct in `Api.def`, fill it in the Lua wrapper, and read the fields
in C++. The ABI hash covers the layout, so a stale library fails at load.

```lua
local slices, count = core.byte_slices(lines)
local slice = lib.vf_explore_accept_snapshot(view_id, slices, count, ...)
core.keep_alive(lines, slices)
return core.unwrap(slice)
```

**Lifetime.** cdata holds raw pointers and anchors nothing: not the Lua strings
a slice points into, and not a cdata array stored in a struct field. A local
that is dead after its last use, or a frame dropped by a tail call, can be
collected while C++ still reads it.

Build the cdata in the wrapper that makes the call, keep the FFI call out of
tail position, and pass the source tables and cdata arrays to `core.keep_alive`
after it. C++ copies whatever it keeps (`linesFromSlices`,
`decodeSearchRequest`).

A search request carries no boundary columns. The slice is the whole edit
region, so C++ derives the boundary from the lines (`sliceBoundary`).

`optimizer_overrides` is the one text input: `scope:key=value` lines parsed by
`OptimizerParamOverrides::parse` in the core. Its keys are identifiers and its
values are numbers, so it cannot carry arbitrary bytes.

## C++ to Lua: one envelope

Every export returning `VFByteSlice` returns a msgpack map with exactly one key:

| Map | Meaning | Written by |
|---|---|---|
| `{value = <payload>}` | Success | `storeBytes` |
| `{error = "<message>"}` | Failure | `storeBytes` on an `Expected` error |
| `{pending = true}` | Async job still running | `storePending` |

`core.unwrap(slice)` raises the error and returns the value, or nil while
pending. `core.decode(slice)` returns `value, err` for the callers that report
an error instead of raising (the tokenizers).

**Exceptions.** An exception unwinding into LuaJIT's C frames is undefined
behaviour, so every export body runs inside `guarded` or `noUnwind`
(`Common.cppm`). `guarded` turns an escaping exception into an `{error}`
envelope; `noUnwind` is for exports with no error channel and terminates.

A worker thread's exception is caught in `AsyncJob`, stored as `error()`, and
returned by the `*_poll` export as an `{error}` envelope.

`storeBytes` takes `Expected<Packed>`, not a string. `Packed` only comes out of
`MsgpackWriter::take()` or the `pack*` helpers, so raw text cannot be returned
unpacked by accident.

## Writing a payload

Emit the exact shape of the LuaLS class the wrapper returns, with the same
snake_case field names, so the wrapper is `return core.unwrap(...)` and nothing
else. `packState` in `InteractiveExports.cpp` and `VF.Explore.State` in
`types.lua` are the pattern.

- `map(n)` and `array(n)` are count-prefixed: write exactly `n` entries after
  them. A wrong count corrupts everything that follows.
- Never write nil. `vim.mpack.decode` turns it into `vim.NIL`, a truthy
  userdata. Omit the key (as `packOutcome` omits `reason` on `Applied`).
- Strings use `str` for all bytes, including sequences with raw control bytes.
  `vim.mpack.decode` returns them unchanged and does not validate UTF-8.
- `integer` for ints, `number` for doubles. Costs cross at full precision; any
  rounding is a display concern.

## Adding or changing a payload

1. Change the C++ writer, the LuaLS class, and the wrapper in the same commit.
2. For an input, change `Api.def`; the hash check then enforces a rebuild.
3. Output shapes are not hash-covered. A renamed field shows up as a nil at
   the use site, so cover the shape with a Lua test through the real library
   (`tests/lua/ffi_analyze.lua` is the pattern).
4. Writer changes are covered by `tests/Unit/Misc/MsgpackWriterTest.cpp`, which
   decodes with msgpack-c.
