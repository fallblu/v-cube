#pragma once

#include <cstddef>
#include <string>

namespace vcube {

// A move count typed in digit by digit, as Move Count has it on a physical
// cube: at most kMaxDigits digits and no leading zeros ("0" then "7" is 7).
// Backspace takes the last digit away and Esc clears it.
class TypedCount {
public:
  static constexpr std::size_t kMaxDigits = 3;

  // Adds a digit (0-9); false, and nothing changes, once there are
  // kMaxDigits.
  bool add(int digit) {
    if (text_.size() >= kMaxDigits || digit < 0 || digit > 9) {
      return false;
    }
    if (text_ == "0") {
      text_.clear();
    }
    text_ += static_cast<char>('0' + digit);
    return true;
  }
  void backspace() {
    if (!text_.empty()) {
      text_.pop_back();
    }
  }
  void clear() { text_.clear(); }

  bool empty() const { return text_.empty(); }
  // The digits typed, "" for none.
  const std::string &text() const { return text_; }
  // The count typed, 0 for none.
  int value() const { return text_.empty() ? 0 : std::stoi(text_); }

private:
  std::string text_;
};

} // namespace vcube
