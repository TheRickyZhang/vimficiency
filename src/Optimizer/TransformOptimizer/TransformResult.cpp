module;
#include <cassert>

module vf.Optimizer.TransformOptimizer.TransformResult;

import std.compat;

using namespace std;

TransformResult::TransformResult(vector<vector<Result>> results, TransformSearchStats stats,
                       const Lines& initialLines, int bufferBeginLine,
                       int bufferBeginCol, CursorPos goalPos,
                       vector<vector<CursorPos>> resultGoalPositions)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      goalPos_(goalPos),
      beginLine_(bufferBeginLine),
      resultGoalPositions_(std::move(resultGoalPositions)) {
  if (!resultGoalPositions_.empty()) {
    assert(resultGoalPositions_.size() == results_.size() &&
           "result cursor matrix must match transform result buckets");
    for (size_t i = 0; i < results_.size(); i++) {
      assert(resultGoalPositions_[i].size() == results_[i].size() &&
             "each result cursor bucket must match result count");
    }
  }

  indexToBufferPos_.assign(results_.size(), CursorPos(-1, -1));
  validStartByIndex_.assign(results_.size(), true);

  lineBaseIndex_.reserve(initialLines.size());
  int cumSum = 0;
  for (int line = 0; line <= initialLines.lastLine(); line++) {
    int colOffset = (line == 0) ? bufferBeginCol : 0;
    lineBaseIndex_.push_back(cumSum - colOffset);
    cumSum += initialLines[line].effectiveSize();
  }

  for (int line = 0; line <= initialLines.lastLine(); line++) {
    const int positions = initialLines[line].effectiveSize();
    const int firstCol = (line == 0) ? bufferBeginCol : 0;
    for (int localCol = 0; localCol < positions; localCol++) {
      const int bufferCol = firstCol + localCol;
      const int idx = resultIndexAt(bufferBeginLine + line, bufferCol);
      if (idx >= 0) {
        indexToBufferPos_[idx] = CursorPos(bufferBeginLine + line, bufferCol);
      }
    }
  }

  // Sort each bucket by cost (best first) — pop order is approximate due to
  // inadmissible heuristic and suffix-cache emissions.
  for (size_t i = 0; i < results_.size(); i++) {
    auto& bucket = results_[i];
    if (resultGoalPositions_.empty()) {
      std::sort(bucket.begin(), bucket.end(),
                [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
      continue;
    }

    auto& goalBucket = resultGoalPositions_[i];
    vector<size_t> order(bucket.size());
    for (size_t j = 0; j < order.size(); j++) order[j] = j;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return bucket[a].getCost() < bucket[b].getCost();
    });

    vector<Result> sortedBucket;
    vector<CursorPos> sortedGoals;
    sortedBucket.reserve(bucket.size());
    sortedGoals.reserve(goalBucket.size());
    for (size_t idx : order) {
      sortedBucket.push_back(std::move(bucket[idx]));
      sortedGoals.push_back(goalBucket[idx]);
    }
    bucket = std::move(sortedBucket);
    goalBucket = std::move(sortedGoals);
  }

  rebuildStartPositions();
}

void TransformResult::rebuildStartPositions() {
  startPositions_.clear();
  for (int idx = 0; idx < std::ssize(results_); idx++) {
    if (!isValidStartIndex(idx)) continue;
    if (results_[idx].empty()) continue;
    CursorPos pos = indexToBufferPos_[idx];
    if (pos.isValid()) startPositions_.push_back(pos);
  }
}

void TransformResult::restrictValidStartRegion(CharRange validStartRegion) {
  validStartRegion.normalize();
  for (size_t idx = 0; idx < validStartByIndex_.size(); idx++) {
    CursorPos pos = indexToBufferPos_[idx];
    validStartByIndex_[idx] =
        pos.isValid() && pos >= validStartRegion.begin && pos < validStartRegion.end;
  }
  rebuildStartPositions();
}

ostream& operator<<(ostream& os, const TransformResult& transformResult) {
  os << transformResult.stats_ << " goalPos=" << transformResult.goalPos_ << "\n";
  for (size_t i = 0; i < transformResult.results_.size(); i++) {
    const auto& bucket = transformResult.results_[i];
    if (!bucket.empty()) {
      os << "  [" << i << "] " << bucket[0] << "\n";
    }
  }
  return os;
}
