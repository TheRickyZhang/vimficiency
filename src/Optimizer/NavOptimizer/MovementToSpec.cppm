export module vf.Optimizer.NavOptimizer.MovementToSpec;

import std.compat;
export import vf.Keyboard.KeyedSequence;
export import vf.VimCore.VimEndpointUtils;

export {

// =============================================================================
// Motion Operation Specs - tables for NavOptimizer
// =============================================================================
// Structured specs with operation parameters alongside pre-computed keys.
// Eliminates runtime map lookups during A* search.

namespace Movement {

// Word motions (w, b, e, ge, W, B, E, gE)
struct WordMovementSpec {
  KSId ksId;
  VimCore::WordMotionTarget target;
  bool big;
};
extern const std::vector<WordMovementSpec> WORD_MOTIONS;
extern const std::vector<WordMovementSpec> FORWARD_WORD_MOVEMENTS;
extern const std::vector<WordMovementSpec> BACKWARD_WORD_MOVEMENTS;

// Paragraph motions ({, }), split by direction for templated dispatch.
struct ParagraphMovementSpecNoDir {
  KSId ksId;
};
extern const std::vector<ParagraphMovementSpecNoDir> FORWARD_PARAGRAPH_MOVEMENTS;   // }
extern const std::vector<ParagraphMovementSpecNoDir> BACKWARD_PARAGRAPH_MOVEMENTS;  // {

// Sentence motions ((, )), split by direction for templated dispatch.
struct SentenceMovementSpecNoDir {
  KSId ksId;
};
extern const std::vector<SentenceMovementSpecNoDir> FORWARD_SENTENCE_MOVEMENTS;   // )
extern const std::vector<SentenceMovementSpecNoDir> BACKWARD_SENTENCE_MOVEMENTS;  // (

// Half-page scroll motions (<C-d>, <C-u>), split by direction for templated dispatch.
// NOTE: <C-f> and <C-b> are excluded - see MovementToSpec.cpp for details
struct ScrollMovementSpecNoDir {
  KSId ksId;
};
extern const std::vector<ScrollMovementSpecNoDir> FORWARD_SCROLL_MOVEMENTS;   // <C-d>
extern const std::vector<ScrollMovementSpecNoDir> BACKWARD_SCROLL_MOVEMENTS;  // <C-u>

} // namespace Movement

}  // export
