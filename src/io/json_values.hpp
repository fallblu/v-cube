#pragma once

#include <QJsonValue>

#include <cstdint>

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

} // namespace vcube
