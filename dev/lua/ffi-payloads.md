# FFI payloads

Code of record: `src/LuaExports/Api.def`, `src/LuaExports/proto/`,
`src/LuaExports/Common.cppm`, `src/LuaExports/Envelope.cpp`,
`src/LuaExports/Msgpack.cppm`, `lua/vimficiency/ffi/core.lua`.

**Rule.** Lua to C++ is typed C structs. C++ to Lua is protobuf for the
`vf_explore_*` exports and msgpack for the rest, which are moving to protobuf
(`dev/architecture/todo.md` item 11). Neither side has a hand-written parser,
and none should be added. The rationale is in `dev/decisions.md`.

## Lua to C++: typed inputs

Scalars are direct cdef parameters. Byte strings are a pointer plus a length, or
a `VFByteSlice`. Lists are a pointer to an array of structs plus a count.

```lua
local slices, count = core.byte_slices(lines)
local slice = lib.vf_explore_accept_snapshot(view_id, slices, count, ...)
core.keep_alive(lines, slices)
return unwrap(slice, "ApplyResult")
```

**Lifetime.** cdata anchors nothing: not the Lua strings a slice points into,
and not a cdata array stored in a struct field. Keep the FFI call out of tail
position and pass the source tables and cdata arrays to `core.keep_alive` after
it.

`optimizer_overrides` is the one text input (`scope:key=value` lines, parsed in
the core). A search request carries no boundary columns; C++ derives the
whole-slice boundary (`sliceBoundary`).

## C++ to Lua: one envelope

Every export returning `VFByteSlice` returns an envelope holding exactly one of:

| Case | Meaning |
|---|---|
| `value` | Success; the payload |
| `error` | An `Expected` error or an escaped exception |
| `pending` | Async job still running |

| | Protobuf | Msgpack |
|---|---|---|
| Envelope | `vf.Envelope` (`proto/common.proto`); `value` is the serialized message | one-key map; `value` is the payload itself |
| C++ wrapper | `guardedProto`, `storeProtoPending` | `guarded`, `storePending` |
| Lua decoder | `core.unwrap_proto(slice, "vf.explore.State")` | `core.unwrap(slice)`, `core.decode(slice)` |

Both `unwrap`s raise the error and return the value, or nil while pending.
`core.decode` returns `value, err` instead of raising.

**Exceptions.** Unwinding into LuaJIT is undefined behaviour, so every export
body runs inside `guarded` / `guardedProto` (exception becomes an error
envelope) or `noUnwind` (no error channel; terminates). `AsyncJob` catches a
worker's exception and the `*_poll` export returns it.

## Protobuf payloads

The schema in `src/LuaExports/proto/` is the contract. `protoc` generates the
C++ message classes, and the compiled descriptor set is embedded in the library
(`vf_proto_descriptor`).

Lua decodes with lua-protobuf, whose `pb.c` is compiled into the same library
and opened by `ffi/core.lua` with `package.loadlib`. The decoder and the schema
therefore always match the encoder, including across a stale rebuild.

Writing one: add the message to the matching `.proto`, fill it in the export,
and return `proto(message)`. `stateMessage` in `InteractiveExports.cpp` is the
pattern.

- Field names are the snake_case names Lua reads.
- Text is `bytes`, not `string`: buffer content and key sequences need not be
  UTF-8.
- Enums decode as their value name, so name the values as Lua compares them
  (`Phase.Kind`).
- A `oneof` decodes as the set member plus the oneof name holding that member's
  name: `{outcome = "rejected", rejected = {reason = ...}}`.
- Scalars decode to their default when unset and empty repeated fields to `{}`
  (`decode_default_array`). An unset message field is nil.
- The LuaLS classes in `types.lua` are still hand-written. A field renamed in
  the schema shows up as a nil at the use site, so cover the shape with a Lua
  test through the real library (`tests/lua/explore/`).

## Msgpack payloads

Emit the exact shape of the LuaLS class the wrapper returns, with the same
snake_case field names.

- `map(n)` and `array(n)` must be followed by exactly `n` entries; a wrong
  count corrupts everything after it.
- Never write nil: `vim.mpack.decode` turns it into `vim.NIL`, a truthy
  userdata. Omit the key instead.
- Output shapes are not covered by the ABI hash. Cover the shape with a Lua
  test through the real library (`tests/lua/ffi_analyze.lua`).

## Build

- `protoc` is the official prebuilt binary, pinned by SHA-256 per host in
  `CMakeLists.txt`. Only the lite runtime and Abseil build from source.
- Generated code is locked to the exact runtime version, so the `protoc`
  release and the runtime tag move together.
- Abseil is declared once at the root under FuzzTest's name and tag; protobuf
  and FuzzTest would otherwise each define its targets.
- `pb.c` builds against fetched LuaJIT headers and resolves the Lua C API from
  the host Neovim at load (`-undefined dynamic_lookup` on macOS).
- Generated headers include standard headers, so they are included only from
  `.cpp` files, ahead of `import std.compat`, never from a module interface.
