export module vf.Optimizer.Result;

import std.compat;
export import vf.Types.Sequence;

export {

// How an edit sequence's outcome depends on the curswant it is entered with.
// Every edit writes curswant, so only a sequence's first command can read it.
enum class EntryCurswant : uint8_t {
  Ignored,   // valid from any cursor
  Followed,  // valid from any cursor; lands at min(entry curswant, lastCol)
  Required,  // valid only when entry curswant == col
};

struct Result {
  Result() = default;
  inline Result(Sequence seq, double c, EntryCurswant entryCurswant = EntryCurswant::Ignored)
      : sequence_(std::move(seq)), keyCost_(c), entryCurswant_(entryCurswant) {}

  inline const Sequence& getSequence() const { return sequence_; }
  inline double getCost() const { return keyCost_; }
  inline EntryCurswant getEntryCurswant() const { return entryCurswant_; }
  bool operator==(const Result&) const = default;

  inline std::string to_string() {
    std::ostringstream oss;
    oss << *this;
    return oss.str();
  }

  inline friend std::ostream& operator<<(std::ostream& os, const Result& r) {
    os << r.sequence_ << " " << r.keyCost_;
    return os;
  }

private:
  Sequence sequence_;
  double keyCost_ = 0;
  EntryCurswant entryCurswant_ = EntryCurswant::Ignored;
};

}  // export
