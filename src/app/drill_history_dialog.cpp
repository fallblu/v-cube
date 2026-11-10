#include "app/drill_history_dialog.hpp"

#include "app/case_page.hpp"
#include "app/widgets.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::DrillHistory", text);
}

QString dayText(std::int64_t day) {
  return QDate(1970, 1, 1).addDays(day).toString("yyyy-MM-dd");
}

} // namespace

void showDrillHistory(QWidget *parent, const QString &caseName,
                      const CaseProgress *progress, AttemptMode mode,
                      int decimals) {
  QDialog dialog(parent);
  const bool virt = mode == AttemptMode::VirtualDrill;
  dialog.setWindowTitle(tr("Drill history of %1").arg(caseName));
  auto *layout = new QVBoxLayout(&dialog);

  std::vector<Solve> attempts;
  std::vector<DayRollup> days;
  if (progress) {
    for (const Attempt &a : progress->attempts) {
      if (a.mode == mode) {
        Solve s;
        s.timeMs = a.timeMs;
        s.penalty = a.failed ? Penalty::Dnf : Penalty::None;
        s.timestamp = a.timestamp;
        attempts.push_back(s);
      }
    }
    days = progress->days(mode);
  }
  int total = 0, fails = 0;
  for (const DayRollup &d : days) {
    total += d.count;
    fails += d.fails;
  }
  auto seconds = [decimals](std::optional<double> ms) {
    if (!ms) {
      return QString("-");
    }
    Solve s;
    s.timeMs = std::llround(*ms);
    return qs(formatSolve(s, decimals));
  };
  QString info =
      (virt ? tr("<p><b>%1</b> on the virtual cube: %2 attempts in all, %3 "
                 "failed. Best %4, mean of the last 5 %5, median %6.</p>")
            : tr("<p><b>%1</b> on a physical cube: %2 attempts in all, %3 "
                 "failed. Best %4, mean of the last 5 %5, median %6.</p>"))
          .arg(caseName.toHtmlEscaped())
          .arg(total)
          .arg(fails)
          .arg(seconds(progress && progress->best(mode)
                           ? std::optional<double>(*progress->best(mode))
                           : std::nullopt),
               seconds(progress ? progress->recentMean(mode) : std::nullopt),
               seconds(progress ? progress->recentMedian(mode) : std::nullopt));
  auto *label = new QLabel(info, &dialog);
  label->setWordWrap(true);
  layout->addWidget(label);

  layout->addWidget(new QLabel(
      tr("The last %1 attempts:").arg(CaseProgress::kMaxAttempts), &dialog));
  auto *chart = new TrendChart(&dialog);
  chart->setSettingsKey("drill/historyChart");
  chart->setEmptyText(tr("Two attempts are needed to chart them"));
  chart->setSolves(attempts);
  chart->setMinimumHeight(160);
  layout->addWidget(chart, 1);

  layout->addWidget(new QLabel(tr("Day by day (the mean of each day's good "
                                  "attempts):"),
                               &dialog));
  std::vector<Solve> means;
  TrendChart::Series bests{tr("daily best"), QColor(0x3d, 0xd6, 0x6b), {}};
  for (const DayRollup &d : days) {
    Solve s;
    auto mean = d.meanMs();
    s.timeMs = mean ? std::llround(*mean) : 0;
    s.penalty = mean ? Penalty::None : Penalty::Dnf;
    means.push_back(s);
    bests.values.push_back(d.bestMs > 0 ? StatValue(d.bestMs) : std::nullopt);
  }
  auto *dayChart = new TrendChart(&dialog);
  dayChart->setSettingsKey("drill/dayChart");
  dayChart->setEmptyText(tr("Days will be charted once there are two"));
  dayChart->setSolves(means);
  dayChart->setSeries({bests});
  dayChart->setMinimumHeight(140);
  layout->addWidget(dayChart, 1);

  auto *table = new QTableWidget(static_cast<int>(days.size()), 5, &dialog);
  table->setHorizontalHeaderLabels(
      {tr("Day"), tr("Attempts"), tr("Failed"), tr("Mean"), tr("Best")});
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  for (std::size_t i = 0; i < days.size(); ++i) {
    const DayRollup &d = days[days.size() - 1 - i]; // newest first
    int row = static_cast<int>(i);
    QStringList cells{
        dayText(d.day), QString::number(d.count), QString::number(d.fails),
        seconds(d.meanMs()),
        seconds(d.bestMs > 0 ? std::optional<double>(d.bestMs) : std::nullopt)};
    for (int c = 0; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      item->setTextAlignment(c == 0 ? Qt::AlignLeft | Qt::AlignVCenter
                                    : Qt::AlignRight | Qt::AlignVCenter);
      table->setItem(row, c, item);
    }
  }
  table->setMinimumHeight(140);
  layout->addWidget(table, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  layout->addWidget(buttons);
  dialog.resize(620, 680);
  dialog.exec();
}

} // namespace vcube
