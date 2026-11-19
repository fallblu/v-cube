#pragma once

#include "app/colours.hpp"
#include "app/ui_text.hpp"
#include "cube/move.hpp"
#include "train/solve_review.hpp"

#include <QDate>
#include <QDateTime>
#include <QLocale>
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

// `one` for 1 and `many` otherwise, with %n replaced by `n`: "1 alg
// hidden", "3 algs hidden". No plural translation is loaded, so tr()'s %n
// alone would show "alg(s)".
inline QString plural(int n, const QString &one, const QString &many) {
  QString s = n == 1 ? one : many;
  return s.replace(QLatin1String("%n"), QString::number(n));
}

// Dates, everywhere in the locale's short format.
// A day: "21/11/2026".
inline QString dateText(const QDate &day) {
  return QLocale().toString(day, QLocale::ShortFormat);
}
// A day and a time (to the minute) from Unix seconds: "21/11/2026 13:34",
// or "" for none.
inline QString dateTimeText(std::int64_t unixSeconds) {
  if (unixSeconds <= 0) {
    return {};
  }
  return QLocale().toString(QDateTime::fromSecsSinceEpoch(unixSeconds),
                            QLocale::ShortFormat);
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
