# Utility Patterns


General-purpose utilities in `src/Utils/`.


## Lines (`Lines.cppm`)

Wrapper around `vector<string>` with position-aware character access. Key methods:
- `get(pos)` - returns char at position, `'\n'` for empty lines
- `getNextPos()` / `getPrevPos()` - step to adjacent char (skip empty lines)
- `getNextPosIncludeEmpty()` / `getPrevPosIncludeEmpty()` - step including empty lines
- `flatten()` / `Lines::unflatten()` convert between buffer lines and newline-delimited text.
- `hashLines(lines)` - FNV-1a hash over all line contents + line count. Stored on `TransformEditorState` and read by `TransformStateKey`/`SuffixKey` to avoid full buffer copies in A* costmap keys (see `dev/optimizer/transform-optimizer.md` § State Hashing).

See `boundary-logic.md` for when to use each variant.

## Sequence Parsing (`Interpreter/SequenceParser.cppm`)

`parseSequence()` / `parseSequenceStrings()` split sequences into:
- Motions: `w`, `3j`, `fa`, `;`, `<C-d>`
- Edits/Changes: `ciw`, `dd`, `D`, `s`
- Typed text: Characters typed in insert mode (between change command and `<Esc>`)
- Special keys: `<Esc>`

`parseSequence` / `parseSequenceStrings` return
`std::expected<..., SequenceParseError>` with two error kinds:
`UnknownCharacter` (byte not recognized in command-mode slot) and
`MalformedSpecialKey` (`<` without a valid closing `>` in a
command-mode slot). Insert-mode typed text stays *tolerant*: a bare
`<` with no closing `>` is treated as a literal character, matching
Vim's own behavior for typed text.

The FFI export `vf_tokenize_sequence` returns parse failures in a protobuf
error envelope. Internal callers that already guarantee a valid sequence can
take the parsed value directly. See the ADR section "Error-handling boundary"
for the rationale.

**Usage**: FFI tokenization and `SequenceDisplay`. Raw sequences are used internally for execution and cost calculation.

## Debug (`Debug.cppm`)

`debug()` is a function template gated by the `VIMFICIENCY_DEBUG` preprocessor
define. The CMake option currently emits a different name. See the
build-flag item in `dev/architecture/todo.md`.
