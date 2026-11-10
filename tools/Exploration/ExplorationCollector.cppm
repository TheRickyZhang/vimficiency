export module vf.tools.Exploration.ExplorationCollector;

import std.compat;
export import vf.Keyboard.Config;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.SearchStats;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

struct FoundResult {
  std::string sequence;
  double effort;
};

struct ContextData {
  std::vector<std::string> initialLines;
  std::vector<std::string> goalLines;
  int initialCursorLine = -1, initialCursorCol = -1;
  int goalCursorLine = -1, goalCursorCol = -1;
  // Range goal (motion-to-range cases): the [begin, end) span of acceptable
  // landing cells. All -1 for a point goal, in which case goalCursor* applies.
  int goalRangeBeginLine = -1, goalRangeBeginCol = -1;
  int goalRangeEndLine = -1, goalRangeEndCol = -1;
  std::string prefix;
  std::string suffix;
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
};

struct ExploreCase {
  std::string name;
  BaseSearchStats stats;
  std::vector<FoundResult> results;
  ContextData context;
};

struct PerDiffEditExploration {
  std::vector<ExploredState> states;
  std::vector<FoundResult> results;
};

struct CompositionExploreCase {
  std::string name;
  int nodesExplored;
  std::vector<FoundResult> results;
  std::vector<CompositionExploredState> states;
  ContextData context;
  std::vector<DiffState> diffs;
  std::vector<PerDiffEditExploration> editDetails;
};

Config config = Config::uniform();

std::string jsonEscape(std::string_view s);
std::vector<std::string> tokenizeSequence(const std::string& seq);
void writeContextJson(std::ofstream& out, const ContextData& ctx);

void writeExplorationJson(const std::string& filename,
                          const std::vector<ExploreCase>& cases);
void writeCompositionExplorationJson(
    const std::string& filename,
    const std::vector<CompositionExploreCase>& cases);

std::vector<ExploreCase> collectMotionCases();
std::vector<ExploreCase> collectEditCases();
std::vector<CompositionExploreCase> collectCompositionCases();

}  // export
