export module vf.Optimizer.Result;

import std.compat;
export import vf.Types.Sequence;

export {

struct Result {
  Result() = default;
  inline Result(Sequence seq, double c) : sequence_(std::move(seq)), keyCost_(c) {}
  inline Result(const std::string& s, double c) : sequence_(s), keyCost_(c) {}
  inline Result(std::string&& s, double c) : sequence_(std::move(s)), keyCost_(c) {}

  inline const Sequence& getSequence() const { return sequence_; }
  inline double getCost() const { return keyCost_; }

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
};

}  // export
