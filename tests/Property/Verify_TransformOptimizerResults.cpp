// Property: TransformOptimizer results for generated edit-region problems
// must replay in Neovim to the exact goal while preserving protected full-buffer
// prefix/suffix context around embedded regions.

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

import std.compat;
import vf.Boundary.TransformBoundary;
import vf.Keyboard.Config;
import vf.Optimizer.BuildTypedCommands;
import vf.Optimizer.TransformOptimizer.TransformOptimizer;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.tests.Utils.NeovimOracle;
import vf.tests.Property.OptimizerResultChecks;
import vf.tests.Property.PropertyDomains;
using namespace std;

namespace {

// Edit slice plus the surrounding full-buffer context that must survive it.
struct EmbeddedEditRegion {
  Lines fullBuffer;
  Lines editRegion;
  string prefix;
  string suffix;

  int startLine;
  int startCol;
  int endLine;
  int endCol;

  TransformBoundary makeBoundary() const {
    return TransformBoundary(fullBuffer, {startLine, startCol}, {endLine, endCol});
  }

  CursorPos toFullBufferPos(const CursorPos& editPos) const {
    CursorPos result = editPos;
    result.line += startLine;
    if (editPos.line == 0) result.col += static_cast<int>(prefix.size());
    return result;
  }

  string expectedAfterDeletion() const { return prefix + suffix; }
};

struct FullBufferSpec {
  vector<string> source;
};

struct SameLengthReplacementSpec {
  string original;
  string replacement;
};

struct SingleLineChangeSpec {
  string source;
  string goal;
};

struct MultiLineChangeSpec {
  vector<string> source;
  vector<string> goal;
};

struct EmbeddedRegionSpec {
  vector<string> editRegion;
  string prefix;
  string suffix;
};

struct EmbeddedChangeSpec {
  vector<string> editRegion;
  vector<string> goal;
  string prefix;
  string suffix;
};

string normalizedNonEmpty(string text, char fallback) {
  if (text.empty()) {
    text.push_back(fallback);
  }
  return text;
}

SameLengthReplacementSpec normalizeSameLength(
    SameLengthReplacementSpec spec) {
  spec.original = normalizedNonEmpty(std::move(spec.original), 'a');
  spec.replacement = normalizedNonEmpty(std::move(spec.replacement), 'b');
  if (spec.replacement.size() < spec.original.size()) {
    spec.replacement.append(
        spec.original.size() - spec.replacement.size(), spec.replacement.back());
  }
  if (spec.replacement.size() > spec.original.size()) {
    spec.replacement.resize(spec.original.size());
  }
  if (spec.replacement == spec.original) {
    spec.replacement[0] = spec.replacement[0] == 'a' ? 'b' : 'a';
  }
  return spec;
}

EmbeddedEditRegion toEmbeddedRegion(EmbeddedRegionSpec spec, bool hasOutsideLines = false) {
  EmbeddedEditRegion result;
  result.editRegion = Lines(std::move(spec.editRegion));
  result.prefix = std::move(spec.prefix);
  result.suffix = std::move(spec.suffix);

  if (hasOutsideLines) result.fullBuffer.push_back("above");
  result.startLine = result.fullBuffer.size();
  for (int i = 0; i <= result.editRegion.lastLine(); i++) {
    string line = result.editRegion[i];
    if (i == 0) line.insert(0, result.prefix);
    if (i == result.editRegion.lastLine()) line += result.suffix;
    result.fullBuffer.push_back(line);
  }

  result.startCol = static_cast<int>(result.prefix.size());
  result.endLine = result.fullBuffer.lastLine();
  result.endCol = static_cast<int>(result.fullBuffer[result.endLine].size()) -
                  static_cast<int>(result.suffix.size());
  if (hasOutsideLines) result.fullBuffer.push_back("below");

  return result;
}

string contextForLines(const string& label, const Lines& source, const Lines& goal) {
  ostringstream out;
  out << label << " source=" << source << " goal=" << goal;
  return out.str();
}

string contextForEmbedded(
    const string& label,
    const EmbeddedEditRegion& region,
    const Lines& goal) {
  ostringstream out;
  out << label
      << " prefix='" << region.prefix << "'"
      << " suffix='" << region.suffix << "'"
      << " editRegion=" << region.editRegion
      << " fullBuffer=" << region.fullBuffer
      << " goal=" << goal;
  return out.str();
}

auto FullBufferSpecDomain() {
  return fuzztest::StructOf<FullBufferSpec>(
      PropertyDomains::LineVecDomain(2, 3, 0, 8));
}

auto SameLengthReplacementSpecDomain() {
  return fuzztest::StructOf<SameLengthReplacementSpec>(
      PropertyDomains::LineTextDomain(0, 9),
      PropertyDomains::LineTextDomain(0, 9));
}

auto SingleLineChangeSpecDomain() {
  return fuzztest::StructOf<SingleLineChangeSpec>(
      PropertyDomains::LineTextDomain(0, 8),
      PropertyDomains::LineTextDomain(1, 7));
}

auto MultiLineChangeSpecDomain() {
  return fuzztest::StructOf<MultiLineChangeSpec>(
      PropertyDomains::LineVecDomain(2, 3, 0, 8),
      PropertyDomains::LineVecDomain(1, 3, 0, 8));
}

auto EmbeddedRegionSpecDomain(
    int minLines, int maxLines, int minLineLen, int maxLineLen) {
  return fuzztest::StructOf<EmbeddedRegionSpec>(
      PropertyDomains::LineVecDomain(
          minLines, maxLines, minLineLen, maxLineLen),
      PropertyDomains::LineTextDomain(0, 3),
      PropertyDomains::LineTextDomain(0, 3));
}

auto EmbeddedChangeSpecDomain() {
  return fuzztest::StructOf<EmbeddedChangeSpec>(
      PropertyDomains::LineVecDomain(2, 4, 0, 5),
      PropertyDomains::LineVecDomain(1, 4, 0, 8),
      PropertyDomains::LineTextDomain(0, 3),
      PropertyDomains::LineTextDomain(0, 3));
}

class TransformOptimizerGeneratedPropertyTest {
 public:
  void SingleLineEmbeddedTopResultsReplay(const EmbeddedRegionSpec& spec) {
    auto test = toEmbeddedRegion(spec);
    TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
    Lines expected{test.expectedAfterDeletion()};
    string context = contextForEmbedded("single-line embedded", test, expected);
    runSampledReplays(res, test.fullBuffer, expected, context,
                      hashLines(test.fullBuffer), [&](int startIdx) {
      CursorPos editPos = test.editRegion.cursorFromFlatIndexClamped(startIdx);
      return test.toFullBufferPos(editPos);
    });
  }

  void MultiLineFullBufferTopResultsReplay(const FullBufferSpec& spec) {
    Lines source(spec.source);
    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = pureDeletionResult(source, boundary);
    Lines goal{""};
    string context = contextForLines("multi-line full buffer", source, goal);
    runSampledReplays(res, source, goal, context, hashLines(source),
                      [&](int startIdx) {
      return source.cursorFromFlatIndexClamped(startIdx);
    });
  }

  void SameLengthReplacementTopResultsReplay(
      SameLengthReplacementSpec rawSpec) {
    SameLengthReplacementSpec spec = normalizeSameLength(std::move(rawSpec));
    Lines source = {spec.original};
    Lines goal = {spec.replacement};
    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);

    expectTopResultsReplay(
        oracle_, res.getResults()[0], source, CursorPos(0, 0), goal,
        MAX_RESULTS_PER_START_TO_REPLAY,
        contextForLines("same-length replacement", source, goal));
  }

  void MultiLineEmbeddedTopResultsReplay(const EmbeddedRegionSpec& spec) {
    auto test = toEmbeddedRegion(spec);
    TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
    Lines expected{test.expectedAfterDeletion()};
    string context = contextForEmbedded("multi-line embedded", test, expected);
    runSampledReplays(res, test.fullBuffer, expected, context,
                      hashLines(test.fullBuffer), [&](int startIdx) {
      CursorPos editPos = test.editRegion.cursorFromFlatIndexClamped(startIdx);
      return test.toFullBufferPos(editPos);
    });
  }

  void SingleLineChangeTopResultsReplay(const SingleLineChangeSpec& spec) {
    Lines source = {spec.source};
    Lines goal = {spec.goal};
    if (source == goal) return;

    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);
    string context = contextForLines("single-line change", source, goal);
    runSampledReplays(res, source, goal, context,
                      hashLines(source) ^ hashLines(goal),
                      [&](int startIdx) {
      return source.cursorFromFlatIndexClamped(startIdx);
    });
  }

  void MultiLineFullBufferChangeTopResultsReplay(
      const MultiLineChangeSpec& spec) {
    Lines source(spec.source);
    Lines goal(spec.goal);
    if (source == goal || goal == Lines{""}) return;

    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);
    string context = contextForLines("multi-line full-buffer change", source, goal);
    runSampledReplays(res, source, goal, context,
                      hashLines(source) ^ hashLines(goal),
                      [&](int startIdx) {
      return source.cursorFromFlatIndexClamped(startIdx);
    });
  }

  void MultiLineEmbeddedAlternativesReplay(const EmbeddedChangeSpec& spec, int startIndex,
                                           bool useQwerty, bool hasOutsideLines) {
    auto test = toEmbeddedRegion({spec.editRegion, spec.prefix, spec.suffix}, hasOutsideLines);
    Lines goal(spec.goal);
    if (goal == test.editRegion) return;

    const CursorPos begin(test.startLine, test.startCol);
    TransformOptimizerParams params;
    params.withMaxResults(160).withMaxResultsPerStartPos(20);
    TransformOptimizer optimizer(useQwerty ? Config::qwerty() : Config::uniform());
    const auto boundary = test.makeBoundary();
    const auto res = goal == Lines{""}
        ? optimizer.optimizePureDeletion(test.editRegion, boundary, params, begin.line, begin.col, begin)
        : optimizer.optimizeTransform(test.editRegion, goal, boundary, params, begin.line, begin.col,
                                      typedCommandsExitCursor(begin, goal, spec.suffix));
    if (res.startPositions().empty()) return;
    const auto start = res.startPositions()[startIndex % res.startPositions().size()];

    Lines expected = goal;
    expected.front().insert(0, spec.prefix);
    expected.back() += spec.suffix;
    if (hasOutsideLines) {
      expected.insert(expected.begin(), "above");
      expected.push_back("below");
    }
    res.forEachEditFrom(start, expected, [&](const Result& result, CursorPos landing) {
      EXPECT_TRUE(OracleReplay::matches(oracle_, test.fullBuffer, start, result.getSequence().str(),
                                        expected, landing, Mode::Normal, "embedded edit"));
    });
  }

 private:
  static constexpr size_t MAX_RESULTS_PER_START_TO_REPLAY = 3;
  static constexpr size_t MAX_REPLAY_SAMPLES_PER_ITERATION = 5;

  Config config_ = Config::uniform();
  TransformOptimizerParams params_{};
  TransformOptimizer opt_{config_};
  NeovimOracle oracle_{};

  TransformResult pureDeletionResult(
      const Lines& initialLines, TransformBoundary boundary) {
    return opt_.optimizePureDeletion(initialLines, boundary, params_);
  }

  // Per-iteration replay is capped at MAX_REPLAY_SAMPLES_PER_ITERATION (start,
  // result) pairs sampled deterministically from `seed`. Per-spec coverage is
  // intentionally shallow; FuzzTest's per-iteration spec mutation handles the
  // breadth across iterations.
  template <typename PosForStart>
  void runSampledReplays(
      const TransformResult& res,
      const Lines& initial,
      const Lines& goal,
      const string& context,
      uint64_t seed,
      PosForStart posForStart) {
    auto pairs = sampleReplayPairs(
        res.getResults(), MAX_RESULTS_PER_START_TO_REPLAY,
        MAX_REPLAY_SAMPLES_PER_ITERATION, seed);
    if (pairs.empty()) {
      ADD_FAILURE() << "no non-empty result buckets " << context;
      return;
    }
    for (auto [startIdx, resultIdx] : pairs) {
      CursorPos pos = posForStart(static_cast<int>(startIdx));
      string itemContext = context + " start=" + std::to_string(startIdx)
          + " result=" + std::to_string(resultIdx);
      EXPECT_TRUE(OracleReplay::matches(
          oracle_, initial, pos,
          res.getResults()[startIdx][resultIdx].getSequence().str(),
          goal, std::nullopt, Mode::Normal, itemContext));
    }
  }
};

}  // namespace

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineEmbeddedTopResultsReplay)
    .WithDomains(EmbeddedRegionSpecDomain(1, 1, 2, 4));

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineFullBufferTopResultsReplay)
    .WithDomains(FullBufferSpecDomain());

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SameLengthReplacementTopResultsReplay)
    .WithDomains(SameLengthReplacementSpecDomain());

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineEmbeddedTopResultsReplay)
    .WithDomains(EmbeddedRegionSpecDomain(2, 4, 3, 5));

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineChangeTopResultsReplay)
    .WithDomains(SingleLineChangeSpecDomain());

FUZZ_TEST_F(
    TransformOptimizerGeneratedPropertyTest,
    MultiLineFullBufferChangeTopResultsReplay)
    .WithDomains(MultiLineChangeSpecDomain());

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineEmbeddedAlternativesReplay)
    .WithDomains(EmbeddedChangeSpecDomain(), fuzztest::InRange<int>(0, 31),
                 fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>())
    .WithSeeds({{EmbeddedChangeSpec{{"", "word", ""}, {"Q"}, "", ""}, 5, true, true},
                {EmbeddedChangeSpec{{"end", "x"}, {""}, "abc", "Z"}, 0, true, true}});
