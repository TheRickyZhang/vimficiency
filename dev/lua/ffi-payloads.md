# FFI payloads

Code of record: `src/LuaExports/Api.def`, `src/LuaExports/proto/`,
`src/LuaExports/Common.cppm`, `src/LuaExports/Envelope.cpp`,
`lua/vimficiency/ffi/core.lua`.

**Rule.** Data crosses the FFI as protobuf in both directions. The schema in
`src/LuaExports/proto/` is the contract; neither side has a hand-written
parser or a struct of its own, and none should be added. The rationale is in
`dev/decisions.md`.

## The C ABI

`Api.def` declares one struct, `VFByteSlice`, and three export shapes:

| Shape | Used for |
|---|---|
| `VFByteSlice vf_x(const char* request, size_t request_len)` | Anything taking data |
| `VFByteSlice vf_x(int id)` / `VFByteSlice vf_x()` | A job or view id alone, or no input |
| `int vf_x(int id)`, `void vf_x()` | Cancel, destroy, reset: no result to report |

`vf_version_string`, `vf_abi_hash` and `vf_proto_descriptor` are the bootstrap
exceptions: Lua needs them before it can decode anything.

The comment beside each export names its request and result messages. The
`Api.def` hash still guards the ABI (`vf_abi_hash`).

## Lua to C++: requests

```lua
return core.call(lib.vf_build_search_request, "vf.search.BuildSearchRequest",
  vim.tbl_extend("error", span, {
    padding = padding,
    max_search_lines = max_search_lines,
  }), "vf.search.SearchRequest")
```

`core.call` encodes the table, calls the export, and unwraps the result. The
request bytes are a Lua string passed as a direct argument, which LuaJIT keeps
alive for the call, so there is no lifetime rule to follow.

**Strictness.** `pb.encode` raises on a value of the wrong type but silently
drops a key the message does not have. `core.encode` therefore walks the table
against the schema first and raises `unknown field 'search.window_hieght'`.
Always encode through `core.encode` or `core.call`.

An unset field arrives as its default. Use `optional` in the schema when C++
must tell unset from zero (`vf.config.Config`, `last_key_time_ns`).

C++ parses with `guardedRequest<Message>(storage, request, request_len, body)`,
which hands `body` the parsed message and turns a parse failure into an error.

`optimizer_overrides` is the one text field (`scope:key=value` lines, parsed
in the core).

## C++ to Lua: one envelope

Every `VFByteSlice` return is a serialized `vf.Envelope` holding exactly one
of:

| Case | Meaning |
|---|---|
| `value` | Success; the serialized result message |
| `error` | An `Expected` error (a bad request field, an id that is not live) or an escaped exception |
| `pending` | Async job still running |

`core.unwrap(slice, message)` raises the error and returns the decoded
message, or nil while pending. `core.decode` returns `value, err` instead of
raising.

**Exceptions.** Unwinding into LuaJIT is undefined behaviour, so every export
body runs inside `guarded` / `guardedRequest` / `guardedPoll` (exception
becomes an error envelope) or `noUnwind` (no error channel; terminates).
`AsyncJob` catches a worker's exception and the `*_poll` export returns it.

**Bad input.** Anything Lua passes fails as an `unexpectedError`, never a
`CHECK`: a request field, or a job or view id that is not live. `CHECK` is for
states C++ itself guarantees.

## Writing a message

Add it to the matching `.proto`, fill it in the export, and return
`proto(message)`. `stateMessage` in `InteractiveExports.cpp` is the pattern.

- Field names are the snake_case names Lua reads and writes.
- Text is `bytes`, not `string`: buffer content and key sequences need not be
  UTF-8. `string` is for identifiers.
- Enums cross as their value name, so name the values as Lua compares them
  (`Phase.Kind`). Encoding an unknown name raises.
- A `oneof` decodes as the set member plus the oneof name holding that member's
  name: `{outcome = "rejected", rejected = {reason = ...}}`.
- Scalars decode to their default when unset and empty repeated fields to `{}`
  (`decode_default_array`). An unset message field is nil.
- The LuaLS classes in `types.lua` are still hand-written. A field renamed in
  the schema shows up as a nil at the use site, so cover the shape with a Lua
  test through the real library (`tests/lua/ffi_analyze.lua`,
  `tests/lua/explore/`).

## How Lua gets the schema

`protoc` compiles the schema to a descriptor set that is embedded in the
library (`vf_proto_descriptor`). lua-protobuf's `pb.c` is compiled into the
same library and opened by `ffi/core.lua` with `package.loadlib`.

The Lua codec and the schema therefore always match the C++ on the other
side, including across a stale rebuild.

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
  Helpers in `Common.cppm` that touch a message are templates for that reason.
