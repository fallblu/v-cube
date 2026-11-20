#pragma once

#include "app/colours.hpp"
#include "cube/move.hpp"
#include "io/qt_text.hpp"
#include "train/solve_review.hpp"
#include "train/srs.hpp"
#include "train/stats.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QString>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vcube {

// Small pieces of text and colour every page words the same way.

// tr() for a file that isn't a QObject's: its text is translated in one
// context, "vcube::" and the class or part of the app it belongs to. The
// file names it once:
//   constexpr char kTrContext[] = "vcube::LearnPage";
//   constexpr auto tr = trIn<kTrContext>;
template <const char *Context> QString trIn(const char *text) {
  return QCoreApplication::translate(Context, text);
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

// Rich text in the colour of secondary text.
inline QString mutedHtml(const QString &text) {
  return coloured(roleColour(Role::Muted), text);
}

// A time as the timer shows it, with `decimals`: "12.34".
inline QString timeText(StatValue ms, int decimals = 2) {
  return qs(formatTime(ms, decimals));
}

// What a table shows where there is no value.
inline const QString kNoValue = QStringLiteral("-");

// An alg or a result that passes, passes with a remark, or fails.
inline const StatusColour kPassColour{Status::Good};
inline const StatusColour kWarnColour{Status::Warning};
inline const RoleColour kFailColour{Role::Fail};

// The mark on a value over its target (a slow time, moves over the
// optimum), so that colour isn't the only sign of it.
inline const QString kOverMark = QStringLiteral("▲");

// Rich text for a value over its target: marked and in the warning colour,
// or bold and in the fail colour as well when it is far over.
inline QString overMarked(const QString &text, bool far = false) {
  const QString marked = kOverMark + "&nbsp;" + text;
  return far ? coloured(kFailColour, "<b>" + marked + "</b>")
             : coloured(kWarnColour, marked);
}

// A length of time, rounded the one way every page words it: minutes
// rounded up, hours and days to the nearest: "<1 min", "2 min", "3 h",
// "4 d".
inline QString durationText(std::int64_t seconds) {
  if (seconds < 60) {
    return QObject::tr("<1 min");
  }
  if (seconds < 3600) {
    return QObject::tr("%1 min").arg((seconds + 59) / 60);
  }
  if (seconds < kSecondsPerDay) {
    return QObject::tr("%1 h").arg((seconds + 1799) / 3600);
  }
  return QObject::tr("%1 d").arg((seconds + kSecondsPerDay / 2) /
                                 kSecondsPerDay);
}

// When a case is due, as a table cell: "-" for none or a new card, "now",
// "today" for a review due before `dayEnd` (reviews are due all day), or
// how long until then (durationText()).
inline QString dueCell(const CaseProgress *p, std::int64_t now,
                       std::int64_t dayEnd) {
  if (!p || p->card.state == CardState::New) {
    return QStringLiteral("-");
  }
  const std::int64_t in = p->card.due - now;
  if (in <= 0) {
    return QObject::tr("now");
  }
  if (p->card.state == CardState::Review && p->card.due < dayEnd) {
    return QObject::tr("today");
  }
  return durationText(in);
}

// When something is due, in a sentence: "now", "in 2 min" or "in 3 h"
// before `dayEnd`, then "tomorrow" or "in 4 d".
inline QString dueSentence(std::int64_t due, std::int64_t now,
                           std::int64_t dayEnd) {
  const std::int64_t in = due - now;
  if (in <= 0) {
    return QObject::tr("now");
  }
  if (due < dayEnd) {
    return QObject::tr("in %1").arg(durationText(in));
  }
  const std::int64_t days = (due - dayEnd) / kSecondsPerDay + 1;
  return days == 1 ? QObject::tr("tomorrow") : QObject::tr("in %1 d").arg(days);
}

// "1 case without a passing alg is skipped." and the plural.
inline QString skippedText(int cases) {
  return cases == 1 ? QObject::tr("1 case without a passing alg is skipped.")
                    : QObject::tr("%1 cases without a passing alg are skipped.")
                          .arg(cases);
}

// Seconds with two decimals, truncated to hundredths as the timer shows
// times, or "-".
inline QString secondsText(std::optional<double> ms) {
  return ms ? QString::number(std::floor(*ms / 10.0) / 100.0, 'f', 2)
            : QString("-");
}

} // namespace vcube
