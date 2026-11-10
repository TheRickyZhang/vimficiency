export module vf.Keyboard.Config;

import std.compat;
export import vf.Keyboard.Finger;
export import vf.Keyboard.Hand;
export import vf.Keyboard.Key;

export {

// -----------------------------------------------------------------------------
// Key metadata and weights
// -----------------------------------------------------------------------------

struct KeyInfo {
  Hand hand       = Hand::None;
  Finger finger   = Finger::None;
  double base_cost = 0.0;
};

// todo tune these
struct ScoreWeights final {
  double key_weight          = 1.0;
  double same_finger_weight{};
  double same_key_weight{};
  double alt_hand_weight{};
  double good_roll_weight{};
  double bad_roll_weight{};
};

// Use factory pattern
struct Config {
  std::array<KeyInfo, KEY_COUNT> keyInfo{};

  ScoreWeights weights{};

  static Config qwerty();
  static Config colemakDh();
  static Config uniform();

private:
  Config() = default;
};

}  // export
