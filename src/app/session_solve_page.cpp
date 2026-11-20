#include "app/session_solve_page.hpp"

#include "app/cube_view.hpp"
#include "app/page_parts.hpp"
#include "io/qt_text.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>

namespace vcube {

SessionSolvePage::SessionSolvePage(AppSettings &settings, AlgLibrary &library,
                                   const QString &mode, int panelWidth,
                                   QWidget *parent)
    : ModePage(parent), settings_(settings), library_(library), feed_(library),
      eoCheck_(settings) {
  // --- left column: sessions, stats, attempts ---
  panel_ = new SessionPanel(this);
  panel_->setStatsClickable(true);
  panel_->setDeleteRowVisible(true);

  // --- right column: scramble, cube, chart ---
  stage_ = new SolveStage(mode, this);
  hideInFocusMode({stage_->options});
  cubeBox_ = stage_->cubeBox;
  skipButton_ = stage_->skipButton;
  scrambleLabel_ = stage_->scramble;
  net_ = stage_->net;
  cubeView_ = stage_->cube;
  display_ = stage_->display;
  hintLabel_ = stage_->hint;
  focusStats_ = stage_->focusStats;
  chart_ = panel_->chart();
  chart_->setSettingsKey(mode + "/chart");

  QSplitter *split =
      mainSplitter(this, {panel_, stage_}, {panelWidth, 1100 - panelWidth});
  split->setStretchFactor(0, 0);
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(split);

  // --- wiring ---
  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &SessionSolvePage::showScramble);
  connect(skipButton_, &QPushButton::clicked, this, &SessionSolvePage::skip);
  connect(cubeBox_, &QComboBox::activated, this, [this](int index) {
    CubeKind kind = index == 0 ? CubeKind::Virtual : CubeKind::Physical;
    if (isBusy() || onVirtualCube() == (kind == CubeKind::Virtual)) {
      return;
    }
    if (attemptCount() > 0) {
      // Its solves were done on the other cube.
      cubeBox_->setCurrentIndex(onVirtualCube() ? 0 : 1);
      emit message(tr("This session has solves on the other cube: start a "
                      "new session for this one"));
      return;
    }
    cubeChosen(kind);
  });
  connect(panel_, &SessionPanel::menuAboutToShow, this,
          &SessionSolvePage::refreshSessionMenu);
  connect(panel_, &SessionPanel::sessionChosen, this,
          &SessionSolvePage::selectSession);
  connect(panel_, &SessionPanel::newSessionRequested, this,
          &SessionSolvePage::addSession);
  connect(panel_, &SessionPanel::renameSessionRequested, this,
          &SessionSolvePage::renameSession);
  connect(panel_, &SessionPanel::deleteSessionRequested, this,
          &SessionSolvePage::deleteSession);
  connect(panel_, &SessionPanel::deleteLastClicked, this, [this] {
    if (attemptCount() > 0) {
      deleteAttempt(static_cast<int>(attemptCount()) - 1);
    }
  });
  connect(panel_, &SessionPanel::undoClicked, this,
          &SessionSolvePage::undoDelete);
  connect(panel_, &SessionPanel::attemptMenuRequested, this,
          &SessionSolvePage::showAttemptMenu);
  connect(panel_, &SessionPanel::attemptActivated, this, [this](int index) {
    if (index >= 0 && index < static_cast<int>(attemptCount()) &&
        canReview(index)) {
      review(index);
    } else {
      showDetails(index);
    }
  });
  connect(panel_, &SessionPanel::statActivated, this,
          &SessionSolvePage::showStat);
}

void SessionSolvePage::setTypeBox(ScrambleTypeBox *box, QWidget *extra) {
  typeBox_ = box;
  stage_->options->addPicker(tr("Scramble:"), typeBox_, 1);
  if (extra) {
    stage_->options->addWidget(extra);
  }
  stage_->addStandardOptions();
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            typeChosen(type);
            if (algs_) {
              algs_->setType(type);
            }
            feed_.setType(type);
          });
}

void SessionSolvePage::start(std::size_t index) {
  useSettings(); // selectSession() shows the attempts
  selectSession(index);
  if (algs_) {
    algs_->setType(typeBox_->currentType());
  }
  feed_.setType(typeBox_->currentType()); // if the type didn't change
}

void SessionSolvePage::applySettings() {
  useSettings();
  refreshAttempts();
}

void SessionSolvePage::useSettings() {
  configureFlow();
  net_->setScheme(settings_.scheme);
  cubeView_->applySettings(settings_);
  feed_.setNeutrality(settings_.neutrality);
  if (algs_) {
    algs_->refresh();
  }
  settingsApplied();
}

void SessionSolvePage::refreshScrambleTypes(const QString &changedSet) {
  QString before = typeBox_->currentType();
  typeBox_->refresh(); // emits typeChanged if the type went away
  if (typeBox_->currentType() == before && !changedSet.isEmpty() &&
      before == "set:" + changedSet) {
    scrambleDropped();
    feed_.regenerate(); // the set's cases or ticks may have changed
  }
  if (algs_) {
    algs_->refresh();
  }
}

void SessionSolvePage::focusModeChanged() { showChart(); }

void SessionSolvePage::showCubeKind() {
  bool virt = onVirtualCube();
  cubeBox_->setCurrentIndex(virt ? 0 : 1);
  cubeView_->setVisible(virt);
  stage_->viewPicker->setVisible(virt);
  net_->setVisible(!virt);
  panel_->setColumns(columns());
  showChart();
}

void SessionSolvePage::showChart() {
  chart_->setVisible(!focusMode() && !scramblesOnly());
  focusStats_->setVisible(focusMode() && !scramblesOnly());
}

void SessionSolvePage::showStats(
    std::span<const Solve> solves, std::int64_t unit, const QString &single,
    const std::function<QString(const std::string &, StatValue)> &format,
    std::size_t count) {
  static constexpr std::string_view kStats[] = {"single", "mo3", "ao5", "ao12",
                                                "ao100"};
  SessionStats stats = sessionStats(solves, kStats, unit);
  const SessionSummary &sum = stats.summary;
  QString colour = palette().color(QPalette::WindowText).name();
  QString html = statsTableHtml(
      stats,
      [&single](const std::string &id) {
        return id == "single" ? single : qs(id);
      },
      format, colour);
  html += tr("<p>Solves: %1%2 &nbsp; Mean: %3</p>")
              .arg(count)
              .arg(sum.dnfs ? QString(" (%1 DNF)").arg(sum.dnfs) : QString())
              .arg(statLinkHtml("mean:all", sum.mean, format("mean", sum.mean),
                                colour));
  addStats(html);
  panel_->setStatsHtml(html);
  focusStats_->setText(
      tr("ao5 %1 · ao12 %2 · best %3 · %4 solves")
          .arg(format("ao5", currentAverage(solves, 5, unit)),
               format("ao12", currentAverage(solves, 12, unit)),
               format("single", sum.best))
          .arg(count));
}

void SessionSolvePage::enableControls(bool undo) {
  bool busy = isBusy();
  for (QWidget *w : std::initializer_list<QWidget *>{
           typeBox_, skipButton_, panel_->sessionButton(), cubeBox_}) {
    w->setEnabled(!busy);
  }
  panel_->setDeleteEnabled(!busy && attemptCount() > 0);
  panel_->setUndoVisible(undo);
  panel_->setUndoEnabled(!busy);
}

} // namespace vcube
