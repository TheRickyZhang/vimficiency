import std.compat;
import vf.tools.Exploration.ExplorationCollector;
import vf.tests.Utils.SeedManager;

using namespace std;

static_assert(SEARCH_TRACE_STATS_ENABLED,
              "vimfy_explore must be built with -DVIMF_TRACK_STATES=ON; "
              "without it, exploredStates_ stays empty and the dashboard "
              "exploration tree/charts cannot render.");

int main() {
  auto& seedMgr = SeedManager::instance();
  cout << "Exploration collector\n";
  cout << "Seed mode: " << (seedMgr.isRandom() ? "random" : "fixed") << "\n\n";

  auto motionCases = collectMotionCases();
  writeExplorationJson("motion_explore.json", motionCases);

  auto editCases = collectEditCases();
  writeExplorationJson("edit_explore.json", editCases);

  auto compositionCases = collectCompositionCases();
  writeCompositionExplorationJson("composition_explore.json", compositionCases);

  cout << "\nDone.\n";
  return 0;
}
