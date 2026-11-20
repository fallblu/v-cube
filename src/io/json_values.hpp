#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace vcube {

// The largest time or timestamp taken from a file: far beyond any real one
// (in milliseconds, some 3000 years), and small enough that thousands of
// them can be added up.
inline constexpr std::int64_t kMaxFileNumber = 100'000'000'000'000;

// A time or timestamp from a data file or an import, which may hold
// anything: not a number gives 0, and the rest is kept within 0 and
// kMaxFileNumber (a double outside the range of the integer can't be
// converted to it).
inline std::int64_t jsonInt64(const QJsonValue &v) {
  const double d = v.toDouble();
  if (!(d > 0)) {
    return 0;
  }
  return d >= static_cast<double>(kMaxFileNumber)
             ? kMaxFileNumber
             : static_cast<std::int64_t>(d);
}

// fn(e) for each e of `range`, as a JSON array.
template <typename Range, typename Fn>
QJsonArray jsonArray(const Range &range, Fn fn) {
  QJsonArray a;
  for (const auto &e : range) {
    a.append(fn(e));
  }
  return a;
}

// fn(v) for each value of the array `list` holds (none when it holds
// something else), as a vector.
template <typename Fn> auto vectorOf(const QJsonValue &list, Fn fn) {
  std::vector<std::invoke_result_t<Fn, const QJsonValue &>> out;
  for (const QJsonValue &v : list.toArray()) {
    out.push_back(fn(v));
  }
  return out;
}

// Sets `key` only when `cond` holds, for the keys a file leaves out when
// they are false, 0 or empty.
inline void setIf(QJsonObject &o, const char *key, bool cond,
                  const QJsonValue &value) {
  if (cond) {
    o[key] = value;
  }
}

} // namespace vcube
