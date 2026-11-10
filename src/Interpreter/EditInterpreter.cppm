export module vf.Interpreter.EditInterpreter;

import std.compat;
export import vf.Types.NavContext;
export import vf.Types.CharLineRange;
export import vf.Types.LineCharRange;
export import vf.Types.Mode;
export import vf.Types.CursorPos;
export import vf.Types.CharRange;
export import vf.Types.Lines;

export {

// Parsed edit (operator + motion/text-object, or single-key command)
class ParsedEdit {
  // 0 -> no count, OK since it is impossible for 0 to be a count.
  // important to distinguish since 1{action} sometimes != action!
  int count;

public:
  std::string_view edit;  // The edit string (e.g., "x", "dd", "ciw", "dfa")

  inline ParsedEdit(std::string_view e, int c = 0) : edit(e), count(c) {}

  inline bool hasCount() const { return count != 0; }
  inline int effectiveCount() const { return count ? count : 1; }
  inline int rawCount() const { return count; }
};

// Dot-repeat state. `base` is the command stripped of its count prefix
// (e.g. "de"); `count` is the count prefix (0 == uncounted). Empty `base`
// means "nothing to repeat" — `.` no-ops in that case. Owns its storage so a
// `ParsedEdit` view built via `asEdit()` is valid as long as the DotRepeat is.
struct DotRepeat {
  std::string base;
  int count = 0;

  inline bool empty() const { return base.empty(); }
  inline ParsedEdit asEdit() const { return ParsedEdit{base, count}; }
};

// Edit operations that modify the buffer.
// All functions modify lines and pos in place.
namespace Edit {

// Apply a single parsed edit. If lastEdit is non-null, '.' repeats the
// last buffer-modifying command stored there, and buffer-modifying commands
// update it. Callers that don't need dot repeat can omit the parameter.
// hasLinesBelow: when true, linewise deletions (dd, dj, dk) that remove the
// last lines leave pos.line past end instead of clamping. This models the
// edit-region context where the real buffer has lines below.
// leftColOffset/rightColOffset + hasLinesAbove/hasLinesBelow can be provided to
// make word text objects boundary-aware (used by TransformOptimizer replay).
void applyEdit(Lines& lines, CursorPos& pos, Mode& mode, const ParsedEdit& edit,
               DotRepeat* lastEdit = nullptr, bool hasLinesBelow = false,
               int leftColOffset = 0, int rightColOffset = 0,
               bool hasLinesAbove = false,
               // Vim's `did_ai` flag for the autoindent strip on `<Esc>`.
               // Set by `<CR>` to the length of autoindent inserted; cleared
               // by any user typing other than the autoindent itself or `<BS>`
               // that wipes it. Caller threads this across token applies so
               // multi-token sequences (`i<CR><Esc>`, `iX<CR><Esc>`) see
               // consistent state.
               int* pendingAutoindentLen = nullptr);

enum class EditParseErrorKind {
  MalformedSpecialKey,
};

struct EditParseError {
  EditParseErrorKind kind;
  size_t offset;
};

std::string formatEditParseError(const EditParseError& error);

// Parse an edit sequence string into individual ParsedEdit tokens.
// Handles operators (d, c) + motions/text objects, special keys (<Esc>, <CR>, etc.),
// and counts.
// IMPORTANT: Returned ParsedEdits contain string_views into seq - caller must ensure seq outlives usage.
std::expected<std::vector<ParsedEdit>, EditParseError> parseEdits(std::string_view seq);

} // namespace Edit

}  // export
