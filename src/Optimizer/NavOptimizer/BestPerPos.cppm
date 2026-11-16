export module vf.Optimizer.NavOptimizer.BestPerPos;

import std.compat;
export import vf.Types.Pos;

export {

// The `cap` cheapest entries added per position. A full position still accepts
// a cheaper entry in place of its worst, since entries do not arrive in cost
// order.
template <typename T>
class BestPerPos {
  struct Entry {
    double cost;
    T value;
  };

  static constexpr int UNLIMITED = std::numeric_limits<int>::max();

  int cap;
  int maxEntries = UNLIMITED;
  int totalPositions = UNLIMITED;
  std::map<Pos, std::vector<Entry>> byPos;
  int count = 0;
  int full = 0;

public:
  explicit BestPerPos(int cap) : cap(cap) {}

  // Collection over `totalPositions` positions that is complete() once it
  // holds `maxEntries` or every position is full.
  BestPerPos(int cap, int maxEntries, int totalPositions)
      : cap(cap), maxEntries(maxEntries), totalPositions(totalPositions) {}

  void add(Pos pos, double cost, T&& value) {
    std::vector<Entry>& bucket = byPos[pos];
    if (std::ssize(bucket) < cap) {
      bucket.emplace_back(cost, std::move(value));
      count++;
      if (std::ssize(bucket) == cap) full++;
      return;
    }
    auto worst = std::ranges::max_element(bucket, {}, &Entry::cost);
    if (cost < worst->cost) *worst = Entry{cost, std::move(value)};
  }

  int size() const { return count; }
  int positions() const { return std::ssize(byPos); }
  int getTotalPositions() const { return totalPositions; }
  bool complete() const { return count >= maxEntries || full == totalPositions; }

  // Every value, in position order.
  std::vector<T> take() {
    std::vector<T> values;
    values.reserve(count);
    for (auto& [pos, bucket] : byPos) {
      for (Entry& entry : bucket) values.push_back(std::move(entry.value));
    }
    return values;
  }
};

}  // export
