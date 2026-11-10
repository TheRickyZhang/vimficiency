#include <gtest/gtest.h>

import std.compat;
import vf.tests.TestMainSupport;

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  installTimingListener();
  return RUN_ALL_TESTS();
}
