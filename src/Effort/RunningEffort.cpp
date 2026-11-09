#include "Effort/RunningEffort.h"

#include "Keyboard/Finger.h"
#include "Keyboard/ToKeys/MovementToKeys.h"

namespace {

double bigramEffort(Key prev, Key next, const Config& model) {
  if (prev == Key::None) return 0.0;

  const KeyInfo& a = model.keyInfo[static_cast<uint8_t>(prev)];
  const KeyInfo& b = model.keyInfo[static_cast<uint8_t>(next)];
  const auto& w = model.weights;

  double s = 0.0;
  if (prev == next) {
    s += w.same_key_weight;
  } else if (a.finger != Finger::None && a.finger == b.finger) {
    s += w.same_finger_weight;
  }

  if (a.hand != Hand::None && b.hand != Hand::None && a.hand != b.hand) {
    s += w.alt_hand_weight;
  }

  if (a.finger != Finger::None && b.finger != Finger::None &&
      sameHand(a.finger, b.finger)) {
    int delta = static_cast<uint8_t>(fingerToPosition(b.finger)) -
                static_cast<uint8_t>(fingerToPosition(a.finger));
    if (delta > 0)      s += w.good_roll_weight;
    else if (delta < 0) s += w.bad_roll_weight;
  }
  return s;
}

} // namespace

RunningEffort::RunningEffort(const PhysicalKeys& keys, const Config& model) {
  append(keys, model);
}

double RunningEffort::append(const PhysicalKeys& keys, const Config& model) {
  for(Key k : keys) {
    appendSingle(k, model);
  }
  return getEffort();
}

void RunningEffort::appendSingle(Key key, const Config &model) {
  typing_effort += model.weights.key_weight * model.keyInfo[static_cast<uint8_t>(key)].base_cost;
  typing_effort += bigramEffort(last_key, key, model);

  if (strokes == 0) first_key = key;
  last_key = key;
  ++strokes;
}

void RunningEffort::reset() {
  *this = RunningEffort();
}

RunningEffort RunningEffort::merge(const RunningEffort& a, const RunningEffort& b,
                                   const Config& model) {
  RunningEffort r = a;
  r.appendFrom(b, model);
  return r;
}

double RunningEffort::appendFrom(const RunningEffort& other, const Config& model) {
  if (other.strokes == 0) return getEffort();
  if (strokes == 0) {
    *this = other;
    return getEffort();
  }

  typing_effort += other.typing_effort + bigramEffort(last_key, other.first_key, model);
  strokes       += other.strokes;
  penalty_      += other.penalty_;
  last_key       = other.last_key;

  return getEffort();
}

double getEffort(std::string_view seq,
                 const Config      &cfg) {
  PhysicalKeys keys = globalSequenceToKeys().tokenize(seq);
  return RunningEffort(keys, cfg).getEffort();
}
