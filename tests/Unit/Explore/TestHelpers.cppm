module;
#include <gtest/gtest.h>

export module vf.tests.Unit.Explore.TestHelpers;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Effort.RunningEffort;
export import vf.Explore.Explore;
export import vf.Interpreter.SequenceParser;
export import vf.Keyboard.Config;
export import vf.LuaExports.Common;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.NavOptimizer.NavOptimizer;
export import vf.Optimizer.NavOptimizer.NavRangeConversion;
export import vf.Optimizer.OptimizerParamOverrides;
export import vf.Optimizer.Result;
export import vf.Optimizer.TransformOptimizer.TransformFrontier;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.NavContext;

export {

class ExploreViewTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};

  Explore::View makeView(Lines initial, CursorPos initialPos, Lines goal,
                         CursorPos goalPos) {
    NavBoundary boundary = VF::LuaExports::sliceBoundary(
        initial, 0, initial.back().size(),
        /*hasLinesAbove=*/false,
        /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }

  Explore::View makeViewWithBoundary(Lines initial, CursorPos initialPos, Lines goal,
                                     CursorPos goalPos, CursorPos boundaryBegin,
                                     CursorPos boundaryEnd) {
    NavBoundary boundary(initial, boundaryBegin, boundaryEnd,
                         /*hasLinesAbove=*/false,
                         /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }
};

}  // export
