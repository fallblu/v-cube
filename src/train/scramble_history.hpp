#pragma once

#include <cstddef>
#include <deque>
#include <utility>

namespace vcube {

// The scrambles done, to go back to (Untimed on a physical cube): the last
// kCapacity of them, and how far back the one shown is. At the end
// (shown() is null) the scramble shown is a new one, not yet done.
template <typename T> class ScrambleHistory {
public:
  static constexpr std::size_t kCapacity = 100;

  void clear() {
    list_.clear();
    index_ = 0;
  }
  // Keeps a scramble that was done; the one shown is then a new one.
  void push(T scramble) {
    list_.push_back(std::move(scramble));
    if (list_.size() > kCapacity) {
      list_.pop_front();
    }
    index_ = list_.size();
  }
  // Back one scramble; false at the first.
  bool back() {
    if (index_ == 0) {
      return false;
    }
    --index_;
    return true;
  }
  // Forward one scramble; false at the end (the new scramble is shown).
  bool forward() {
    if (index_ >= list_.size()) {
      return false;
    }
    ++index_;
    return true;
  }
  // On to the new scramble.
  void toEnd() { index_ = list_.size(); }

  // The scramble gone back to, or null at the end.
  const T *shown() const {
    return index_ < list_.size() ? &list_[index_] : nullptr;
  }
  // How many scrambles back the one shown is (0 at the end).
  std::size_t stepsBack() const { return list_.size() - index_; }
  std::size_t size() const { return list_.size(); }

private:
  std::deque<T> list_;
  std::size_t index_ = 0;
};

} // namespace vcube
