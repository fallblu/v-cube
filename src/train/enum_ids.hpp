#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

namespace vcube {

// An enum's ids or names kept as a table in value order (the enum counts
// from 0), so that each is spelled once.

// The entry for `e`, or `fallback` for a value past the table's end.
template <typename T, std::size_t N, typename E>
constexpr T idOf(const std::array<T, N> &table, E e,
                 std::type_identity_t<T> fallback = {}) {
  auto i = static_cast<std::size_t>(e);
  return i < N ? table[i] : fallback;
}

// The value whose entry is `id`; nullopt for any other text.
template <typename E, std::size_t N>
constexpr std::optional<E> fromId(const std::array<std::string_view, N> &table,
                                  std::string_view id) {
  for (std::size_t i = 0; i < N; ++i) {
    if (table[i] == id) {
      return static_cast<E>(i);
    }
  }
  return std::nullopt;
}

} // namespace vcube
