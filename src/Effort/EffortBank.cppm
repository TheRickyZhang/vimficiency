export module vf.Effort.EffortBank;

import std.compat;
export import vf.Effort.RunningEffort;
export import vf.Keyboard.Config;
export import vf.Keyboard.KeyedSequence;

export {

// Pre-computed RunningEffort for each static KeyedSequence constant (indexed by KSId).
// Avoids recomputing effort from keys on every emitMotion call.
struct EffortBank {
  std::array<RunningEffort, KS_COUNT> efforts;

  inline explicit EffortBank(const Config& config) {
    for (int i = 0; i < KS_COUNT; ++i) {
      efforts[i] = RunningEffort(KeyedSequence::byId(static_cast<KSId>(i)).keys, config);
    }
  }

  inline const RunningEffort& operator[](KSId id) const {
    return efforts[static_cast<uint8_t>(id)];
  }
};

}  // export
