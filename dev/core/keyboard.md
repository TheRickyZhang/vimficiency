# Keyboard Module

The `src/Keyboard/` module handles physical keyboard representation, key mappings, and sequence-to-keys conversion. This is the foundation for effort-based cost calculation in the optimizer.

## Key Concepts

| Concept | Description | Source |
|---------|-------------|--------|
| `Key` | Physical key enum (61 keys) | `Key.cppm` |
| `PhysicalKeys` | Ordered list of keys representing keystrokes | `PhysicalKeys.cppm` |
| `Hand`, `Finger` | Ergonomic metadata for effort calculation | `Hand.cppm`, `Finger.cppm` |
| `CharToKeys` | Single character → key sequence | `ToKeys/CharToKeys.cppm` |
| `CommandToKeys` | Token string → key sequence map | `ToKeys/CommandToKeys.cppm` |
| `CountToKeys` | Prebuilt count-prefix keys/text | `ToKeys/CountToKeys.cppm` |
| `SequenceToKeys` | Vim sequence → physical keys | `ToKeys/SequenceToKeys.cppm` |

## X Macros (`XMacroKey.inc`, `XMacroHand.inc`, `XMacroFinger.inc`, `XMacroKeyedSequence.h`)

**Single source of truth** for key/hand/finger definitions. Uses X-macro include files to generate enums, name arrays, and FFI tables.

```cpp
enum class Key : int {
#define X(name, str) name,
#include "XMacroKey.inc"
#undef X
  None
};
```

This generates:
- **Enum values**: `enum class Key { Key_A, Key_B, ..., None }`
- **Name arrays**: Used for FFI and debug output
- **Static assertions**: Verify counts match at compile time

### Supported Keys (61 total)
- Letters: `Q-P`, `A-L`, `Z-M` (26)
- Digits: `0-9` (10)
- Punctuation: `Semicolon`, `Comma`, `Period`, `Slash`, `Grave`, `Minus`, `Equal`, `LBracket`, `RBracket`, `Backslash`, `Apostrophe` (11)
- Modifiers: `Ctrl`, `Shift` (2)
- Special: `Esc`, `Tab`, `Enter`, `Backspace`, `Space`, `Delete` (6)
- Navigation: `Home`, `End`, `Left`, `Down`, `Up`, `Right` (6)

### Hands and Fingers

```cpp
enum class Hand : int8_t {
#define X(name, str) name,
#include "XMacroHand.inc"
#undef X
  None
};

enum class Finger : int8_t {
#define X(name, str) name,
#include "XMacroFinger.inc"
#undef X
  None
};
```

Mapping: `L`/`R` = hand, `p`/`r`/`m`/`i`/`t` = pinky/ring/middle/index/thumb.

## PhysicalKeys

Represents a sequence of physical key presses. Used as the input to effort calculation.

```cpp
PhysicalKeys keys;
keys.push_back(Key::Key_3);
keys.push_back(Key::Key_W);
// Represents typing "3w"
```

**Key methods:**
- `append(other, count)` - append keys, optionally repeated
- `makeCountedKeys(count, motionKeys)` - prepend digit keys for counted motions

## Character and String Mappings

### CharToKeys (`ToKeys/CharToKeys.cppm`)

`CHAR_TO_KEYS` is the single char → keys table: every printable ASCII byte plus `\t`, `\n`, `\r` (`'a' -> {Key_A}`, `'!' -> {Key_Shift, Key_1}`). It is the sole source of key sequences for plain characters; command tokens like `gg` or `dw` price as the concatenation of their chars.

### CommandToKeys (`ToKeys/CommandToKeys.cppm`)

`std::unordered_map<string, PhysicalKeys>` with transparent hashing for `string_view` lookups. Used for the `<...>` key notations (`<Esc>`, `<C-d>`, `<lt>`), whose keys are not derivable from their chars.

### MovementToKeys (`ToKeys/MovementToKeys.cppm`)

| Symbol | Purpose |
|--------|---------|
| `ALL_MOTIONS` | `MotionSet` of supported motion tokens, for parser validation (`SequenceParser`, `MovementInterpreter`) |
| `KEY_NOTATIONS` | File-local `CommandToKeys` of `<...>` notations, feeding `globalSequenceToKeys()` |

`ALL_MOTIONS` is not the optimizer's search set. NavOptimizer enumerates motions via `MovementToSpec` specs and the count-search pair specs in `Optimizer/NavOptimizer/CountableMovementPair.cppm`. Note `SCROLL_MOTIONS` is `<C-d>`/`<C-u>` only — `<C-f>`/`<C-b>` are excluded as viewport-dependent (see `MovementToSpec.cpp`).

## Sequence Tokenization

`SequenceToKeys::tokenize` matches `KEY_NOTATIONS` longest-first at each position, otherwise prices the byte via `CHAR_TO_KEYS` (unknown bytes warn and are skipped). `tests/Unit/Misc/SequenceToKeysTest.cpp` pins that every non-notation token is charwise.

```
Sequence "l3wfD;"
  -> Keys: {Key_L, Key_3, Key_W, Key_F, Key_Shift, Key_D, Key_Semicolon}
```

**Important:** This is for physical effort calculation only. For semantic parsing (understanding what motions/edits are being executed), see `Interpreter/SequenceParser.cppm`.

## Effort Model

The `RunningEffort` class (`Effort/RunningEffort.cppm`) computes typing cost from `PhysicalKeys`:

**Terms, each weighed by `ScoreWeights` as the key is appended:**
- Base key cost (some keys harder than others)
- Same-finger bigrams on different keys (penalty)
- Same-key repeats (priced separately, not as same-finger)
- Hand alternation (bonus)
- Good/bad rolls (inward vs outward finger sequences)

Only the running total, the stroke count, the additive penalty, and the
first/last key are stored, so a `RunningEffort` is tied to the `Config` it was
built with; `merge` and `appendFrom` take that config to price the seam bigram.

The total is the effort score used by the
optimizer's A* heuristic. Every term is bigram-window: `RunningEffort` is a
monoid with O(1) `merge` and a one-key boundary
(`typing s[q:j) = PS(j) - PS(q) - cut(q)`, pinned by
`tests/Unit/Misc/EffortDecompositionTest.cpp`). A metric with a wider context
window (e.g. a long same-hand-run penalty) would break that identity — that
test fails first if one is added. The VimDiff planner prices inserts per key
and does not rely on it.

## Adding New Keys or Commands

1. **New physical key**: Add to `XMacroKey.inc`, update any related key metadata in `Config`
2. **New character mapping**: Add to the matching category map in `ToKeys/CharToKeys.cpp`
3. **New `<...>` notation**: Add to `KEY_NOTATIONS` in `ToKeys/MovementToKeys.cpp`
4. **New motion**: Add the token to `ALL_MOTIONS`; its keys derive from `CHAR_TO_KEYS`

## Semantics
Note that append(thing, count) -> append count copies of thing, while append(count, thing) -> append count as chars literally, then thing
