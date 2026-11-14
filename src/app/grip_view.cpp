#include "app/grip_view.hpp"
#include "app/ui_text.hpp"

#include "app/colours.hpp"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

enum Row { RowMove, RowRight, RowLeft, RowCount };

// "index flick", "wrist up" ... without the hand.
QString trickShort(const GripTrick &t) {
  QString s = qs(fingerId(t.finger));
  if (t.finger == Finger::Hands) {
    s = QObject::tr("both hands");
  }
  if (!t.how.empty()) {
    s += ' ' + qs(t.how);
  }
  return s;
}

QString cost(double c) { return QString::number(c, 'f', 2); }

} // namespace

GripView::GripView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  summary_->setTextFormat(Qt::RichText);
  summary_->setToolTip(
      tr("Each hand's grip is where its thumb sits, as the wrist turns it: "
         "home, up a quarter turn (the right thumb after R from home, the "
         "left thumb after L'), down, or far up or down (a strain)."));
  layout->addWidget(summary_);

  body_ = new QWidget(this);
  auto *bl = new QVBoxLayout(body_);
  bl->setContentsMargins(0, 0, 0, 0);
  lanes_ = new QTableWidget(RowCount, 0, body_);
  lanes_->setVerticalHeaderLabels(
      {tr("Move"), tr("Right hand"), tr("Left hand")});
  lanes_->horizontalHeader()->hide();
  lanes_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  lanes_->setSelectionBehavior(QAbstractItemView::SelectColumns);
  lanes_->setSelectionMode(QAbstractItemView::SingleSelection);
  lanes_->setFocusPolicy(Qt::ClickFocus);
  lanes_->setWordWrap(false);
  lanes_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  lanes_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  bl->addWidget(lanes_);

  auto *below = new QHBoxLayout;
  auto *optBox = new QVBoxLayout;
  optionsTitle_ = new QLabel(body_);
  options_ = new QListWidget(body_);
  options_->setFocusPolicy(Qt::ClickFocus);
  options_->setFixedHeight(options_->fontMetrics().height() * 5 + 8);
  optBox->addWidget(optionsTitle_);
  optBox->addWidget(options_);
  below->addLayout(optBox, 1);
  auto *buttons = new QVBoxLayout;
  reset_ = new QPushButton(tr("Reset picks"), body_);
  reset_->setToolTip(tr("Back to the best plan"));
  copy_ = new QPushButton(tr("Copy"), body_);
  copy_->setToolTip(tr("Copy the plan as text"));
  buttons->addStretch();
  buttons->addWidget(reset_);
  buttons->addWidget(copy_);
  below->addLayout(buttons);
  bl->addLayout(below);
  layout->addWidget(body_);

  connect(lanes_, &QTableWidget::itemSelectionChanged, this,
          &GripView::fillOptions);
  connect(options_, &QListWidget::itemClicked, this, &GripView::optionPicked);
  connect(options_, &QListWidget::itemActivated, this, &GripView::optionPicked);
  connect(reset_, &QPushButton::clicked, this, [this] {
    choice_ = {};
    replan();
  });
  connect(copy_, &QPushButton::clicked, this, [this] {
    if (model_) {
      QApplication::clipboard()->setText(
          text_ + "\n" + qs(gripPlanText(result_.plan, *model_)));
    }
  });
  clear();
}

void GripView::clear(const QString &why) {
  text_.clear();
  alg_.clear();
  model_.reset();
  choice_ = {};
  result_ = {};
  summary_->setText(why);
  summary_->setVisible(!why.isEmpty());
  body_->hide();
}

void GripView::setAlg(const QString &alg, const GripModel *model,
                      const QString &why) {
  auto parsed = parseAlg(alg.toStdString());
  if (!model || alg.trimmed().isEmpty() || !parsed || parsed.alg.empty()) {
    clear(why);
    return;
  }
  bool same = alg == text_ && model_ && *model_ == *model;
  if (same) {
    return; // keep the picks
  }
  text_ = alg;
  alg_ = parsed.alg;
  model_ = *model;
  choice_ = {};
  replan();
}

void GripView::replan() {
  if (!model_) {
    return;
  }
  result_ = planGrips(alg_, *model_, choice_, true);
  const GripPlan &p = result_.plan;
  double best = gripCost(alg_, *model_);
  QString start =
      tr("right thumb %1, left thumb %2")
          .arg(qs(gripName(p.start.right)), qs(gripName(p.start.left)));
  QString text = tr("<b>Start:</b> %1 &nbsp;·&nbsp; <b>Cost</b> %2 (tricks "
                    "%3, regrips and strain %4) &nbsp;·&nbsp; %n regrip(s)",
                    nullptr, p.regrips)
                     .arg(start, cost(p.total), cost(p.tricks), cost(p.extras));
  if (p.untricked > 0) {
    text += tr(" &nbsp;·&nbsp; %n move(s) with no trick in the profile",
               nullptr, p.untricked);
  }
  bool picked = std::any_of(choice_.forced.begin(), choice_.forced.end(),
                            [](int f) { return f >= 0; });
  if (picked) {
    text += tr(" &nbsp;·&nbsp; your picks: +%1 over the best")
                .arg(cost(p.total - best));
  }
  summary_->setText(text);
  summary_->show();
  reset_->setEnabled(picked);
  body_->show();
  int col = lanes_->currentColumn();
  fillLanes();
  if (col >= 0 && col < lanes_->columnCount()) {
    lanes_->selectColumn(col);
  } else {
    fillOptions();
  }
}

void GripView::fillLanes() {
  QSignalBlocker block(lanes_);
  const GripPlan &p = result_.plan;
  const auto n = static_cast<int>(p.steps.size());
  lanes_->clearContents();
  lanes_->setColumnCount(n);
  QColor regripBg = statusColour(Status::Warning);
  regripBg.setAlpha(60);
  QColor pickedBg = palette().color(QPalette::Highlight);
  pickedBg.setAlpha(40);
  for (int i = 0; i < n; ++i) {
    const GripStep &st = p.steps[static_cast<std::size_t>(i)];
    const GripTrick *t =
        st.trick >= 0 ? &model_->tricks[static_cast<std::size_t>(st.trick)]
                      : nullptr;
    bool picked = static_cast<std::size_t>(i) < choice_.forced.size() &&
                  choice_.forced[static_cast<std::size_t>(i)] >= 0;
    auto *move = new QTableWidgetItem(qs(toString(st.move)));
    QFont f = move->font();
    f.setBold(true);
    move->setFont(f);
    move->setTextAlignment(Qt::AlignCenter);
    if (picked) {
      move->setBackground(pickedBg);
      move->setToolTip(tr("Your pick"));
    }
    lanes_->setItem(RowMove, i, move);
    for (Hand h : {Hand::Right, Hand::Left}) {
      QStringList lines;
      bool regrip = h == Hand::Right ? st.regripRight : st.regripLeft;
      int before = h == Hand::Right ? st.before.right : st.before.left;
      int after = h == Hand::Right ? st.after.right : st.after.left;
      if (regrip) {
        lines << tr("↻ regrip to %1").arg(qs(gripName(before)));
      }
      if (!t) {
        if (h == Hand::Right) {
          lines << tr("(no trick)");
        }
      } else if (t->hand == h || t->hand == Hand::Both) {
        lines << trickShort(*t);
        if (after != before && t->hand == h) {
          lines << tr("thumb %1").arg(qs(gripName(after)));
        }
      }
      auto *cell = new QTableWidgetItem(lines.join('\n'));
      cell->setTextAlignment(Qt::AlignCenter);
      if (regrip) {
        cell->setBackground(regripBg);
        cell->setToolTip(t && t->hand != h && t->hand != Hand::Both
                             ? tr("Regrips while the other hand turns")
                             : tr("Regrips before turning: a pause"));
      }
      lanes_->setItem(h == Hand::Right ? RowRight : RowLeft, i, cell);
    }
  }
  lanes_->resizeColumnsToContents();
  lanes_->resizeRowsToContents();
  int h = 2 * lanes_->frameWidth();
  for (int r = 0; r < RowCount; ++r) {
    h += lanes_->rowHeight(r);
  }
  // Room for a scroll bar when the moves don't fit.
  h += lanes_->horizontalScrollBar()->sizeHint().height();
  lanes_->setFixedHeight(h);
}

void GripView::fillOptions() {
  options_->clear();
  int col = lanes_->currentColumn();
  const auto &opts = result_.options;
  if (!model_ || col < 0 || col >= static_cast<int>(opts.size()) ||
      lanes_->selectedItems().isEmpty()) {
    optionsTitle_->setText(
        tr("Click a move to see the other ways to turn it."));
    return;
  }
  const auto i = static_cast<std::size_t>(col);
  const GripStep &st = result_.plan.steps[i];
  optionsTitle_->setText(tr("Ways to turn move %1, %2, and what the alg "
                            "costs that way (click one to use it):")
                             .arg(col + 1)
                             .arg(qs(toString(st.move))));
  double best = opts[i].empty() ? 0.0 : opts[i].front().total;
  for (const GripOption &o : opts[i]) {
    QString name =
        o.trick >= 0
            ? qs(trickText(model_->tricks[static_cast<std::size_t>(o.trick)]))
            : tr("no trick in the profile");
    bool chosen = o.trick == st.trick;
    QString extra = o.total - best < 0.005
                        ? tr("best, %1").arg(cost(o.total))
                        : tr("+%1").arg(cost(o.total - best));
    auto *item = new QListWidgetItem(
        (chosen ? QStringLiteral("✓ ") : QStringLiteral("    ")) + name +
            QStringLiteral("  —  ") + extra,
        options_);
    item->setData(Qt::UserRole, o.trick);
  }
}

void GripView::optionPicked() {
  QListWidgetItem *item = options_->currentItem();
  int col = lanes_->currentColumn();
  if (!item || col < 0) {
    return;
  }
  const auto i = static_cast<std::size_t>(col);
  if (choice_.forced.size() < alg_.size()) {
    choice_.forced.resize(alg_.size(), -1);
  }
  int trick = item->data(Qt::UserRole).toInt();
  if (choice_.forced[i] == trick ||
      (choice_.forced[i] < 0 && result_.plan.steps[i].trick == trick)) {
    return;
  }
  choice_.forced[i] = trick;
  replan();
}

} // namespace vcube
