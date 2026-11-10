module vf.Types.Sequence;

import std.compat;
import vf.Interpreter.SequenceParser;
import vf.Utils.PrettyText;

// An implementation unit of vf.Types.Sequence that lives in Interpreter/: the
// operator belongs to Sequence's module, but its body needs the parser, which
// Types must not depend on.
std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  if (seq.empty()) return os;

  auto parsed = parseSequence(seq.view());
  if (!parsed) {
    os << VF::prettify(seq.str());
    return os;
  }
  std::vector<TaggedToken> tokens = *parsed;

  for (size_t i = 0; i < tokens.size(); i++) {
    os << VF::prettify(tokens[i].token);

    if (i + 1 < tokens.size()) {
      auto cur = tokens[i].kind;
      auto next = tokens[i + 1].kind;
      if (cur == TokenKind::Escape ||
          (cur == TokenKind::Change && next == TokenKind::TypedText)) {
        os << " ";
      }
    }
  }

  return os;
}
