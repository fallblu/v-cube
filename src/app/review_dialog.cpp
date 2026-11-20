#include "app/review_dialog.hpp"

#include "app/colours.hpp"
#include "app/cube_view.hpp"
#include "app/page_parts.hpp"
#include "app/text_style.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
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
  // Links by mouse only, and no focus: the arrow keys step the replay.
  l->setTextInteractionFlags(Qt::TextSelectableByMouse |
                             Qt::LinksAccessibleByMouse);
  l->setFocusPolicy(Qt::NoFocus);
  l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  return l;
}

QString secondsText(std::int64_t ms) {
  return QString::fromStdString(formatTime(ms));
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
  return "<span style='color:" + roleColour(Role::Muted).name() + "'>" + html +
         "</span>";
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
  marks_ = stepMarks(start_, solve_.solution, settings_.simplifiedCounts);
  if (!solve_.step) {
    splits_ = solveSplits(start_, solve_.solution, solve_.times, solve_.totalMs,
                          settings_.simplifiedCounts);
  }
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
  cube_->applySettings(settings_);
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
  auto button = [&](QStyle::StandardPixmap icon, const QString &name,
                    auto &&fn) {
    auto *b = new QToolButton(left);
    b->setIcon(style()->standardIcon(icon));
    b->setToolTip(name);
    b->setAccessibleName(name);
    b->setFocusPolicy(Qt::NoFocus);
    QSize icons = b->iconSize();
    b->setIconSize(icons * 1.4);
    connect(b, &QToolButton::clicked, this, fn);
    buttons->addWidget(b);
    return b;
  };
  buttons->addStretch();
  button(QStyle::SP_MediaSkipBackward, tr("To the scramble (Home)"), [this] {
    setPlaying(false);
    showPosition(0);
  });
  button(QStyle::SP_MediaSeekBackward, tr("Back one move (←)"),
         [this] { step(-1); });
  playButton_ = button(QStyle::SP_MediaPlay, tr("Play (Space)"),
                       [this] { setPlaying(!playTimer_.isActive()); });
  button(QStyle::SP_MediaSeekForward, tr("Forward one move (→)"),
         [this] { step(1); });
  button(QStyle::SP_MediaSkipForward, tr("To the end (End)"), [this] {
    setPlaying(false);
    showPosition(replay_.size());
  });
  buttons->addStretch();
  leftLayout->addLayout(buttons);
  positionLabel_ = new QLabel(left);
  positionLabel_->setAlignment(Qt::AlignCenter);
  leftLayout->addWidget(positionLabel_);
  // The keys, as on the stages.
  keys_ = new HintBar(left);
  leftLayout->addWidget(keys_);
  showKeys();
  // A move at a time, a little slower than a turn is animated.
  playTimer_.setInterval(std::max(300, settings_.animationMs + 200));
  connect(&playTimer_, &QTimer::timeout, this, [this] {
    if (at_ >= replay_.size()) {
      setPlaying(false);
      return;
    }
    showPosition(at_ + 1);
    if (at_ >= replay_.size()) {
      setPlaying(false);
    }
  });

  // --- right: what happened, and what could have ---
  auto *right = new QWidget;
  auto *rightLayout = new QVBoxLayout(right);
  auto *scramble = richLabel(right);
  QString recorded;
  if (solve_.recorded) {
    recorded = " · " + tr("recorded: %1").arg(moveWord(*solve_.recorded));
  }
  if (solve_.totalMs > 0) {
    recorded += " · " + secondsText(solve_.totalMs);
  }
  scramble->setText(
      "<b>" + solve_.title.toHtmlEscaped() + "</b>" +
      (solve_.dnf ? " · DNF" : QString()) + recorded + "<br>" +
      muted(tr("Scramble: %1").arg(escaped(toString(solve_.scramble)))));
  rightLayout->addWidget(scramble);

  auto *movesBox = new QGroupBox(tr("Moves"), right);
  auto *movesLayout = new QVBoxLayout(movesBox);
  movesLabel_ = richLabel(movesBox);
  setTextRole(movesLabel_, TextRole::Heading);
  movesLayout->addWidget(movesLabel_);
  marksLabel_ = richLabel(movesBox);
  movesLayout->addWidget(marksLabel_);
  rightLayout->addWidget(movesBox);

  auto *splitsBox = new QGroupBox(tr("Splits"), right);
  auto *splitsLayout = new QVBoxLayout(splitsBox);
  splitsLabel_ = richLabel(splitsBox);
  splitsLayout->addWidget(splitsLabel_);
  rightLayout->addWidget(splitsBox);
  splitsBox->setVisible(!solve_.step);

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
  compareLabel_ = new QLabel(tr("Compared with:"), pairsBox_);
  pairSetRow->addWidget(compareLabel_);
  pairSetRow->addWidget(pairSetBox_, 1);
  pairsLayout->addLayout(pairSetRow);
  pairsLabel_ = richLabel(pairsBox_);
  pairsLayout->addWidget(pairsLabel_);
  rightLayout->addWidget(pairsBox_);
  rightLayout->addStretch();

  QScrollArea *scroll = scrollArea(right, this);

  auto *bottom = new QDialogButtonBox(this);
  auto *copyButton = bottom->addButton(tr("Copy reconstruction"),
                                       QDialogButtonBox::ActionRole);
  copyButton->setToolTip(
      tr("Copy the scramble and solution, a step per line with its moves "
         "and time, e.g. to post it (C)"));
  auto *webButton =
      bottom->addButton(tr("alg.cubing.net"), QDialogButtonBox::ActionRole);
  webButton->setToolTip(tr("Open the solve on alg.cubing.net (W)"));
  retryButton_ = bottom->addButton(tr("Retry this scramble"),
                                   QDialogButtonBox::ActionRole);
  retryButton_->setToolTip(
      tr("Solve the scramble again; the retry isn't recorded (R)"));
  retryButton_->setVisible(false);
  QPushButton *close = bottom->addButton(QDialogButtonBox::Close);
  // The buttons take no keys, so that the arrow keys step the replay.
  for (QPushButton *b : {copyButton, webButton, retryButton_, close}) {
    b->setAutoDefault(false);
    b->setFocusPolicy(Qt::NoFocus);
  }
  connect(bottom, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(retryButton_, &QPushButton::clicked, this, [this] {
    emit retryRequested();
    accept();
  });
  connect(copyButton, &QPushButton::clicked, this, [this] {
    SolveSplits splits =
        solveSplits(start_, solve_.solution, solve_.times, solve_.totalMs,
                    settings_.simplifiedCounts);
    QApplication::clipboard()->setText(
        qs(reconstruction(solve_.scramble, solve_.solution, splits)));
  });
  connect(webButton, &QPushButton::clicked, this, [this] {
    SolveSplits splits =
        solveSplits(start_, solve_.solution, solve_.times, solve_.totalMs,
                    settings_.simplifiedCounts);
    QDesktopServices::openUrl(QUrl(qs(reconstructionUrl(
        solve_.scramble, solve_.solution, splits, ss(solve_.title)))));
  });

  auto *columns = new QHBoxLayout;
  columns->addWidget(left, 1);
  columns->addWidget(scroll, 1);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(columns, 1);
  layout->addWidget(bottom);
  resize(1100, 720);
  // Window shortcuts, so that the keys step the replay whatever has the
  // focus.
  auto shortcut = [this](Qt::Key key, auto &&fn) {
    auto *s = new QShortcut(QKeySequence(key), this);
    s->setContext(Qt::WindowShortcut);
    connect(s, &QShortcut::activated, this, fn);
  };
  shortcut(Qt::Key_Left, [this] { step(-1); });
  shortcut(Qt::Key_Right, [this] { step(1); });
  shortcut(Qt::Key_Home, [this] {
    setPlaying(false);
    showPosition(0);
  });
  shortcut(Qt::Key_End, [this] {
    setPlaying(false);
    showPosition(replay_.size());
  });
  shortcut(Qt::Key_Space, [this] { setPlaying(!playTimer_.isActive()); });
  // The buttons take no focus: each has a key of its own.
  shortcut(Qt::Key_C, [copyButton] { copyButton->click(); });
  shortcut(Qt::Key_W, [webButton] { webButton->click(); });
  shortcut(Qt::Key_R, [this] {
    if (retryButton_->isVisibleTo(this)) {
      retryButton_->click();
    }
  });
  connect(slider_, &QSlider::sliderPressed, this,
          [this] { setPlaying(false); });

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
    } else if (parts[0] == "pairopt" && parts.size() == 2 && pairOptima_) {
      F2LBreakdown b =
          f2lBreakdown(start_, solve_.solution, nullptr, Edges::Oriented,
                       settings_.simplifiedCounts);
      auto n = parts[1].toULongLong();
      if (n < b.segments.size() && n < pairOptima_->size() &&
          (*pairOptima_)[n]) {
        const PairSegment &seg = b.segments[n];
        Alg alg(solve_.solution.begin(),
                solve_.solution.begin() +
                    static_cast<std::ptrdiff_t>(seg.begin));
        FaceletCube then = start_;
        then.apply(alg);
        Alg turn = rotationTo(then, b.hold);
        alg.insert(alg.end(), turn.begin(), turn.end());
        const Alg &pair = *(*pairOptima_)[n];
        alg.insert(alg.end(), pair.begin(), pair.end());
        play(alg, tr("The optimal pair"));
        showPosition(seg.begin + turn.size());
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
        play(
            alg,
            tr("%1 with the set's alg").arg(qs(slotName(seg.options[o].slot))));
        showPosition(seg.begin + turn.size());
      }
    }
  };
  for (QLabel *l : {movesLabel_, optimalLabel_, pairsLabel_}) {
    connect(l, &QLabel::linkActivated, this, link);
  }
  connect(&pairWatcher_, &QFutureWatcher<PairOptima>::finished, this, [this] {
    pairOptima_ = pairWatcher_.result();
    showPairs();
  });
  connect(pairSetBox_, &QComboBox::activated, this, [this] {
    saved::reviewPairSet().set(pairSetBox_->currentData().toString());
    showPairs();
  });
  connect(&optimaWatcher_, &QFutureWatcher<Optima>::finished, this, [this] {
    optima_ = optimaWatcher_.result();
    showOptima();
  });

  buildPairSets();
  showMarks();
  showSplits();
  showPairs();
  startOptima();
  startPairOptima();
  play(solve_.solution, replayName_);
  showPosition(replay_.size());
  setFocus();
}

ReviewDialog::~ReviewDialog() {
  optimaWatcher_.waitForFinished();
  pairWatcher_.waitForFinished();
}

void ReviewDialog::setRetryEnabled(bool on) {
  retryButton_->setVisible(on);
  showKeys();
}

void ReviewDialog::showKeys() {
  QList<KeyHint> keys = {{tr("← →"), tr("step")},
                         {tr("Home / End"), tr("start / end")},
                         {tr("Space"), tr("play or pause")},
                         {tr("C"), tr("copy")},
                         {tr("W"), tr("alg.cubing.net")}};
  if (retryButton_ && retryButton_->isVisibleTo(this)) {
    keys.append({tr("R"), tr("retry")});
  }
  keys_->setHint({}, keys);
}

void ReviewDialog::setPlaying(bool on) {
  if (on && at_ >= replay_.size()) {
    showPosition(0); // from the start again
  }
  on = on && !replay_.empty();
  if (!playButton_) {
    return;
  }
  if (on) {
    playTimer_.start();
  } else {
    playTimer_.stop();
  }
  playButton_->setIcon(
      style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
  QString name = on ? tr("Pause (Space)") : tr("Play (Space)");
  playButton_->setToolTip(name);
  playButton_->setAccessibleName(name);
}

void ReviewDialog::step(int by) {
  setPlaying(false);
  if (by < 0) {
    if (at_ > 0) {
      showPosition(at_ - 1);
    }
  } else {
    showPosition(std::min(at_ + 1, replay_.size()));
  }
}

void ReviewDialog::buildPairSets() {
  bool solveHasPairs = !solve_.step || *solve_.step == SolveStep::ZZF2L ||
                       *solve_.step == SolveStep::XEOCross;
  pairsBox_->setVisible(solveHasPairs);
  if (!solveHasPairs || !library_) {
    pairSetBox_->setVisible(false);
    compareLabel_->setVisible(false);
    return;
  }
  QString wanted = saved::reviewPairSet().get();
  for (const AlgSet &s : library_->sets()) {
    if (isPairSet(s)) {
      pairSetBox_->addItem(qs(s.name), qs(s.id));
    }
  }
  int i = pairSetBox_->findData(wanted);
  pairSetBox_->setCurrentIndex(std::max(i, 0));
  // Without a set to compare with there is nothing to choose.
  bool any = pairSetBox_->count() > 0;
  pairSetBox_->setVisible(any);
  compareLabel_->setVisible(any);
}

void ReviewDialog::startPairOptima() {
  if (!pairsBox_->isVisibleTo(this)) {
    return;
  }
  FaceletCube start = start_;
  Alg solution = solve_.solution;
  bool simplified = settings_.simplifiedCounts;
  pairWatcher_.setFuture(QtConcurrent::run([start, solution, simplified] {
    F2LBreakdown b =
        f2lBreakdown(start, solution, nullptr, Edges::Oriented, simplified);
    PairOptima out;
    FaceletCube f = start;
    std::size_t at = 0;
    for (const PairSegment &seg : b.segments) {
      f.apply(Alg(solution.begin() + static_cast<std::ptrdiff_t>(at),
                  solution.begin() + static_cast<std::ptrdiff_t>(seg.begin)));
      at = seg.begin;
      std::optional<CubieCube> c = heldCubie(f, b.hold);
      out.push_back(c && seg.solved.size() == 1
                        ? optimalPair(*c, seg.solved.front())
                        : std::nullopt);
    }
    return out;
  }));
}

void ReviewDialog::showSplits() {
  if (solve_.step) {
    return;
  }
  if (splits_.phases.empty()) {
    splitsLabel_->setText(muted(tr("The solve never had a cross done.")));
    return;
  }
  bool timed =
      solve_.times.size() == solve_.solution.size() && !solve_.times.empty();
  QString html = "<table cellspacing='4'><tr><th align='left'>" + tr("Step") +
                 "</th><th align='right'>" + tr("Moves") + "</th>";
  if (timed) {
    html += "<th align='right'>" + tr("Time") + "</th><th align='right'>" +
            tr("TPS") + "</th><th align='right'>" + tr("At") + "</th>";
  }
  html += "</tr>";
  auto row = [&](const QString &name, int htm, std::optional<std::int64_t> ms,
                 std::optional<std::int64_t> at, bool bold) {
    QString b = bold ? "<b>" : "", e = bold ? "</b>" : "";
    html += "<tr><td>" + b + name.toHtmlEscaped() + e +
            "</td><td align='right'>" + QString::number(htm) + "</td>";
    if (timed) {
      html += "<td align='right'>" + (ms ? secondsText(*ms) : QString("-")) +
              "</td><td align='right'>" +
              (ms && *ms > 0 ? QString::number(tps(htm, *ms), 'f', 2)
                             : QString("-")) +
              "</td><td align='right'>" +
              (at ? muted(secondsText(*at)) : QString()) + "</td>";
    }
    html += "</tr>";
  };
  for (const SolvePhase &p : splits_.phases) {
    QString name = qs(p.name);
    if (p.kind == SolvePhase::Kind::Pair) {
      name = tr("Pair: %1").arg(name);
    }
    row(name, p.htm, p.ms, p.endMs, p.kind != SolvePhase::Kind::Pair);
  }
  if (splits_.f2lMs || splits_.f2lHtm > 0) {
    row(tr("F2L in all"), splits_.f2lHtm, splits_.f2lMs, std::nullopt, true);
  }
  html += "</table>";
  int total = solutionHtm(solve_.solution, settings_.simplifiedCounts);
  if (timed && solve_.totalMs > 0) {
    html += tr("Whole solve: %1 in %2, %3 TPS")
                .arg(moveWord(total))
                .arg(secondsText(solve_.totalMs))
                .arg(tps(total, solve_.totalMs), 0, 'f', 2);
  } else if (!timed) {
    html += muted(tr("No step times: the solve wasn't timed, or was saved "
                     "before move times were kept."));
  }
  splitsLabel_->setText(html);
}

void ReviewDialog::play(const Alg &alg, const QString &name) {
  setPlaying(false);
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
                      .arg(solutionHtm(Alg(replay_.begin(),
                                           replay_.begin() +
                                               static_cast<std::ptrdiff_t>(at)),
                                       settings_.simplifiedCounts));
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
        steps << qs(stepName(m.step));
      }
    }
    if (!steps.isEmpty()) {
      parts << "<span style='color:" + statusColour(Status::Good).name() +
                   "'>| " + steps.join(", ") + "</span>";
    }
  };
  for (std::size_t i = 0; i < replay_.size(); ++i) {
    markAt(i);
    QString t = escaped(toString(replay_[i]));
    if (i + 1 == at_) {
      t = "<span style='color:" + statusColour(Status::Warning).name() +
          "; font-weight:bold'>" + t + "</span>";
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
      byScramble << qs(stepName(m.step));
      continue;
    }
    html += QString("<tr><td><b>%1</b></td><td align='right'>%2</td>"
                    "<td>%3</td><td>%4</td></tr>")
                .arg(qs(stepName(m.step)))
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
  QString stepText = qs(stepName(step));
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
  if (yours && solve_.step && solve_.recorded && *solve_.recorded != *yours) {
    html += " " + muted(tr("(%1 recorded: the moves after it count too)")
                            .arg(*solve_.recorded));
  }
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
  F2LBreakdown b = f2lBreakdown(start_, solve_.solution, reference,
                                Edges::Oriented, settings_.simplifiedCounts);
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
      solved << qs(slotName(s));
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
          .arg(qs(slotName(o.slot)), escaped(o.caseName))
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
      if (reference) {
        html += "<br>" + muted(tr("No case of the set matched this pair."));
      }
    } else {
      allMatched = false;
      html += "<br>" + muted(tr("Several slots were solved at once."));
    }
    if (seg.solved.size() == 1) {
      if (!pairOptima_) {
        html += "<br>" + muted(tr("Working out the optimal pair…"));
      } else if (n < pairOptima_->size() && (*pairOptima_)[n]) {
        const Alg &best = *(*pairOptima_)[n];
        int moves = htmCount(best);
        int over = seg.htm - moves;
        html += "<br>" +
                tr("Optimal &lt;R, U, L&gt;: %1 %2 <a href='pairopt:%3'>%4</a>")
                    .arg(moveWord(moves))
                    .arg(muted(escaped(toString(best))))
                    .arg(n)
                    .arg(tr("show")) +
                (over > 0 ? " " + muted(tr("(you +%1)").arg(over)) : QString());
      }
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
          order << qs(slotName(o.slot));
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

void showTextDialog(QWidget *parent, const QString &title,
                    const QString &text) {
  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  auto *layout = new QVBoxLayout(&dialog);
  auto *edit = new QPlainTextEdit(text, &dialog);
  edit->setReadOnly(true);
  edit->setLineWrapMode(QPlainTextEdit::NoWrap);
  QFont mono = edit->font();
  // The first of these that is installed: Linux, Windows, then macOS.
  mono.setFamilies({"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono",
                    "Cascadia Mono", "Consolas", "Menlo", "Courier New",
                    "Monospace"});
  mono.setStyleHint(QFont::Monospace);
  edit->setFont(mono);
  layout->addWidget(edit);
  QPushButton *copy =
      addButtonBox(&dialog, layout, QDialogButtonBox::Close)
          ->addButton(QObject::tr("Copy"), QDialogButtonBox::ActionRole);
  QObject::connect(copy, &QPushButton::clicked, &dialog,
                   [text] { QApplication::clipboard()->setText(text); });
  dialog.resize(640, 420);
  dialog.exec();
}

QString statListing(const QString &heading, std::span<const Solve> solves,
                    std::size_t first, bool trimmed,
                    const std::function<QString(std::size_t)> &result) {
  std::vector<std::size_t> dropped;
  if (trimmed) {
    dropped = trimmedIndices(solves);
  }
  QStringList lines{heading, QString()};
  for (std::size_t i = 0; i < solves.size(); ++i) {
    QString r = result(i);
    if (std::find(dropped.begin(), dropped.end(), i) != dropped.end()) {
      r = "(" + r + ")";
    }
    QString line = QString("%1. %2").arg(first + i).arg(r, -10);
    if (!solves[i].scramble.empty()) {
      line += "  " + qs(solves[i].scramble);
    }
    if (!solves[i].comment.empty()) {
      line += "  [" + qs(solves[i].comment) + "]";
    }
    lines << line;
  }
  return lines.join('\n');
}

void showSolveDetails(QWidget *parent, const QString &title,
                      const QList<DetailRow> &rows,
                      const std::function<void()> &review) {
  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  for (const auto &[name, value, copyable] : rows) {
    auto *label = new QLabel(value.isEmpty() ? QString("–") : value, &dialog);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setMinimumWidth(360);
    if (copyable && !value.isEmpty()) {
      auto *row = new QHBoxLayout;
      row->addWidget(label, 1);
      auto *copy = new QPushButton(QObject::tr("Copy"), &dialog);
      copy->setAutoDefault(false);
      QObject::connect(copy, &QPushButton::clicked, &dialog,
                       [value] { QApplication::clipboard()->setText(value); });
      row->addWidget(copy, 0, Qt::AlignTop);
      form->addRow(name + ":", row);
    } else {
      form->addRow(name + ":", label);
    }
  }
  layout->addLayout(form);
  auto *buttons = addButtonBox(&dialog, layout, QDialogButtonBox::Close);
  if (review) {
    QPushButton *b = buttons->addButton(QObject::tr("Review…"),
                                        QDialogButtonBox::ActionRole);
    QObject::connect(b, &QPushButton::clicked, &dialog,
                     [&dialog] { dialog.done(2); });
  }
  if (dialog.exec() == 2 && review) {
    review();
  }
}

} // namespace vcube
