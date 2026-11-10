export module vf.Optimizer.TransformOptimizer.TransformResult;

import std.compat;
export import vf.Optimizer.OptimizerResult;
export import vf.Optimizer.Result;
export import vf.Optimizer.SearchStats;
export import vf.Types.CharRange;
export import vf.Types.Lines;

export {

struct TransformResult : BaseOptimizerResult<std::vector<Result>> {
  TransformResult() = default;

  // Constructor: initializes results and flat index for buffer-position lookup.
  // Buffer-position params default to edit-region-local (line 0, col 0).
  // results is a 2D vector: one bucket of Results per starting position.
  TransformResult(std::vector<std::vector<Result>> results, TransformSearchStats stats,
             const Lines& initialLines, int bufferBeginLine = 0,
             int bufferBeginCol = 0, CursorPos goalPos = {0, 0},
             std::vector<std::vector<CursorPos>> resultGoalPositions = {});

  inline const TransformSearchStats& getStats() const { return stats_; }

  // Look up the best result for a buffer position. Returns nullptr if the position
  // is outside the transform region or has no result.
  // Return a nullable, const & view.
  // TODO (C++ 26): use optional<const T&>
  inline const Result* resultAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return nullptr;
    if (!isValidStartIndex(idx)) return nullptr;
    const auto& bucket = results_[static_cast<size_t>(idx)];
    return bucket.empty() ? nullptr : &bucket[0];
  }

  // All captured results for this starting position, sorted by cost (best first).
  inline std::span<const Result> resultsAt(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    if (idx < 0) return {};
    if (!isValidStartIndex(idx)) return {};
    const auto& bucket = results_[idx];
    return {bucket.data(), bucket.size()};
  }

  inline const std::vector<CursorPos>& startPositions() const { return startPositions_; }

  inline const CursorPos& getGoalPos() const { return goalPos_; }

  // Returns the post-edit cursor for a result alternative at this start.
  inline CursorPos goalPosAt(
      int bufferLine, int bufferCol, std::size_t resultIndex = 0) const {
    if (!resultGoalPositions_.empty()) {
      int idx = resultIndexAt(bufferLine, bufferCol);
      if (idx >= 0 && isValidStartIndex(idx)) {
        const auto& bucket = resultGoalPositions_[idx];
        if (resultIndex < bucket.size()) return bucket[resultIndex];
      }
    }
    return goalPos_;
  }

  inline bool hasResultGoals() const { return !resultGoalPositions_.empty(); }
  inline bool isValidStartPosition(int bufferLine, int bufferCol) const {
    int idx = resultIndexAt(bufferLine, bufferCol);
    return idx >= 0 && isValidStartIndex(idx);
  }

  void restrictValidStartRegion(CharRange validStartRegion);

  // Flat result index for a buffer position, or -1 if out of range.
  inline int resultIndexAt(int bufferLine, int bufferCol) const {
    int editLine = bufferLine - beginLine_;
    if (editLine < 0 || editLine >= static_cast<int>(lineBaseIndex_.size()))
      return -1;
    int idx = lineBaseIndex_[editLine] + bufferCol;
    if (idx < 0 || idx >= static_cast<int>(results_.size()))
      return -1;
    return idx;
  }

private:
  TransformSearchStats stats_;
  CursorPos goalPos_;
  int beginLine_ = 0;
  std::vector<int> lineBaseIndex_;
  std::vector<CursorPos> indexToBufferPos_;
  std::vector<char> validStartByIndex_;
  std::vector<std::vector<CursorPos>> resultGoalPositions_;
  std::vector<CursorPos> startPositions_;

  inline bool isValidStartIndex(int idx) const {
    return validStartByIndex_[idx];
  }
  void rebuildStartPositions();

  friend std::ostream& operator<<(std::ostream& os, const TransformResult& transformResult);
};

std::ostream& operator<<(std::ostream& os, const TransformResult& transformResult);

}  // export
