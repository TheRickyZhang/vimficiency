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

  inline SequenceBinding(const KeyedSequence& base, const RunningEffort& effort)
      : count(0), base(base), effort(effort) {}

  inline SequenceBinding(const KeyedSequence& base, const RunningEffort& effort, int count)
      : count(count), base(base), effort(effort) {}
};

}  // export
