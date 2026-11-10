#include <gtest/gtest.h>

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Interpreter.MovementInterpreter;
import vf.Keyboard.Config;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;

using namespace std;

TEST(NavDebug, DISABLED_InvestigateSubBufferSentenceCountSeed) {
  Lines fullBuffer = {
      ",.acc .bbc.ddab.a bbbbbca,.,",
      "addd  dc bc.bdbd.adbb b .a,a",
      "cbcc ,.c,a,b",
      "dc c.,,b. dcc ccadd,,.,, da,.",
      "c,c ,a cada... b a",
      ".bbbaccc,ddb .",
      ". abc aa.dbcb,... b. a,b.ac c",
      "aa dadb,c..a",
  };
  Lines subBuffer = {
      fullBuffer[3],
      fullBuffer[4],
      fullBuffer[5],
      fullBuffer[6],
  };
  CursorPos subStart(0, 24);
  CursorPos subEnd(3, 21);
  NavBoundary boundary(
      fullBuffer,
      CursorPos(3, 0),
      CursorPos(6, fullBuffer[6].effectiveSize()));

  NavOptimizer opt(Config::uniform());
  NavContext navContext;
  auto results = opt.optimize(
      subBuffer, subStart, subEnd, {}, "", boundary, navContext).getResults();

  cerr << "results=" << results.size() << "\n";
  for (const auto& result : results) {
    CursorPos endpoint =
        simulateMovements(subStart, result.getSequence().view(), subBuffer);
    cerr << "  " << result.getSequence() << " -> " << endpoint << "\n";
  }
}
