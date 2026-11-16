#pragma once

#include "app/colours.hpp"
#include "cube/move.hpp"
#include "train/solve_review.hpp"

#include <QObject>
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

// "1 move", "12 moves".
inline QString movesText(int n) {
  return n == 1 ? QObject::tr("1 move") : QObject::tr("%1 moves").arg(n);
}

// An alg as rich text.
inline QString algText(const Alg &alg) {
  return qs(toString(alg)).toHtmlEscaped();
}

// "FR", "BL".
inline QString slotText(Slot s) { return qs(slotName(s)); }

// Rich text in a colour.
inline QString coloured(const QColor &colour, const QString &text) {
  return QString("<span style='color:%1'>%2</span>").arg(colour.name(), text);
}

// What a table shows where there is no value.
inline const QString kNoValue = QStringLiteral("-");

// An alg or a result that passes, passes with a remark, or fails.
inline const StatusColour kPassColour{Status::Good};
inline const StatusColour kWarnColour{Status::Warning};
inline const QColor kFailColour(0xe6, 0x66, 0x66);

} // namespace vcube
