// tests/Debug/Debug.cpp
//
// Debug utilities and scratch tests for development.
// Enable a test by removing DISABLED_ prefix.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="DebugTest.*"
//   - Or: ./vimfy_unit_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>
#include <cassert>
#include <climits>
#include <cstdio>

import std.compat;
import vf.Interpreter.EditInterpreter;
import vf.Interpreter.MovementInterpreter;
import vf.Keyboard.Config;
import vf.Optimizer.TransformOptimizer.TransformOptimizer;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.DiffPlanner.MyersDiff;
import vf.Optimizer.DiffPlanner.VimDiff;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Boundary.TransformBoundary;
import vf.Boundary.NavBoundary;
import vf.tests.Utils.NeovimOracle;
import vf.Utils.PrettyText;
import vf.tests.Utils.RandomBufferHelpers;
import vf.tests.Utils.RandomGeneration;
import vf.Optimizer.TransformOptimizer.TransformState;
import vf.VimCore.VimEditUtils;
import vf.VimCore.VimEndpointUtils;

using namespace std;

namespace {

}
