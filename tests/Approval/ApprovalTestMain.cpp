#define APPROVALS_GOOGLETEST_EXISTING_MAIN
#include <gtest/gtest.h>
#include "ApprovalTests.hpp"

import std.compat;
import vf.tests.TestMainSupport;

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  ApprovalTests::initializeApprovalTestsForGoogleTests();
  auto directoryDisposer = ApprovalTests::Approvals::useApprovalsSubdirectory("fixtures");
  auto namerDisposer = ApprovalTests::TemplatedCustomNamer::useAsDefaultNamer(
      "{TestSourceDirectory}/{ApprovalsSubdirectory}"
      "{TestCaseName}.{ApprovedOrReceived}.{FileExtension}");
  installTimingListener();
  return RUN_ALL_TESTS();
}
