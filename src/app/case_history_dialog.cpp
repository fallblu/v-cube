#include "app/case_history_dialog.hpp"

#include "app/case_page.hpp"
#include "app/widgets.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::CaseHistory", text);
}

QString dateText(std::int64_t t) {
  return t <= 1 ? QString("-")
                : QDateTime::fromSecsSinceEpoch(t).toString("yyyy-MM-dd hh:mm");
}

QString stateText(CardState s) {
  switch (s) {
  case CardState::New:
    return tr("new");
  case CardState::Learning:
    return tr("learning");
  case CardState::Review:
    return tr("in review");
  case CardState::Relearning:
    return tr("relearning");
  }
  return {};
}

} // namespace

void showCaseHistory(QWidget *parent, const QString &caseName,
                     const CaseProgress *card,
                     const std::vector<Attempt> &attempts, bool familyCard,
                     const SrsConfig &config) {
  QDialog dialog(parent);
  dialog.setWindowTitle(tr("History of %1").arg(caseName));
  auto *layout = new QVBoxLayout(&dialog);
  QString info;
  if (card && card->card.state != CardState::New) {
    const SrsCard &k = card->card;
    info = tr("<p><b>%1</b>, %2. Introduced %3%4. Reviews %5, lapses %6, "
              "ease %7%, interval %8 d, due %9.</p>")
               .arg(caseName.toHtmlEscaped(), stateText(k.state),
                    dateText(k.introduced),
                    k.marked ? tr(" (marked as learned)") : QString())
               .arg(k.reps)
               .arg(k.lapses)
               .arg(std::lround(k.ease * 100))
               .arg(k.intervalDays, 0, 'f', 1)
               .arg(k.state == CardState::Review
                        ? dateText(k.due)
                        : dueText(card, unixNow(), localDayEnd()));
    if (k.suspended) {
      info += "<p>" + tr("Suspended: left out of Learn.") + "</p>";
    }
    if (isLeech(k, config)) {
      info += "<p style='color:#ff5555'>" +
              tr("A leech: forgotten %1 times. A different alg, a mnemonic "
                 "for recognising it or suspending it for a while may help.")
                  .arg(k.lapses)
                  .toHtmlEscaped() +
              "</p>";
    }
    if (familyCard) {
      info += "<p>" +
              tr("The card is shared by the case's family: its Learn "
                 "attempts are the family's, its drill attempts this case's.")
                  .toHtmlEscaped() +
              "</p>";
    }
  } else {
    info = "<p>" + tr("Not started in Learn.").toHtmlEscaped() + "</p>";
  }
  auto *label = new QLabel(info, &dialog);
  label->setWordWrap(true);
  layout->addWidget(label);

  auto *table = new QTableWidget(&dialog);
  table->setColumnCount(6);
  table->setHorizontalHeaderLabels({tr("When"), tr("Mode"), tr("Result"),
                                    tr("Time"), tr("Recognition"),
                                    tr("Execution")});
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  // The date and the mode on one line each, the times aligned.
  table->setWordWrap(false);
  for (int col : {0, 1}) {
    table->horizontalHeader()->setSectionResizeMode(
        col, QHeaderView::ResizeToContents);
  }
  table->setRowCount(static_cast<int>(attempts.size()));
  int row = 0;
  for (auto a = attempts.rbegin(); a != attempts.rend(); ++a, ++row) {
    bool learn = a->mode == AttemptMode::Learn;
    QString mode = learn ? (a->recognitionOnly ? tr("Recognise") : tr("Learn"))
                   : a->mode == AttemptMode::VirtualDrill
                       ? tr("Drill (virtual)")
                       : tr("Drill (physical)");
    QString result = learn ? (a->failed ? tr("Again") : tr("passed"))
                           : (a->failed ? tr("DNF") : tr("ok"));
    auto ms = [](std::int64_t t) {
      return t > 0 ? secondsText(static_cast<double>(t)) : QString("-");
    };
    QStringList cells = {dateText(a->timestamp),
                         mode,
                         result,
                         ms(a->timeMs),
                         ms(a->recognitionMs),
                         ms(a->executionMs)};
    for (int col = 0; col < 6; ++col) {
      auto *item = new QTableWidgetItem(cells[col]);
      if (col >= 3) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      table->setItem(row, col, item);
    }
  }
  layout->addWidget(table, 1);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  layout->addWidget(buttons);
  dialog.resize(640, 480);
  dialog.exec();
}

} // namespace vcube
