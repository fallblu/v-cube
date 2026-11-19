#include "app/practice_list.hpp"

#include "app/page_parts.hpp"
#include "app/practice_html.hpp"
#include "app/session_panel.hpp"
#include "app/ui_text.hpp"
#include "app/widgets.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace vcube {

namespace {

constexpr int kKeyRole = Qt::UserRole;
constexpr int kCardRole = Qt::UserRole + 1; // the row is a card's

QString oneDecimal(double v) { return QString::number(v, 'f', 1); }

} // namespace

const PracticeTries &tabTries(const PracticeData &data, PracticeTab tab) {
  switch (tab) {
  case PracticeTab::Timed:
    return data.timed;
  case PracticeTab::Inspection:
    return data.inspection;
  case PracticeTab::Untimed:
  case PracticeTab::Learn:
    break;
  }
  return data.learn;
}

PracticeTries &tabTries(PracticeData &data, PracticeTab tab) {
  return const_cast<PracticeTries &>(tabTries(std::as_const(data), tab));
}

PracticeList::PracticeList(PracticeData &practice, const AppSettings &settings,
                           QWidget *parent)
    : QWidget(parent), data_(practice), settings_(settings),
      sittingStart_(unixNow()) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  summary_->setTextFormat(Qt::RichText);
  tree_ = new QTreeWidget(this);
  tree_->setObjectName("practiceList");
  tree_->setRootIsDecorated(false);
  tree_->setFocusPolicy(Qt::NoFocus);
  tree_->header()->setStretchLastSection(false);
  // The first column takes the room; fill() sets up the others.
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  help_ = helpLabel({}, this);
  clearButton_ = flatButton(tr("Clear progress…"), this);
  layout->addWidget(summary_);
  layout->addWidget(tree_, 1);
  layout->addWidget(help_);
  layout->addWidget(clearButton_, 0, Qt::AlignLeft);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *row) { rowActivated(row); });
  connect(clearButton_, &QPushButton::clicked, this, &PracticeList::clear);
}

const PracticeTries &PracticeList::tries() const {
  return tabTries(std::as_const(data_), view_.tab);
}

QString PracticeList::clock(std::int64_t ms) const {
  return clockText(ms, settings_.decimals);
}

void PracticeList::fill(const View &view, const std::vector<Row> &rows) {
  view_ = view;
  QSignalBlocker block(tree_);
  tree_->clear();
  QStringList headers;
  switch (view_.tab) {
  case PracticeTab::Learn:
    headers = {tr("Case"), tr("Due"), tr("Reviews"), tr("Lapses")};
    break;
  case PracticeTab::Inspection:
    headers = {view_.step, tr("Tries"), tr("Done"), tr("Looked"), tr("Extra")};
    break;
  case PracticeTab::Untimed:
    headers = {view_.step, tr("Solves"), tr("Moves"), tr("Extra")};
    break;
  case PracticeTab::Timed:
    headers = {view_.step, tr("Solves"), tr("Mean"), tr("Best"), tr("TPS")};
    break;
  }
  tree_->setColumnCount(static_cast<int>(headers.size()));
  tree_->setHeaderLabels(headers);
  for (int c = 1; c < tree_->columnCount(); ++c) {
    tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  const int size = settings_.thumbnailSize;
  tree_->setIconSize({size, size});
  QTreeWidgetItem *section = nullptr;
  QString shown;
  for (const Row &r : rows) {
    if (!r.section.isEmpty() && (r.section != shown || !section)) {
      shown = r.section;
      section = new QTreeWidgetItem(tree_);
      section->setText(0, shown);
      section->setFirstColumnSpanned(true);
      section->setFlags(Qt::ItemIsEnabled);
      QFont f = section->font(0);
      f.setBold(true);
      section->setFont(0, f);
    }
    auto *row = r.section.isEmpty() ? new QTreeWidgetItem(tree_)
                                    : new QTreeWidgetItem(section);
    row->setData(0, kKeyRole, qs(r.key));
    row->setData(0, kCardRole, view_.cards);
    row->setText(0, r.name);
    if (!r.icon.isNull()) {
      row->setIcon(0, r.icon);
    }
    for (int c = 1; c < tree_->columnCount(); ++c) {
      row->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  tree_->expandAll();
  help_->setText(view_.help);
  help_->setVisible(!view_.help.isEmpty());
  refresh();
}

void PracticeList::fillRow(QTreeWidgetItem *row, std::int64_t now,
                           std::int64_t dayEnd) {
  const QBrush normal = palette().brush(QPalette::Text);
  const QBrush muted = palette().brush(QPalette::PlaceholderText);
  const std::string key = row->data(0, kKeyRole).toString().toStdString();
  if (key.empty()) {
    return;
  }
  if (row->data(0, kCardRole).toBool()) {
    auto it = data_.cards.find(key);
    const SrsCard card = it == data_.cards.end() ? SrsCard{} : it->second.card;
    const bool fresh = card.state == CardState::New || isQueued(card);
    QString status;
    if (fresh) {
      status = tr("new");
    } else if (isDueNow(card, now, dayEnd, settings_.srs)) {
      status = tr("due");
    } else {
      status = dueText(card.due, now, dayEnd);
    }
    row->setText(1, status);
    row->setText(2, fresh ? kNoValue : QString::number(card.reps));
    row->setText(3, fresh ? kNoValue : QString::number(card.lapses));
    row->setForeground(0, fresh ? muted : normal);
    row->setForeground(1, status == tr("due") ? QBrush(kWarnColour) : muted);
    return;
  }
  const PracticeTries &all = tries();
  auto it = all.find(view_.tab == PracticeTab::Timed ? timedKey(view_.cube, key)
                                                     : key);
  const PracticeSummary s =
      it == all.end() ? PracticeSummary{} : summarize(it->second);
  row->setText(1, s.tries ? QString::number(s.tries) : kNoValue);
  switch (view_.tab) {
  case PracticeTab::Timed:
    row->setText(2, s.meanMs ? clock(static_cast<std::int64_t>(*s.meanMs))
                             : kNoValue);
    row->setText(3, s.bestMs ? clock(*s.bestMs) : kNoValue);
    row->setText(4, s.meanTps ? QString::number(*s.meanTps, 'f', 2) : kNoValue);
    break;
  case PracticeTab::Inspection:
    row->setText(2, s.tries
                        ? QString("%1%").arg(qRound(100.0 * s.solved / s.tries))
                        : kNoValue);
    row->setText(3,
                 s.meanInspectMs
                     ? tenthsText(static_cast<std::int64_t>(*s.meanInspectMs))
                     : kNoValue);
    row->setText(4, s.meanExtra ? "+" + oneDecimal(*s.meanExtra) : kNoValue);
    break;
  case PracticeTab::Untimed:
  case PracticeTab::Learn:
    row->setText(2, s.meanMoves ? oneDecimal(*s.meanMoves) : kNoValue);
    row->setText(3, s.meanExtra ? "+" + oneDecimal(*s.meanExtra) : kNoValue);
    break;
  }
  row->setForeground(0, s.tries ? normal : muted);
}

void PracticeList::refresh() {
  const std::int64_t now = unixNow();
  const std::int64_t dayEnd = localDayEnd();
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    QTreeWidgetItem *top = tree_->topLevelItem(i);
    fillRow(top, now, dayEnd);
    for (int j = 0; j < top->childCount(); ++j) {
      fillRow(top->child(j), now, dayEnd);
    }
  }
}

void PracticeList::showSummary(const std::vector<std::string> &pool,
                               const std::string &group) {
  QStringList lines;
  auto sitting = [this, &group]() {
    const PracticeTries &all = tries();
    auto it = all.find(group);
    return group.empty() || it == all.end()
               ? PracticeSummary{}
               : summarize(it->second, sittingStart_);
  };
  switch (view_.tab) {
  case PracticeTab::Learn:
    if (!pool.empty()) {
      PracticeWaiting w =
          practiceWaiting(pool, data_.cards, unixNow(), localDayStart(),
                          localDayEnd(), settings_.srs);
      lines << tr("<b>%1</b> due · %2 new left today · %3 of %4 learned")
                   .arg(w.due)
                   .arg(w.newLeft)
                   .arg(w.learned)
                   .arg(w.total);
    }
    break;
  case PracticeTab::Inspection: {
    const PracticeSummary s = sitting();
    if (s.tries) {
      QString line = tr("This sitting: %1, %2 done")
                         .arg(counted(s.tries, tr("try"), tr("tries")))
                         .arg(s.solved);
      if (s.meanInspectMs) {
        line +=
            ", " +
            tr("looked %1 on average")
                .arg(tenthsText(static_cast<std::int64_t>(*s.meanInspectMs)));
      }
      if (s.overTime > 0) {
        line += ", " + tr("%1 over the limit").arg(s.overTime);
      }
      lines << line;
    }
    break;
  }
  case PracticeTab::Untimed: {
    const PracticeSummary s = sitting();
    if (s.tries) {
      QString line = tr("This sitting: %1")
                         .arg(counted(s.tries, tr("solve"), tr("solves")));
      if (s.meanMoves) {
        line += ", " + tr("%1 moves on average").arg(oneDecimal(*s.meanMoves));
      }
      if (s.meanExtra) {
        line += " (+" + oneDecimal(*s.meanExtra) + ")";
      }
      lines << line;
    }
    break;
  }
  case PracticeTab::Timed: {
    const PracticeSummary s = sitting();
    if (s.tries) {
      QString line = tr("This sitting: %1")
                         .arg(counted(s.tries, tr("solve"), tr("solves")));
      if (s.meanMs) {
        line += ", " +
                tr("mean %1").arg(clock(static_cast<std::int64_t>(*s.meanMs)));
      }
      if (s.meanTps) {
        line += ", " + tr("%1 TPS").arg(QString::number(*s.meanTps, 'f', 2));
      }
      lines << line;
    }
    break;
  }
  }
  summary_->setText(lines.join("<br>"));
  summary_->setVisible(!lines.isEmpty());
  switch (view_.tab) {
  case PracticeTab::Learn:
    clearButton_->setText(tr("Clear the cards…"));
    break;
  case PracticeTab::Inspection:
    clearButton_->setText(tr("Clear these tries…"));
    break;
  case PracticeTab::Untimed:
    clearButton_->setText(tr("Clear these solves…"));
    break;
  case PracticeTab::Timed:
    clearButton_->setText(tr("Clear these times…"));
    break;
  }
}

void PracticeList::rowActivated(QTreeWidgetItem *row) {
  const std::string key = row->data(0, kKeyRole).toString().toStdString();
  if (key.empty() || (busy_ && busy_())) {
    return;
  }
  emit activated(key, row->data(0, kCardRole).toBool());
}

void PracticeList::clear() {
  if (busy_ && busy_()) {
    return;
  }
  QString what;
  switch (view_.tab) {
  case PracticeTab::Learn:
    what = tr("Forget every %1 card? The cases start as new again (the "
              "solutions you chose for them are kept).")
               .arg(view_.step);
    break;
  case PracticeTab::Inspection:
    what = tr("Delete every Inspection try of %1?").arg(view_.step);
    break;
  case PracticeTab::Untimed:
    what = tr("Delete every Untimed solve of %1?").arg(view_.step);
    break;
  case PracticeTab::Timed:
    what =
        tr("Delete every timed %1 attempt on the %2 cube?")
            .arg(view_.step, view_.cube == CubeKind::Physical ? tr("physical")
                                                              : tr("virtual"));
    break;
  }
  if (QMessageBox::question(this, "v-cube", what) != QMessageBox::Yes) {
    return;
  }
  switch (view_.tab) {
  case PracticeTab::Learn:
    data_.cards.clear();
    break;
  case PracticeTab::Inspection:
    data_.inspection.clear();
    break;
  case PracticeTab::Untimed:
    data_.learn.clear();
    break;
  case PracticeTab::Timed:
    clearTimed(data_.timed, view_.cube);
    break;
  }
  refresh();
  emit cleared();
}

} // namespace vcube
