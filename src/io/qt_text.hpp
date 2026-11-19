#pragma once

#include <QString>

#include <string>
#include <string_view>

namespace vcube {

// UTF-8 text between the C++ libraries and Qt. One overload, so that a
// std::string, a string_view and a literal all reach it without a
// temporary.
inline QString qs(std::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
inline std::string ss(const QString &s) { return s.toStdString(); }

} // namespace vcube
