#pragma once

#include "app/colours.hpp"

#include <QString>

#include <string>
#include <string_view>

namespace vcube {

// Small pieces of text and colour every page words the same way.

inline QString qs(std::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// "1 alg", "3 algs".
inline QString counted(int n, const QString &one, const QString &many) {
  return QString("%1 %2").arg(n).arg(n == 1 ? one : many);
}

// What a table shows where there is no value.
inline const QString kNoValue = QStringLiteral("-");

// An alg or a result that passes, passes with a remark, or fails.
inline const StatusColour kPassColour{Status::Good};
inline const StatusColour kWarnColour{Status::Warning};
inline const QColor kFailColour(0xe6, 0x66, 0x66);

} // namespace vcube
