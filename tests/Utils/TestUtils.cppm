module;
#include <cassert>

export module vf.tests.Utils.TestUtils;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Keyboard.Config;
export import vf.Keyboard.Key;
export import vf.Optimizer.NavOptimizer.NavOptimizer;
export import vf.Types.Lines;
export import vf.Types.NavContext;

export {

// =============================================================================
// Result Inspection Helpers
// =============================================================================

// Check if any result contains the exact sequence. Generic over any Result
// subclass so it works with both `Result` and `LandingResult` lists.
template <typename ResultT>
inline bool hasSequence(const std::vector<ResultT>& results, const std::string& seq) {
  return std::any_of(results.begin(), results.end(),
      [&seq](const ResultT& r) { return r.getSequence() == seq; });
}

template <typename ResultT>
inline bool hasSequenceContaining(const std::vector<ResultT>& results,
                                  std::string_view needle) {
  return std::any_of(results.begin(), results.end(), [&](const ResultT& r) {
    return r.getSequence().view().find(needle) != std::string_view::npos;
  });
}

template <typename ResultT>
bool contains_all(const std::vector<ResultT>& v,
                  std::initializer_list<std::string> need) {
  std::unordered_set<std::string> s;
  for (const auto& r : v) s.insert(r.getSequence().str());
  for (const auto& x : need) if (s.find(x) == s.end()) return false;
  return true;
}

// =============================================================================
// Optimizer Runners
// =============================================================================

// Standard bounded Nav search used by the unit suites.
inline std::vector<LandingResult> runNavOptimizer(
    const Lines& lines, CursorPos start, CursorPos end,
    const std::string& userSeq,
    const Config& config = Config::uniform(),
    const NavBoundary& boundary = NavBoundary{},
    const NavContext& navContext = NavContext{}) {
  NavOptimizer opt(config);
  return opt.optimize(lines, start, end,
                      NavOptimizerParams{}
                          .withMaxResults(30)
                          .withMaxNodesPopped(20000)
                          .withMaxResultsPerEndPos(2),
                      userSeq, boundary, navContext).getResults();
}

struct KeyAdjustment {
  Key k;
  double cost;
  KeyAdjustment(Key k, double cost) : k(k), cost(cost) {}
};

namespace TestFiles {

inline Lines load(const std::string& filename) {
  auto path = std::filesystem::path(__FILE__).parent_path() / ".." / ".." /
              "data" / "TestFiles" / filename;
  std::ifstream file(path);
  if (!file) {
    assert(false && "Cannot open test file");
  }
  Lines lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace TestFiles

}  // export
