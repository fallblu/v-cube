#include "app/practice_html.hpp"

#include "app/colours.hpp"
#include "app/ui_text.hpp"
#include "train/move_count.hpp"
#include "train/stats.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::PracticePage", text);
}

QString gradeName(Grade g) {
  switch (g) {
  case Grade::Again:
    return QObject::tr("Again");
  case Grade::Hard:
    return QObject::tr("Hard");
  case Grade::Good:
    return QObject::tr("Good");
  case Grade::Easy:
    return QObject::tr("Easy");
  }
  return {};
}

// Learn's grade badge colours (gradeColour() in alg_html.cpp).
QColor gradeBadgeColour(Grade g) {
  switch (g) {
  case Grade::Again:
    return roleColour(Role::GradeAgain);
  case Grade::Hard:
    return roleColour(Role::GradeHard);
  case Grade::Good:
    return roleColour(Role::GradeGood);
  case Grade::Easy:
    return roleColour(Role::GradeEasy);
  }
  return roleColour(Role::GradeNone);
}

} // namespace

QString smallLine(const QString &text) {
  return text.isEmpty()
             ? QString()
             : "<br><span style='font-size:small'>" + text + "</span>";
}

QString clockText(std::int64_t ms, int decimals) {
  return qs(formatTime(StatValue(ms), decimals));
}

QString tenthsText(std::int64_t ms) {
  return QObject::tr("%1 s").arg(QString::number(ms / 1000.0, 'f', 1));
}

QString bestLine(const PracticeOutcome &o) {
  if (o.best.empty() && o.bestText.isEmpty()) {
    return {};
  }
  return (o.bestLabel.isEmpty() ? tr("Best") : o.bestLabel) + ": <b>" +
         (o.bestText.isEmpty() ? algText(o.best) : o.bestText) + "</b>";
}

QString gradeLine(Grade grade, std::int64_t due, std::int64_t now,
                  std::int64_t dayEnd) {
  // A badge in the grade's colour, as Learn shows it.
  const QString badge =
      QString("<span style=\"background-color: %1; color: %2;\">"
              "&nbsp;<b>%3</b>&nbsp;</span>")
          .arg(gradeBadgeColour(grade).name(), roleColour(Role::OnBadge).name(),
               gradeName(grade));
  return tr("%1 · it comes back %2").arg(badge, dueSentence(due, now, dayEnd));
}

QString timedLine(const TimedResult &r, int decimals) {
  QStringList parts;
  if (r.moves > 0) {
    parts << movesText(r.moves);
    if (r.ms > 0 && !r.dnf) {
      parts << tr("%1 TPS").arg(QString::number(
          r.moves * 1000.0 / static_cast<double>(r.ms), 'f', 2));
    }
  }
  if (r.dnf) {
    parts.prepend(coloured(kFailColour, tr("DNF")));
  } else if (r.meanBefore && r.ms > 0) {
    const double d = static_cast<double>(r.ms) - *r.meanBefore;
    const QString mean =
        clockText(static_cast<std::int64_t>(*r.meanBefore), decimals);
    const QString by =
        clockText(static_cast<std::int64_t>(std::abs(d)), decimals);
    parts << (d <= 0 ? coloured(kPassColour,
                                tr("%1 under your mean (%2)").arg(by, mean))
                     : tr("%1 over your mean (%2)").arg(by, mean));
  }
  return parts.join(" · ");
}

QString inspectionLine(std::int64_t lookedMs, std::int64_t limitMs) {
  if (lookedMs <= 0) {
    return {};
  }
  QString text = tr("looked %1").arg(tenthsText(lookedMs));
  if (limitMs > 0 && lookedMs > limitMs) {
    text = coloured(kFailColour,
                    tr("looked %1, over the %2 limit")
                        .arg(tenthsText(lookedMs), tenthsText(limitMs)));
  }
  return text;
}

QString inspectionClock(std::int64_t usedMs, std::int64_t limitMs, bool running,
                        const QColor &text) {
  QString shown;
  QColor colour = text;
  if (limitMs > 0 && usedMs <= limitMs) {
    // Counting down whole seconds, as an inspection timer does.
    shown = QString::number((limitMs - usedMs + 999) / 1000);
    if (limitMs - usedMs < 3000) {
      colour = QColor(kWarnColour);
    }
  } else if (limitMs > 0) {
    shown = "+" + QString::number((usedMs - limitMs) / 1000.0, 'f', 1);
    colour = QColor(kFailColour);
  } else {
    shown = QString::number(usedMs / 1000.0, 'f', 1);
  }
  if (!running) {
    shown = tr("Looked %1").arg(tenthsText(usedMs));
  }
  return QString("<span style='color:%1'>%2</span>").arg(colour.name(), shown);
}

QString inspectionReadyPrompt(std::int64_t limitMs) {
  return tr("Ready when you are") +
         smallLine(limitMs > 0
                       ? tr("Space shows a state and starts %1 s of "
                            "inspection: look with rotations, then do your "
                            "plan blind.")
                             .arg(limitMs / 1000)
                       : tr("Space shows a state and starts the inspection "
                            "clock: look with rotations, then do your plan "
                            "blind."));
}

QString verdictHtml(const PracticeVerdict &v, const QString &muted) {
  QStringList lines;
  if (v.waiting) {
    lines << tr("Working out the best solutions…");
  } else {
    for (const QString &line : {v.result, v.best, v.grade}) {
      if (!line.isEmpty()) {
        lines << line;
      }
    }
  }
  if (!v.note.isEmpty()) {
    lines << coloured(QColor(muted), v.note);
  }
  return lines.join("<br>");
}

QString splitsHtml(const std::vector<PracticeSplit> &parts, const Alg &moves,
                   const std::vector<std::int64_t> &moveMs, int decimals,
                   const QString &muted) {
  if (parts.empty() || moveMs.size() != moves.size() || moves.empty()) {
    return {};
  }
  QString html =
      "<p>" + tr("Where the time went:") +
      "</p><table align='center' cellspacing='0' cellpadding='3'><tr "
      "style='color:" +
      muted + "'><th align='left'>" + tr("Part") + "</th><th>" + tr("Time") +
      "</th><th>" + tr("Pause before") + "</th><th>" + tr("Moves") +
      "</th><th>" + tr("TPS") + "</th></tr>";
  std::size_t from = 0;
  std::int64_t at = 0; // when the part before ended
  auto row = [&](const QString &name, std::size_t end) {
    end = std::min(end, moves.size());
    if (end <= from) {
      return;
    }
    // The first turn of the part (rotations before it are looking).
    std::size_t first = from;
    while (first < end && isRotation(moves[first].type)) {
      ++first;
    }
    const std::int64_t ended = moveMs[end - 1];
    const std::int64_t began = first < end ? moveMs[first] : ended;
    const std::int64_t pause = std::max<std::int64_t>(0, began - at);
    const std::int64_t time = std::max<std::int64_t>(0, ended - at);
    Alg part(moves.begin() + static_cast<std::ptrdiff_t>(from),
             moves.begin() + static_cast<std::ptrdiff_t>(end));
    const int htm = htmCount(simplify(part));
    const std::int64_t turning = ended - began;
    html +=
        QString("<tr><td>%1</td><td align='right'>%2</td><td "
                "align='right'>%3</td><td align='right'>%4</td><td "
                "align='right'>%5</td></tr>")
            .arg(name, clockText(time, decimals), clockText(pause, decimals))
            .arg(htm)
            .arg(turning > 0 && htm > 1
                     ? QString::number(
                           htm * 1000.0 / static_cast<double>(turning), 'f', 1)
                     : kNoValue);
    from = end;
    at = ended;
  };
  for (const PracticeSplit &p : parts) {
    row(p.name, p.end);
  }
  row(tr("the rest"), moves.size());
  return html + "</table><p style='color:" + muted + "'>" +
         tr("A part's time runs from the end of the one before; the pause "
            "is the time before its first turn, and TPS is of the turning "
            "alone.") +
         "</p>";
}

QString efficiencyHtml(const PracticeOutcome &o) {
  QStringList eff;
  if (!o.verdict.isEmpty()) {
    eff << o.verdict;
  }
  if (const QString best = bestLine(o); !best.isEmpty()) {
    eff << best;
  }
  return eff.isEmpty() ? QString()
                       : "<p align='center'>" + eff.join("<br>") + "</p>";
}

} // namespace vcube
