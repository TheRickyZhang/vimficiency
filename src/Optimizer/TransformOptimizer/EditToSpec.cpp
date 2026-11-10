module vf.Optimizer.TransformOptimizer.EditToSpec;

import std.compat;

using namespace std;

namespace Edit {
using VimCore::WordOperatorTarget;

const vector<WordEditSpec> FORWARD_WORD_EDITS = {
    {KSId::de, WordOperatorTarget::DeleteToWordEnd, false},
    {KSId::dw, WordOperatorTarget::DeleteToNextWord, false},
    {KSId::dE, WordOperatorTarget::DeleteToWordEnd, true},
    {KSId::dW, WordOperatorTarget::DeleteToNextWord, true},
};

const vector<WordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS = {
    {KSId::de, WordOperatorTarget::DeleteToWordEnd, false},
    {KSId::dE, WordOperatorTarget::DeleteToWordEnd, true},
};

const vector<WordEditSpec> BACKWARD_WORD_EDITS = {
    {KSId::db, WordOperatorTarget::DeleteBackToWordBegin, false},
    {KSId::dge, WordOperatorTarget::DeleteBackToWordEnd, false},
    {KSId::dB, WordOperatorTarget::DeleteBackToWordBegin, true},
    {KSId::dgE, WordOperatorTarget::DeleteBackToWordEnd, true},
};

const vector<WordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS = {
    {KSId::db, WordOperatorTarget::DeleteBackToWordBegin, false},
    {KSId::dB, WordOperatorTarget::DeleteBackToWordBegin, true},
};

const vector<WordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS = {
    {KSId::db, WordOperatorTarget::DeleteBackToWordBegin, false},
    {KSId::dge, WordOperatorTarget::DeleteBackToWordEnd, false},
    {KSId::dB, WordOperatorTarget::DeleteBackToWordBegin, true},
};

const vector<TextObjectEditSpec> TEXT_OBJECT_EDITS = {
    // Small word
    {KSId::diw, true, false},
    {KSId::daw, false, false},
    // bigWord
    {KSId::diW, true, true},
    {KSId::daW, false, true},
};

const vector<LineEditSpec> HALF_LINE_EDITS = {
    {KSId::D},
    {KSId::d0},
};

const vector<FullLineEditSpec> FULL_LINE_EDITS = {
    {KSId::dd},
};

const vector<ParagraphEditSpecNoDir> FORWARD_PARAGRAPH_EDITS = {
    {KSId::dRBrace},
};
const vector<ParagraphEditSpecNoDir> BACKWARD_PARAGRAPH_EDITS = {
    {KSId::dLBrace},
};

const vector<SentenceEditSpecNoDir> FORWARD_SENTENCE_EDITS = {
    {KSId::dRParen},
};
const vector<SentenceEditSpecNoDir> BACKWARD_SENTENCE_EDITS = {
    {KSId::dLParen},
};

} // namespace Edit
