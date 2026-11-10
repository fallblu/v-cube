#include "app/review_dialog.hpp"

#include "app/colours.hpp"
#include "app/cube_view.hpp"
#include "app/widgets.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace vcube {

namespace {

// Optimal solutions kept per hold.
constexpr int kSolutionsPerHold = 4;
// Optimal solutions listed.
constexpr std::size_t kListed = 4;

QLabel *richLabel(QWidget *parent) {
  auto *l = new QLabel(parent);
  l->setTextFormat(Qt::RichText);
  l->setWordWrap(true);
  l->setTextInteractionFlags(Qt::TextBrowserInteraction);
  l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  return l;
}

// The whole-cube rotation that turns `cube` (in any orientation) to be held
// as `h`.
Alg rotationTo(const FaceletCube &cube, Hold h) {
  for (const Alg &r : allRotations()) {
    FaceletCube f = cube;
    f.apply(r);
    if (std::optional<Hold> now = holdOf(f); now && *now == h) {
      return r;
    }
  }
  return {};
}

QString escaped(const std::string &text) { return qs(text).toHtmlEscaped(); }

QString muted(const QString &html) {
  return "<span style='color:gray'>" + html + "</span>";
}

QString moveWord(int n) {
  return n == 1 ? QObject::tr("1 move") : QObject::tr("%1 moves").arg(n);
}

} // namespace

QString holdRotationText(Hold h) { return qs(toString(holdRotation(h))); }

ReviewDialog::ReviewDialog(ReviewedSolve solve, const AppSettings &settings,
                           AlgLibrary *library, QWidget *parent)
    : QDialog(parent), solve_(std::move(solve)), settings_(settings),
      library_(library) {
  setWindowTitle(tr("Review: %1").arg(solve_.title));
  start_.apply(solve_.scramble);
  marks_ = stepMarks(start_, solve_.solution);
  // Name each step's hold from the neutrality's holds when one has it.
  std::vector<Hold> neutral = withBothEoAxes(settings_.neutrality.holds);
  for (StepMark &m : marks_) {
    FaceletCube then = start_;
    then.apply(
        Alg(solve_.solution.begin(),
            solve_.solution.begin() + static_cast<std::ptrdiff_t>(m.moves)));
    for (Hold h : stepReachedHolds(then, m.step)) {
      if (std::find(neutral.begin(), neutral.end(), h) != neutral.end()) {
        m.hold = h;
        break;
      }
    }
  }
  replay_ = solve_.solution;
  replayName_ = tr("Your solution");

  // --- left: the replay ---
  auto *left = new QWidget(this);
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  cube_ = new CubeView(left);
  CubeLook look = cube_->look();
  look.scheme = settings_.scheme;
  cube_->setLook(look);
  cube_->setAnimationMs(settings_.animationMs);
  cube_->setMinimumSize(320, 280);
  auto *viewRow = new QHBoxLayout;
  viewRow->addWidget(new QLabel(tr("View:"), left));
  viewRow->addWidget(createViewSelector(cube_, "review", left));
  viewRow->addStretch();
  leftLayout->addLayout(viewRow);
  leftLayout->addWidget(cube_, 1);
  slider_ = new QSlider(Qt::Horizontal, left);
  slider_->setFocusPolicy(Qt::NoFocus);
  leftLayout->addWidget(slider_);
  auto *buttons = new QHBoxLayout;
  auto button = [&](QStyle::StandardPixmap icon, const QString &tip,
                    auto &&fn) {
    auto *b = new QToolButton(left);
    b->setIcon(style()->standardIcon(icon));
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, fn);
    buttons->addWidget(b);
  };
  buttons->addStretch();
  button(QStyle::SP_MediaSkipBackward, tr("To the scramble (Home)"),
         [this] { showPosition(0); });
  button(QStyle::SP_MediaSeekBackward, tr("Back one move (←)"), [this] {
    if (at_ > 0) {
      showPosition(at_ - 1);
    }
  });
  button(QStyle::SP_MediaSeekForward, tr("Forward one move (→)"),
         [this] { showPosition(std::min(at_ + 1, replay_.size())); });
  button(QStyle::SP_MediaSkipForward, tr("To the end (End)"),
         [this] { showPosition(replay_.size()); });
  buttons->addStretch();
  leftLayout->addLayout(buttons);
  positionLabel_ = new QLabel(left);
  positionLabel_->setAlignment(Qt::AlignCenter);
  leftLayout->addWidget(positionLabel_);

  // --- right: what happened, and what could have ---
  auto *right = new QWidget;
  auto *rightLayout = new QVBoxLayout(right);
  auto *scramble = richLabel(right);
  scramble->setText(
      "<b>" + solve_.title.toHtmlEscaped() + "</b>" +
      (solve_.dnf ? " · DNF" : QString()) + "<br>" +
      muted(tr("Scramble: %1").arg(escaped(toString(solve_.scramble)))));
  rightLayout->addWidget(scramble);

  auto *movesBox = new QGroupBox(tr("Moves"), right);
  auto *movesLayout = new QVBoxLayout(movesBox);
  movesLabel_ = richLabel(movesBox);
  QFont mf = movesLabel_->font();
  mf.setPointSize(mf.pointSize() + 2);
  movesLabel_->setFont(mf);
  movesLayout->addWidget(movesLabel_);
  marksLabel_ = richLabel(movesBox);
  movesLayout->addWidget(marksLabel_);
  rightLayout->addWidget(movesBox);

  auto *optimalBox = new QGroupBox(tr("Optimal"), right);
  auto *optimalLayout = new QVBoxLayout(optimalBox);
  optimalLabel_ = richLabel(optimalBox);
  optimalLayout->addWidget(optimalLabel_);
  rightLayout->addWidget(optimalBox);

  pairsBox_ = new QGroupBox(tr("F2L pairs"), right);
  auto *pairsLayout = new QVBoxLayout(pairsBox_);
  auto *pairSetRow = new QHBoxLayout;
  pairSetBox_ = new QComboBox(pairsBox_);
  pairSetBox_->setToolTip(
      tr("The alg set whose pair cases the solve's pairs are compared with"));
  pairSetRow->addWidget(new QLabel(tr("Compared with:"), pairsBox_));
  pairSetRow->addWidget(pairSetBox_, 1);
  pairsLayout->addLayout(pairSetRow);
  pairsLabel_ = richLabel(pairsBox_);
  pairsLayout->addWidget(pairsLabel_);
  rightLayout->addWidget(pairsBox_);
  rightLayout->addStretch();

  auto *scroll = new QScrollArea(this);
  scroll->setWidget(right);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setFocusPolicy(Qt::NoFocus);

  auto *bottom = new QDialogButtonBox(this);
  retryButton_ = bottom->addButton(tr("Retry this scramble"),
                                   QDialogButtonBox::ActionRole);
  retryButton_->setToolTip(
      tr("Solve the scramble again; the retry isn't recorded"));
  retryButton_->setVisible(false);
  retryButton_->setAutoDefault(false);
  QPushButton *close = bottom->addButton(QDialogButtonBox::Close);
  close->setAutoDefault(false);
  connect(bottom, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(retryButton_, &QPushButton::clicked, this, [this] {
    emit retryRequested();
    accept();
  });

  auto *columns = new QHBoxLayout;
  columns->addWidget(left, 1);
  columns->addWidget(scroll, 1);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(columns, 1);
  layout->addWidget(bottom);
  resize(1100, 720);

  connect(slider_, &QSlider::valueChanged, this, [this](int v) {
    if (static_cast<std::size_t>(v) != at_) {
      showPosition(static_cast<std::size_t>(v));
    }
  });
  auto link = [this](const QString &href) {
    // "opt:<list>:<hold>:<solution>" plays an optimal solution, "pair:<n>:
    // <option>" the set's alg for a pair, "mine" the solution again.
    QStringList parts = href.split(':');
    if (parts[0] == "mine") {
      play(solve_.solution, tr("Your solution"));
    } else if (parts[0] == "opt" && parts.size() == 4 && optima_) {
      const auto &list = parts[1] == "all" ? optima_->all : optima_->neutral;
      auto h = parts[2].toULongLong();
      auto i = parts[3].toULongLong();
      if (h < list.size() && i < list[h].optimum.solutions.size()) {
        Alg alg = holdRotation(list[h].hold);
        const Alg &moves = list[h].optimum.solutions[i];
        alg.insert(alg.end(), moves.begin(), moves.end());
        play(alg, tr("Optimal solution"));
      }
    } else if (parts[0] == "pair" && parts.size() == 3) {
      F2LBreakdown b = f2lBreakdown(
          start_, solve_.solution,
          library_ ? library_->find(ss(pairSetBox_->currentData().toString()))
                   : nullptr);
      auto n = parts[1].toULongLong();
      auto o = parts[2].toULongLong();
      if (n < b.segments.size() && o < b.segments[n].options.size()) {
        const PairSegment &seg = b.segments[n];
        Alg alg(solve_.solution.begin(),
                solve_.solution.begin() +
                    static_cast<std::ptrdiff_t>(seg.begin));
        FaceletCube then = start_;
        then.apply(alg);
        Alg turn = rotationTo(then, b.hold);
        alg.insert(alg.end(), turn.begin(), turn.end());
        Alg pair = parseAlgOrEmpty(seg.options[o].alg);
        alg.insert(alg.end(), pair.begin(), pair.end());
        play(alg, tr("%1 with the set's alg")
                      .arg(qs(std::string(slotName(seg.options[o].slot)))));
        showPosition(seg.begin + turn.size());
      }
    }
  };
  for (QLabel *l : {movesLabel_, optimalLabel_, pairsLabel_}) {
    connect(l, &QLabel::linkActivated, this, link);
  }
  connect(pairSetBox_, &QComboBox::activated, this, [this] {
    QSettings().setValue("review/pairSet", pairSetBox_->currentData());
    showPairs();
  });
  connect(&optimaWatcher_, &QFutureWatcher<Optima>::finished, this, [this] {
    optima_ = optimaWatcher_.result();
    showOptima();
  });

  buildPairSets();
  showMarks();
  showPairs();
  startOptima();
  play(solve_.solution, replayName_);
  showPosition(replay_.size());
}

ReviewDialog::~ReviewDialog() { optimaWatcher_.waitForFinished(); }

void ReviewDialog::setRetryEnabled(bool on) { retryButton_->setVisible(on); }

void ReviewDialog::keyPressEvent(QKeyEvent *e) {
  switch (e->key()) {
  case Qt::Key_Left:
    if (at_ > 0) {
      showPosition(at_ - 1);
    }
    return;
  case Qt::Key_Right:
    showPosition(std::min(at_ + 1, replay_.size()));
    return;
  case Qt::Key_Home:
    showPosition(0);
    return;
  case Qt::Key_End:
    showPosition(replay_.size());
    return;
  default:
    QDialog::keyPressEvent(e);
  }
}

void ReviewDialog::buildPairSets() {
  bool solveHasPairs = !solve_.step || *solve_.step == SolveStep::ZZF2L;
  pairsBox_->setVisible(solveHasPairs);
  if (!solveHasPairs || !library_) {
    return;
  }
  QString wanted = QSettings().value("review/pairSet", "zz-f2l").toString();
  for (const AlgSet &s : library_->sets()) {
    if (isPairSet(s)) {
      pairSetBox_->addItem(qs(s.name), qs(s.id));
    }
  }
  int i = pairSetBox_->findData(wanted);
  pairSetBox_->setCurrentIndex(std::max(i, 0));
}

void ReviewDialog::play(const Alg &alg, const QString &name) {
  replay_ = alg;
  replayName_ = name;
  slider_->blockSignals(true);
  slider_->setRange(0, static_cast<int>(replay_.size()));
  slider_->blockSignals(false);
  at_ = replay_.size() + 1; // so that showPosition() jumps
  showPosition(0);
}

void ReviewDialog::showPosition(std::size_t at) {
  at = std::min(at, replay_.size());
  if (at == at_ + 1) {
    cube_->animateMove(replay_[at_]);
  } else if (at + 1 == at_ && at < replay_.size()) {
    cube_->animateSequence(invert(Alg{replay_[at]}));
  } else if (at != at_) {
    FaceletCube f = start_;
    f.apply(Alg(replay_.begin(),
                replay_.begin() + static_cast<std::ptrdiff_t>(at)));
    cube_->setState(f);
  }
  at_ = at;
  slider_->blockSignals(true);
  slider_->setValue(static_cast<int>(at));
  slider_->blockSignals(false);
  QString where = tr("%1: move %2 of %3, %4 HTM")
                      .arg(replayName_)
                      .arg(at)
                      .arg(replay_.size())
                      .arg(htmCount(Alg(replay_.begin(),
                                        replay_.begin() +
                                            static_cast<std::ptrdiff_t>(at))));
  positionLabel_->setText(where);
  showMoves();
}

void ReviewDialog::showMoves() {
  bool mine = replay_ == solve_.solution;
  QStringList parts;
  auto markAt = [&](std::size_t i) {
    if (!mine) {
      return;
    }
    QStringList steps;
    for (const StepMark &m : marks_) {
      if (m.moves == i && i > 0) {
        steps << qs(std::string(stepName(m.step)));
      }
    }
    if (!steps.isEmpty()) {
      parts << "<span style='color:#3dd66b'>| " + steps.join(", ") + "</span>";
    }
  };
  for (std::size_t i = 0; i < replay_.size(); ++i) {
    markAt(i);
    QString t = escaped(toString(replay_[i]));
    if (i + 1 == at_) {
      t = "<span style='color:#ffb030; font-weight:bold'>" + t + "</span>";
    } else if (i >= at_) {
      t = muted(t);
    }
    parts << t;
  }
  markAt(replay_.size());
  QString html = parts.isEmpty() ? muted(tr("No moves")) : parts.join(' ');
  if (!mine) {
    html = "<b>" + replayName_.toHtmlEscaped() + "</b>: " + html + "<br>" +
           "<a href='mine'>" + tr("Back to your solution") + "</a>";
  }
  movesLabel_->setText(html);
}

void ReviewDialog::showMarks() {
  if (marks_.empty()) {
    marksLabel_->setText(muted(tr("No step was done.")));
    return;
  }
  QString html = "<table cellspacing='3'>";
  int before = 0;
  QStringList byScramble; // steps the scramble left done
  for (const StepMark &m : marks_) {
    if (m.moves == 0) {
      byScramble << qs(std::string(stepName(m.step)));
      continue;
    }
    html += QString("<tr><td><b>%1</b></td><td align='right'>%2</td>"
                    "<td>%3</td><td>%4</td></tr>")
                .arg(qs(std::string(stepName(m.step))))
                .arg(moveWord(m.htm))
                .arg(m.htm > before ? muted(tr("(+%1)").arg(m.htm - before))
                                    : QString())
                .arg(stepHoldHtml(m.hold, m.step == SolveStep::EO,
                                  settings_.scheme));
    before = m.htm;
  }
  html += "</table>";
  if (!byScramble.isEmpty()) {
    html += muted(tr("Done by the scramble: %1").arg(byScramble.join(", ")));
  }
  marksLabel_->setText(html);
}

void ReviewDialog::startOptima() {
  SolveStep step = solve_.step.value_or(SolveStep::EOCross);
  if (!hasOptimalSolver(step)) {
    optimalLabel_->parentWidget()->setVisible(false);
    return;
  }
  optimalLabel_->setText(muted(tr("Working out the optimal solutions…")));
  std::vector<Hold> neutral = withBothEoAxes(settings_.neutrality.holds);
  Alg scramble = solve_.scramble;
  optimaWatcher_.setFuture(QtConcurrent::run([scramble, step, neutral] {
    const auto &every = allHolds();
    return Optima{solveStepHeld(scramble, step, neutral, kSolutionsPerHold),
                  solveStepHeld(scramble, step, {every.begin(), every.end()},
                                kSolutionsPerHold)};
  }));
}

void ReviewDialog::showOptima() {
  if (!optima_) {
    return;
  }
  SolveStep step = solve_.step.value_or(SolveStep::EOCross);
  QString stepText = qs(std::string(stepName(step)));
  // The solve's own count for the step: where it was first done.
  std::optional<int> yours;
  for (const StepMark &m : marks_) {
    if (m.step == step) {
      yours = m.htm;
    }
  }
  QString html;
  if (!solve_.step) {
    html += muted(tr("For reference: EO Cross for this scramble.")) + "<br>";
  }
  html +=
      yours ? tr("You did %1 in <b>%2</b>.").arg(stepText).arg(moveWord(*yours))
            : tr("%1 wasn't done.").arg(stepText);
  auto list = [&](const std::vector<HeldOptimum> &optima, const QString &id,
                  const QString &heading) {
    if (optima.empty()) {
      return;
    }
    int best = optima.front().optimum.length;
    html += "<p><b>" + heading.arg(best) + "</b>";
    if (yours) {
      int over = *yours - best;
      html += " " + (over == 0 ? tr("(you were optimal)")
                               : muted(tr("(+%1)").arg(over)));
    }
    std::size_t shown = 0;
    for (std::size_t h = 0; h < optima.size() && shown < kListed; ++h) {
      const HeldOptimum &o = optima[h];
      if (o.optimum.length != best) {
        break;
      }
      for (std::size_t i = 0; i < o.optimum.solutions.size() && shown < kListed;
           ++i, ++shown) {
        QString rotation = holdRotationText(o.hold);
        QString moves = escaped(toString(o.optimum.solutions[i]));
        html += "<br>" +
                stepHoldHtml(o.hold, step == SolveStep::EO, settings_.scheme) +
                ": " +
                (rotation.isEmpty() ? QString() : muted(rotation) + " ") +
                moves +
                QString(" <a href='opt:%1:%2:%3'>%4</a>")
                    .arg(id)
                    .arg(h)
                    .arg(i)
                    .arg(tr("show"));
      }
    }
    // The other holds' lengths, shortest first.
    QStringList others;
    for (const HeldOptimum &o : optima) {
      if (o.optimum.length != best) {
        others << QString("%1: %2")
                      .arg(stepHoldHtml(o.hold, step == SolveStep::EO,
                                        settings_.scheme))
                      .arg(o.optimum.length);
      }
    }
    if (!others.isEmpty() && others.size() <= 8) {
      html += "<br>" + muted(tr("Other holds: ")) + others.join(", ");
    }
    html += "</p>";
  };
  list(optima_->neutral, "neutral", tr("Optimal in your holds: %1"));
  list(optima_->all, "all", tr("Optimal over every colour and axis: %1"));
  optimalLabel_->setText(html);
}

void ReviewDialog::showPairs() {
  if (!pairsBox_->isVisible() && !pairsBox_->isVisibleTo(this)) {
    return;
  }
  const AlgSet *reference =
      library_ && pairSetBox_->count() > 0
          ? library_->find(ss(pairSetBox_->currentData().toString()))
          : nullptr;
  F2LBreakdown b = f2lBreakdown(start_, solve_.solution, reference);
  if (!b.found) {
    pairsLabel_->setText(muted(tr("The solve never had EO Cross done.")));
    return;
  }
  QString html;
  if (!reference) {
    html += muted(tr("No alg set of F2L pair cases to compare with (such as "
                     "a ZZ F2L set).")) +
            "<br>";
  }
  html += tr("EO Cross after %1, %2.")
              .arg(moveWord(
                  htmCount(Alg(solve_.solution.begin(),
                               solve_.solution.begin() +
                                   static_cast<std::ptrdiff_t>(b.eoCross)))))
              .arg(stepHoldHtml(b.hold, false, settings_.scheme));
  int yourTotal = 0, setTotal = 0;
  bool allMatched = true;
  for (std::size_t n = 0; n < b.segments.size(); ++n) {
    const PairSegment &seg = b.segments[n];
    QStringList solved;
    for (Slot s : seg.solved) {
      solved << qs(std::string(slotName(s)));
    }
    yourTotal += seg.htm;
    html += "<p><b>" + tr("Pair %1: %2").arg(n + 1).arg(solved.join(" + ")) +
            "</b> — " + tr("you took %1").arg(moveWord(seg.htm)) + " " +
            muted(tr("(moves %1–%2)").arg(seg.begin + 1).arg(seg.end));
    // The set's alg for the pair that was done, and the cheapest then.
    const PairOption *done = nullptr;
    const PairOption *cheapest = nullptr;
    for (const PairOption &o : seg.options) {
      if (o.caseId.empty()) {
        continue;
      }
      if (seg.solved.size() == 1 && o.slot == seg.solved.front()) {
        done = &o;
      }
      if (!cheapest || o.moves < cheapest->moves) {
        cheapest = &o;
      }
    }
    auto optionHtml = [&](const PairOption &o) {
      auto index = static_cast<std::size_t>(&o - seg.options.data());
      return tr("%1 (%2): %3 %4 <a href='pair:%5:%6'>%7</a>")
          .arg(qs(std::string(slotName(o.slot))), escaped(o.caseName))
          .arg(moveWord(o.moves))
          .arg(muted(escaped(o.alg)))
          .arg(n)
          .arg(index)
          .arg(tr("show"));
    };
    if (done) {
      setTotal += done->moves;
      int over = seg.htm - done->moves;
      html += "<br>" + tr("Your set: %1").arg(optionHtml(*done)) +
              (over > 0   ? " " + muted(tr("(you +%1)").arg(over))
               : over < 0 ? " " + muted(tr("(you %1 fewer)").arg(-over))
                          : QString());
    } else if (seg.solved.size() == 1) {
      allMatched = false;
      html += "<br>" + muted(tr("No case of the set matched this pair."));
    } else {
      allMatched = false;
      html += "<br>" + muted(tr("Several slots were solved at once."));
    }
    if (cheapest && cheapest != done &&
        (!done || cheapest->moves < done->moves)) {
      html += "<br>" + tr("Cheapest then: %1").arg(optionHtml(*cheapest));
    }
    html += "</p>";
  }
  if (reference && !b.segments.empty()) {
    html += "<p>" + tr("Pairs in all: you %1").arg(yourTotal);
    if (allMatched) {
      html += tr(", your set's algs for the same pairs %1").arg(setTotal);
    }
    // The best order from EO Cross with the set's algs.
    FaceletCube f = start_;
    f.apply(
        Alg(solve_.solution.begin(),
            solve_.solution.begin() + static_cast<std::ptrdiff_t>(b.eoCross)));
    if (std::optional<CubieCube> c = heldCubie(f, b.hold)) {
      PairPlan plan = cheapestPairOrder(*c, *reference);
      if (plan.complete && !plan.steps.empty()) {
        QStringList order;
        for (const PairOption &o : plan.steps) {
          order << qs(std::string(slotName(o.slot)));
        }
        html += tr(", the set's cheapest order %1 (%2)")
                    .arg(plan.moves)
                    .arg(order.join(" → "));
      }
    }
    html += ".</p>";
  }
  if (!b.complete) {
    html += muted(tr("Not every slot was solved."));
  }
  pairsLabel_->setText(html);
}

} // namespace vcube
