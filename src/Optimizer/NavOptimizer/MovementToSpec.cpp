module vf.Optimizer.NavOptimizer.MovementToSpec;

import std.compat;

using namespace std;

// =============================================================================
// Motion Operation Specs - tables for NavOptimizer
// =============================================================================
// KSId references for bank lookup during A* search

namespace Movement {
using VimCore::WordMotionTarget;

const vector<WordMovementSpec> WORD_MOTIONS = {
    {KSId::w,  WordMotionTarget::NextBegin, false},
    {KSId::W,  WordMotionTarget::NextBegin, true},
    {KSId::b,  WordMotionTarget::PreviousBegin, false},
    {KSId::B,  WordMotionTarget::PreviousBegin, true},
    {KSId::e,  WordMotionTarget::NextEnd, false},
    {KSId::E,  WordMotionTarget::NextEnd, true},
    {KSId::ge, WordMotionTarget::PreviousEnd, false},
    {KSId::gE, WordMotionTarget::PreviousEnd, true},
};

const vector<WordMovementSpec> FORWARD_WORD_MOVEMENTS = {
    {KSId::w, WordMotionTarget::NextBegin, false},
    {KSId::W, WordMotionTarget::NextBegin, true},
    {KSId::e, WordMotionTarget::NextEnd, false},
    {KSId::E, WordMotionTarget::NextEnd, true},
};

const vector<WordMovementSpec> BACKWARD_WORD_MOVEMENTS = {
    {KSId::b, WordMotionTarget::PreviousBegin, false},
    {KSId::B, WordMotionTarget::PreviousBegin, true},
    {KSId::ge, WordMotionTarget::PreviousEnd, false},
    {KSId::gE, WordMotionTarget::PreviousEnd, true},
};

const vector<ParagraphMovementSpecNoDir> FORWARD_PARAGRAPH_MOVEMENTS = {
    {KSId::RBrace},
};
const vector<ParagraphMovementSpecNoDir> BACKWARD_PARAGRAPH_MOVEMENTS = {
    {KSId::LBrace},
};

const vector<SentenceMovementSpecNoDir> FORWARD_SENTENCE_MOVEMENTS = {
    {KSId::RParen},
};
const vector<SentenceMovementSpecNoDir> BACKWARD_SENTENCE_MOVEMENTS = {
    {KSId::LParen},
};

// NOTE: <C-f> and <C-b> (full-page scroll) are NOT included because they depend on
// viewport state that we don't track. These commands scroll the window, and when the
// buffer is smaller than the window or cursor is near buffer edges, they may not move
// the cursor at all (nothing to scroll). To support them properly, we would need to:
// 1. Track viewport state (top visible line, window height)
// 2. Simulate actual scroll behavior (viewport movement + cursor adjustment)
// 3. Handle edge cases where scrolling is not possible
// <C-d> and <C-u> are more predictable as they move by a fixed scroll amount.
const vector<ScrollMovementSpecNoDir> FORWARD_SCROLL_MOVEMENTS = {
    {KSId::CtrlD},
};
const vector<ScrollMovementSpecNoDir> BACKWARD_SCROLL_MOVEMENTS = {
    {KSId::CtrlU},
};

} // namespace Movement
