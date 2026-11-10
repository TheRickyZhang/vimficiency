export module vf.Optimizer.SequenceBinding;

import std.compat;
export import vf.Keyboard.KeyedSequence;
export import vf.Effort.RunningEffort;

export {

// Command payload used across TransformOptimizer exploration callbacks.
// - count=0 means uncounted.
// - base is the canonical command token (without count prefix).
// - effort is the precomputed effort for typing count+base (or base when count=0),
//   including any penalties already applied.
struct SequenceBinding {
  int count = 0;
  const KeyedSequence& base;
  const RunningEffort& effort;

  inline SequenceBinding(const KeyedSequence& base, const RunningEffort& effort, int count = 0)
      : count(count), base(base), effort(effort) {}
};

// `base` carries no count prefix (`ParsedEdit.edit` / `DotRepeat.base` / the
// binding's `base`), so the count must be prepended explicitly.
inline std::string countedCommandString(int count, std::string_view base) {
  if (count <= 0) return std::string(base);
  return std::to_string(count) + std::string(base);
}

}  // export
