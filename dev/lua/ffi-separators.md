# FFI payload framing

Code of record: `src/LuaExports/Common.cppm` (constants), `src/LuaExports/Payload.cppm`
(`payload::` encoders/decoders), `lua/vimficiency/ffi/core.lua` (Lua
mirrors, `encode_string_list`).

**Rule.** A field that can contain arbitrary bytes is length-prefixed. A field
that is guaranteed printable uses the shared control-character separators.
Never invent a per-site delimiter: captured sequences contain spaces, tabs,
newlines, `<Esc>`, and raw control bytes, and every "safe-looking" delimiter has
bitten us.

## Length-prefixed strings

`N:<N bytes>N:<N bytes>...` to EOF. For buffer lines and any user-provided text.
Lua `core.encode_string_list`; C++ `payload::decodeLengthPrefixedStrings`.

## Control-character separators

`\x1e` (record) and `\x1f` (field): `EVENT_RECORD_SEP` / `EVENT_FIELD_SEP` in
both `Common.cppm` and `ffi/core.lua`. Only for content that cannot contain
those bytes: `keytrans` output, mode strings, numbers. The encoder asserts the
invariant (`ffi/session.lua` `build_sequence`) because a violation desyncs the
C++ decoder silently.

## Analyze results

`AnalyzeExports.cpp` emits newline-terminated records so the optional
`----------------DEBUG----------------` trailer can be found by substring:

```
size: <N> user_cost: <X>
<seq bytes>\x1f<cost>        × N
diffs: <M>
<8 \x1f-separated ints>      × M
```

Both sections are count-framed, so a body line is never mistaken for a header.
The seq/cost separator was once a literal space, which corrupted any sequence
with insert-mode text; that is the canonical failure this rule exists for.

## Numbers

No numeric field crosses as text today; int64 args are direct cdef parameters.
If one ever does, `tostring(n)` emits scientific notation above ~1e14 (hrtime
scale) and `from_chars<int64_t>` rejects it. Use `string.format("%.0f", n)`.

## Adding a payload

1. Arbitrary bytes possible → length-prefix. Otherwise → shared separators plus
   an encoder assert.
2. Import the constants; never hardcode `\x1e`/`\x1f`.
3. Change the Lua and C++ sides in the same commit.
4. Unit-test the parser with whitespace, control bytes, and multibyte UTF-8
   (`tests/lua/ffi_analyze.lua` is the pattern).
