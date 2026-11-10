#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace vcube {

// The attempt last deleted from a session, and where it was, so that the
// delete can be undone. It is for one session: forget() it when another
// is chosen or a new attempt is recorded.
template <typename T> class DeletedAttempt {
public:
  // Deletes attempt `index` of `list`, remembering it; false for an index
  // that isn't there.
  bool erase(std::vector<T> &list, std::size_t index) {
    if (index >= list.size()) {
      return false;
    }
    deleted_ = {index, list[index]};
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
  }
  // Puts the attempt back where it was (at the end if the list is shorter
  // now), giving its index; nullopt when there is none to put back.
  std::optional<std::size_t> restore(std::vector<T> &list) {
    if (!deleted_) {
      return std::nullopt;
    }
    std::size_t at = std::min(deleted_->first, list.size());
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(at),
                std::move(deleted_->second));
    deleted_.reset();
    return at;
  }
  bool has() const { return deleted_.has_value(); }
  void forget() { deleted_.reset(); }

private:
  std::optional<std::pair<std::size_t, T>> deleted_;
};

} // namespace vcube
