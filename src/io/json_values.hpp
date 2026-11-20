#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "io/qt_text.hpp"

#include <cstdint>
#include <map>
#include <string>
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

// A map of values by key as a JSON object of their JSON, each built again
// only when its value changed since the last time: recording an attempt
// at one case makes that case's JSON again, not every case's. As with
// ItemJsonCache (see session_store.hpp), what it returns shares its data
// with what it keeps, so handing it to the writer's thread copies nothing,
// and values are compared whole.
template <typename Value> class KeyedJsonCache {
public:
  template <typename ToJson>
  QJsonObject object(const std::map<std::string, Value> &values,
                     ToJson toJson) {
    for (auto it = kept_.begin(); it != kept_.end();) {
      if (values.contains(it->first)) {
        ++it;
      } else {
        json_.remove(qs(it->first));
        it = kept_.erase(it);
      }
    }
    for (const auto &[key, value] : values) {
      auto it = kept_.find(key);
      if (it == kept_.end()) {
        kept_.emplace(key, value);
      } else if (it->second == value) {
        continue;
      } else {
        it->second = value;
      }
      json_.insert(qs(key), toJson(value));
    }
    return json_;
  }

private:
  std::map<std::string, Value> kept_;
  QJsonObject json_;
};

} // namespace vcube
