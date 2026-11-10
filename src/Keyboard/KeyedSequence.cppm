module;
#include "XMacroKeyedSequence.h"
#include <cassert>

export module vf.Keyboard.KeyedSequence;

import std.compat;
export import vf.Keyboard.PhysicalKeys;
export import vf.Keyboard.ToKeys.CountToKeys;
export import vf.Keyboard.ToKeys.CharToKeys;
export import vf.Types.KeyNotation;
export import vf.Types.Sequence;

export {

// Note the intentional declaration order in this file with KSId and X-Macro expansions

#define KS_ENUM_VALUE(name, seq, keys) name,
enum class KSId : uint8_t {
  VF_KEYED_SEQUENCES(KS_ENUM_VALUE)
  COUNT
};
#undef KS_ENUM_VALUE

inline constexpr int KS_COUNT = static_cast<int>(KSId::COUNT);
static_assert(KS_COUNT == 62, "Expected 62 static KeyedSequence constants");

// =============================================================================
// KeyedSequence
// =============================================================================

struct KeyedSequence {
  Sequence seq;
  PhysicalKeys keys;

  KeyedSequence() = default;
  inline KeyedSequence(std::string_view s, PhysicalKeys k) : seq(std::string(s)), keys(std::move(k)) {}
  inline KeyedSequence(int count, const KeyedSequence& base) {
    appendCounted(count, base);
  }

  inline KeyedSequence& operator+=(const KeyedSequence& other) {
    seq.append(other.seq.view());
    keys += other.keys;
    return *this;
  }

  // Primitive appends
  inline void append(char c, int count = 1) {
    seq.append(displayChar(c), count);
    keys.append(CHAR_TO_KEYS.at(c), count);
  }
  inline void append(std::string_view text) {
    keys.reserve(keys.size() + text.size());
    for (char c : text) {
      seq.append(displayChar(c));
      keys.append(CHAR_TO_KEYS.at(c));
    }
  }

  // Same type appends
  inline void append(const KeyedSequence& ks, int count) {
    seq.append(ks.seq.view(), count);
    keys.append(ks.keys, count);
  }

  inline void appendCounted(int count, const KeyedSequence& base) {
    assert(count >= CountPrefixLimits::MIN_PREFIX_COUNT && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
    seq.append(CountToKeys::textForCount(count));
    seq.append(base.seq.view());
    keys.append(CountToKeys::keysForCount(count));
    keys.append(base.keys);
  }

  // Convert delete command to its change equivalent (d→c in both seq and keys).
  inline KeyedSequence asChange() const {
    assert(!seq.view().empty() && seq.view()[0] == 'd');
    return {std::string("c") + std::string(seq.view().substr(1)), keys.asChange()};
  }

  // Static constants — declared via X-macro
#define KS_DECLARE(name, seq, keys) static const KeyedSequence name;
  VF_KEYED_SEQUENCES(KS_DECLARE)
#undef KS_DECLARE

  // KeyedSequence::byId: KSId -> const predefined KeyedSequence&
  static inline const KeyedSequence& byId(KSId id) {
    // Flat array of pointers, initialized once via X-macro
#define KS_PTR(name, seq, keys) &KeyedSequence::name,
    static const std::array<const KeyedSequence*, KS_COUNT> table = {{
      VF_KEYED_SEQUENCES(KS_PTR)
    }};
#undef KS_PTR
    return *table[static_cast<uint8_t>(id)];
  }
};

}  // export

// Static constant definitions — generated via X-macro. Outside the export
// block: member definitions are not namespace-scope declarations, so they
// cannot be exported (the class already is). Non-inline so they are ordered
// by the module initializer, ahead of importers' globals.
#define KS_DEFINE(name, seqStr, keyGroup) \
  const KeyedSequence KeyedSequence::name{seqStr, {STRIP_PARENS(keyGroup)}};
VF_KEYED_SEQUENCES(KS_DEFINE)
#undef KS_DEFINE
