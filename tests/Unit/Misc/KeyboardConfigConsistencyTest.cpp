// tests/Unit/Misc/KeyboardConfigConsistencyTest.cpp
//
// Each keyboard layout assigns every key a (hand, finger). The two must agree:
// RunningEffort gates rolls on KeyInfo::hand but scores them by finger
// position, so a key whose finger belongs to the other hand would be rolled
// against fingers it cannot roll with.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="KeyboardConfigConsistency.*"

#include <gtest/gtest.h>

import std.compat;
import vf.Keyboard.Config;
import vf.Keyboard.Finger;
import vf.Keyboard.Key;

TEST(KeyboardConfigConsistency, ColemakSpaceHandMatchesFinger) {
  Config cfg = Config::colemakDh();
  const KeyInfo& space = cfg.keyInfo[static_cast<uint8_t>(Key::Key_Space)];
  EXPECT_EQ(fingerToHand(space.finger), space.hand);
}

TEST(KeyboardConfigConsistency, EveryKeyHandMatchesFinger) {
  struct Layout {
    const char* name;
    Config cfg;
  };
  Layout layouts[] = {
      {"qwerty", Config::qwerty()},
      {"colemakDh", Config::colemakDh()},
  };
  for (const auto& layout : layouts) {
    for (int i = 0; i < KEY_COUNT; ++i) {
      const KeyInfo& ki = layout.cfg.keyInfo[i];
      // The roll gate in bigramEffort keys on hand and then reads finger.
      EXPECT_EQ(ki.hand == Hand::None, ki.finger == Finger::None)
          << layout.name << " key index " << i;
      if (ki.hand == Hand::None) continue;
      EXPECT_EQ(fingerToHand(ki.finger), ki.hand)
          << layout.name << " key index " << i;
    }
  }
}
