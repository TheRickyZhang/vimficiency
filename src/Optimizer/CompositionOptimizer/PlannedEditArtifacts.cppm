export module vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;

import std.compat;
export import vf.Optimizer.CompositionOptimizer.JoinPlan;
export import vf.Keyboard.Config;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.TransformOptimizer.TransformOptimizer;
export import vf.Types.BracketFlags;
export import vf.Types.Lines;
export import vf.Types.QuoteFlags;

export {

// Per-edit tracking of valid text object entry points.
// For each column on the edit's line, tracks which quote/bracket types
// can be used as text objects to reach the edit region from that column.
struct BracketQuoteContext {
  // Entries inside a matching pair are valid. The first pair is also reachable from its left.
  std::vector<QuoteFlags> validQuoteMask;  // Indexed by column

  // Opening-bracket lookup ignores quotes, even when the closing search skips a quoted bracket.
  std::vector<BracketFlags> validBracketMask;  // Indexed by column

  // Whether to use around (true) or inner (false) for each delimiter type
  // Set based on whether edit region includes delimiters
  QuoteFlags useAroundQuote;
  BracketFlags useAroundBracket;

  // Line this context applies to (-1 if not applicable, e.g., pure insertion)
  int line = -1;

  inline bool hasAnyValid() const {
    for (const auto& mask : validQuoteMask) {
      if (mask.seen('"') || mask.seen('\'') || mask.seen('`')) return true;
    }
    for (const auto& mask : validBracketMask) {
      if (mask.seen('(') || mask.seen('[') || mask.seen('{') || mask.seen('<')) return true;
    }
    return false;
  }

  inline char quoteModifier(char q) const {
    return useAroundQuote.seen(q) ? 'a' : 'i';
  }
  inline char bracketModifier(char b) const {
    return useAroundBracket.seen(b) ? 'a' : 'i';
  }
};

TransformResult computeTransformResultForDiff(
    const DiffState& diff,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored = nullptr);

BracketQuoteContext computeTextObjectContextForDiff(
    const DiffState& diff,
    const Lines& preEditLines, bool hasHiddenLinesAbove = false);

std::optional<JoinPlan> computeJoinPlanForDiff(
    const DiffState& diff,
    const Lines& preEditLines,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored = nullptr);

}  // export
