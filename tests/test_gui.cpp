// The app's pages driven through the main window on the offscreen platform:
// keys go in as QTest events and what was saved is read back from the data
// folder. The settings and data folders are temporary ones, set before the
// QApplication exists, so the user's own data is never read or written.
#include "app/alglab_page.hpp"
#include "app/app_settings.hpp"
#include "app/blind_page.hpp"
#include "app/cube_view.hpp"
#include "app/drill_page.hpp"
#include "app/grip_view.hpp"
#include "app/help_panel.hpp"
#include "app/help_text.hpp"
#include "app/inspection_page.hpp"
#include "app/learn_page.hpp"
#include "app/library_page.hpp"
#include "app/main_window.hpp"
#include "app/mode_table.hpp"
#include "app/move_count_page.hpp"
#include "app/settings_dialog.hpp"
#include "app/single_instance.hpp"
#include "app/timer_page.hpp"
#include "app/today_page.hpp"
#include "app/untimed_page.hpp"
#include "cube/solver.hpp"
#include "io/alglab_store.hpp"
#include "io/data_file.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/sessions.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QThreadPool>
#include <QTimer>

#include <array>
#include <functional>
#include <memory>
#include <optional>

using namespace vcube;

namespace {

const char *const kTPerm = "R U R' U' R' F R2 U' R' U' R U R' F'";

// The temporary folders, made before anything reads a path.
QTemporaryDir *tempRoot = nullptr;

[[maybe_unused]] QString tempPath(const QString &name) {
  return QDir(tempRoot->path()).filePath(name);
}

bool isUnderTemp(const QString &path) {
  if (path.isEmpty()) {
    return false;
  }
  QString root = QDir::cleanPath(QDir(tempRoot->path()).absolutePath());
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath())
      .startsWith(root + "/");
}

// The cube as it is shown, with its stickers named by the centre they
// match: face turns of this cube are the turns of the cube as it is held.
std::optional<CubieCube> heldCube(const FaceletCube &shown) {
  FaceletCube f = shown;
  std::array<std::uint8_t, 6> faceOfColour{};
  for (int face = 0; face < 6; ++face) {
    faceOfColour[shown[face * 9 + 4]] = static_cast<std::uint8_t>(face);
  }
  for (int i = 0; i < kFaceletCount; ++i) {
    f[i] = faceOfColour[shown[i]];
  }
  return CubieCube::fromFacelets(f);
}

// Moves that solve the cube shown.
std::optional<Alg> solutionFor(const FaceletCube &shown) {
  std::optional<CubieCube> c = heldCube(shown);
  if (!c) {
    return std::nullopt;
  }
  return TwoPhaseSolver::solve(*c);
}

Alg uTurns(int n) {
  if (n % 4 == 0) {
    return {};
  }
  return {Move{MoveType::U, static_cast<std::uint8_t>(n % 4)}};
}

// `alg` with the U turns before and after it that solve the cube shown.
std::optional<Alg> withAufs(const FaceletCube &shown, const Alg &alg) {
  for (int before = 0; before < 4; ++before) {
    for (int after = 0; after < 4; ++after) {
      Alg moves = uTurns(before);
      moves.insert(moves.end(), alg.begin(), alg.end());
      Alg post = uTurns(after);
      moves.insert(moves.end(), post.begin(), post.end());
      FaceletCube f = shown;
      f.apply(moves);
      if (f.isSolved()) {
        return moves;
      }
    }
  }
  return std::nullopt;
}

ModePage *shownPage(QMainWindow &w) {
  for (QStackedWidget *s : w.findChildren<QStackedWidget *>()) {
    if (auto *page = qobject_cast<ModePage *>(s->currentWidget())) {
      return page;
    }
  }
  return nullptr;
}

// The page's main cube: the largest one shown.
CubeView *mainCube(QWidget *page) {
  CubeView *best = nullptr;
  for (CubeView *v : page->findChildren<CubeView *>()) {
    if (v->isVisible() &&
        (!best || v->width() * v->height() > best->width() * best->height())) {
      best = v;
    }
  }
  return best;
}

// The page's main cube once it shows a scramble or a case (not solved).
CubeView *waitForCase(QWidget *page) {
  CubeView *view = nullptr;
  bool shown = QTest::qWaitFor(
      [&] {
        view = mainCube(page);
        if (!view) {
          return false;
        }
        view->finishAnimations();
        return !view->displayedState().isSolved();
      },
      10000);
  return shown ? view : nullptr;
}

// Closes every modal dialog or message box that comes up while it lives,
// keeping what each said, so that none can block the test.
class ModalWatcher : public QObject {
public:
  explicit ModalWatcher(std::function<void(QWidget *)> onModal = {})
      : onModal_(std::move(onModal)) {
    connect(&timer_, &QTimer::timeout, this, [this] {
      if (QWidget *m = QApplication::activeModalWidget()) {
        seen.append(m->metaObject()->className());
        if (auto *box = qobject_cast<QMessageBox *>(m)) {
          texts.append(box->text());
        }
        if (onModal_) {
          onModal_(m);
        }
        m->close();
      }
    });
    timer_.start(20);
  }

  QStringList seen;
  QStringList texts;

private:
  QTimer timer_;
  std::function<void(QWidget *)> onModal_;
};

} // namespace

class GuiTest : public QObject {
  Q_OBJECT

  AppSettings settings_;
  QString dataDir_;
  std::unique_ptr<SingleInstance> instance_;
  std::unique_ptr<MainWindow> window_;

  template <typename Page> Page *page() {
    auto *p = window_->findChild<Page *>();
    if (!p) {
      qFatal("page not found");
    }
    return p;
  }

  // Switches mode with its shortcut, as the user does.
  bool switchTo(int mode, ModePage *expected) {
    QTest::keySequence(window_.get(), AppMode::shortcut(mode));
    return QTest::qWaitFor([&] { return shownPage(*window_) == expected; },
                           5000);
  }

  // Types the moves with the default (cstimer) turning keys.
  void typeMoves(const Alg &moves) {
    for (Move m : moves) {
      Move quarter = m;
      int times = 1;
      if (m.amount == 2) {
        quarter.amount = 1;
        times = 2;
      }
      quarter.quarters = 0;
      const KeyBinding *binding = nullptr;
      for (const KeyBinding &b : settings_.keymap.bindings) {
        if (b.move == quarter) {
          binding = &b;
        }
      }
      QVERIFY2(binding, qPrintable(qs(toString(Alg{m}))));
      for (int i = 0; i < times; ++i) {
        QTest::keyClick(window_.get(), static_cast<Qt::Key>(binding->key),
                        binding->shift ? Qt::ShiftModifier : Qt::NoModifier);
      }
    }
  }

  // Space held past the hold time and let go.
  void holdSpace() {
    QTest::keyPress(window_.get(), Qt::Key_Space);
    QTest::qWait(settings_.holdMs + 150);
    QTest::keyRelease(window_.get(), Qt::Key_Space);
  }

  SetProgress savedProgress(const std::string &setId) {
    finishBackgroundWrites();
    AlgLibrary lib(QDir(dataDir_).filePath("algsets"), ":/algsets");
    lib.load();
    return lib.progress(setId);
  }

  std::vector<std::string> savedAlgs(const std::string &setId,
                                     const std::string &caseId) {
    finishBackgroundWrites();
    AlgLibrary lib(QDir(dataDir_).filePath("algsets"), ":/algsets");
    lib.load();
    AlgSet *set = lib.find(setId);
    const AlgCase *c = set ? set->findCase(caseId) : nullptr;
    return c ? c->algs : std::vector<std::string>{};
  }

private slots:
  void initTestCase();
  void modesSwitch();
  void timedPhysicalSolve();
  void untimedVirtualSolve();
  void learnStartsACase();
  void drillArmsAndStopsItself();
  void libraryAddsAnAlgAndUndoes();
  void algLabTimesEachAlg();
  void fingertrickGuidance();
  void settingsDialogOpensAndCloses();
  void helpPanelShowsTheKeys();
  void keysDocMatchesTheKeyTable();
  void cleanupTestCase();
};

void GuiTest::initTestCase() {
  // Everything the app could read or write is in the temporary folder.
  QVERIFY(tempRoot && tempRoot->isValid());
  QVERIFY(isUnderTemp(QSettings().fileName()));
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
  QVERIFY(isUnderTemp(
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)));
  QVERIFY(isUnderTemp(
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)));
  QVERIFY(isUnderTemp(
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
  dataDir_ = QDir(QStandardPaths::writableLocation(
                      QStandardPaths::GenericDataLocation))
                 .filePath("v-cube");
#else
  // Elsewhere the standard folders don't follow XDG_*: the data folder is
  // given directly.
  dataDir_ = tempPath("data/v-cube");
#endif
  QVERIFY(isUnderTemp(dataDir_));
  QVERIFY(QDir().mkpath(dataDir_));
  QVERIFY(QDir(dataDir_).isEmpty());

  // The single-instance lock lives in the (temporary) data folder.
  instance_ = std::make_unique<SingleInstance>(dataDir_);
  QVERIFY(instance_->claim() == SingleInstance::Claim::First);

  // T perm has an alg, done without a walkthrough; the timer's session is
  // for a physical cube.
  {
    AlgLibrary lib(QDir(dataDir_).filePath("algsets"), ":/algsets");
    lib.load();
    QCOMPARE(lib.errors(), QStringList{});
    AlgSet *pll = lib.find("pll");
    QVERIFY(pll);
    QVERIFY(pll->findCase("T"));
    pll->findCase("T")->algs = {kTPerm};
    pll->guided = false;
    QVERIFY(lib.save(*pll));
    SessionStore sessions(QDir(dataDir_).filePath("sessions.json"));
    QVERIFY(sessions.load());
    sessions.sessions().front().cube = CubeKind::Physical;
    QVERIFY(sessions.save());
  }

  TwoPhaseSolver::warmUp(); // so that scrambles come quickly
  settings_ = AppSettings::load();
  applyTheme(*qApp, settings_.darkTheme);
  {
    ModalWatcher watcher;
    window_ = std::make_unique<MainWindow>(settings_, dataDir_);
    QCOMPARE(watcher.texts, QStringList{});
  }
  window_->show();
  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  QVERIFY(shownPage(*window_));
}

void GuiTest::modesSwitch() {
  const std::array<ModePage *, AppMode::Count> pages = {
      page<LearnPage>(),      page<DrillPage>(),   page<LibraryPage>(),
      page<TimerPage>(),      page<UntimedPage>(), page<MoveCountPage>(),
      page<InspectionPage>(), page<BlindPage>(),   page<AlgLabPage>(),
      page<TodayPage>()};
  for (int m : AppMode::sidebarOrder()) {
    QVERIFY2(switchTo(m, pages[static_cast<std::size_t>(m)]),
             qPrintable(AppMode::label(m)));
    QVERIFY(pages[static_cast<std::size_t>(m)]->isVisible());
  }
  // And back to the first, from the last.
  QVERIFY(switchTo(AppMode::Learn, pages[AppMode::Learn]));
}

void GuiTest::algLabTimesEachAlg() {
  auto *lab = page<AlgLabPage>();
  QVERIFY(switchTo(AppMode::AlgLab, lab));
  lab->selectCase("pll", "T");
  QTableWidget *candidates = nullptr;
  QTabWidget *tabs = lab->findChild<QTabWidget *>();
  QVERIFY(tabs);
  tabs->setCurrentIndex(0);
  for (QTableWidget *t : lab->findChildren<QTableWidget *>()) {
    if (t->columnCount() > 10 && t->horizontalHeaderItem(1) &&
        t->horizontalHeaderItem(1)->text() == "HTM") {
      candidates = t;
    }
  }
  QVERIFY(candidates);
  // The set's alg is a candidate.
  QCOMPARE(candidates->rowCount(), 1);
  QCOMPARE(candidates->item(0, 0)->data(Qt::UserRole).toString(),
           qs(labAlgKey(kTPerm)));

  // Another T perm, typed in.
  const std::string other = "R2 F2 U R2 U2 F' U F' R2 F U' F' U F2";
  QLineEdit *add = nullptr;
  for (QLineEdit *e : lab->findChildren<QLineEdit *>()) {
    if (e->placeholderText().startsWith("Type an alg")) {
      add = e;
    }
  }
  QVERIFY(add);
  add->setText(qs(other));
  QMetaObject::invokeMethod(add, "returnPressed");
  QCOMPARE(candidates->rowCount(), 2);

  // Both into the session, then timed on a physical cube.
  candidates->selectAll();
  QPushButton *toSession = nullptr;
  for (QPushButton *b : lab->findChildren<QPushButton *>()) {
    if (b->text() == "Add to session") {
      toSession = b;
    }
  }
  QVERIFY(toSession);
  toSession->click();
  tabs->setCurrentIndex(1);
  // No warm-up reps, so that every time counts.
  for (QSpinBox *box : lab->findChildren<QSpinBox *>()) {
    if (box->prefix().startsWith("warm-up")) {
      box->setValue(0);
    }
  }
  QTest::keyClick(window_.get(), Qt::Key_Space); // starts the session
  holdSpace();
  QVERIFY(lab->isBusy());
  QTest::qWait(350);
  QTest::keyClick(window_.get(), Qt::Key_A);
  QVERIFY(!lab->isBusy());
  finishBackgroundWrites();
  {
    AlgLabStore saved(QDir(dataDir_).filePath("alglab.json"));
    QVERIFY(saved.load());
    QCOMPARE(saved.current().entries.size(), std::size_t{2});
    QCOMPARE(saved.attempts().size(), std::size_t{1});
    QVERIFY(saved.attempts()[0].cube == CubeKind::Physical);
    QVERIFY(saved.attempts()[0].timeMs >= 300);
    QVERIFY(!saved.attempts()[0].scramble.empty());
  }

  // The virtual cube: the next alg is set up on it, done with that alg.
  QComboBox *cube = nullptr;
  for (QComboBox *box : lab->findChildren<QComboBox *>()) {
    if (box->findText("Virtual cube") >= 0) {
      cube = box;
    }
  }
  QVERIFY(cube);
  QTest::qWait(400);
  QTest::keyClick(window_.get(), Qt::Key_Space); // the next alg
  cube->setCurrentIndex(cube->findText("Virtual cube"));
  QTest::keyClick(window_.get(), Qt::Key_Escape); // set up on the cube
  CubeView *view = nullptr;
  for (CubeView *v : lab->findChildren<CubeView *>()) {
    if (v->isVisible()) {
      view = v;
    }
  }
  QVERIFY(view);
  view->finishAnimations();
  // The alg the session asks for, as the page shows it.
  std::string asked;
  for (QLabel *l : lab->findChildren<QLabel *>()) {
    for (const std::string &a : {std::string(kTPerm), other}) {
      if (l->isVisible() && l->text() == qs(a)) {
        asked = a;
      }
    }
  }
  QVERIFY(!asked.empty());
  std::optional<Alg> moves =
      withAufs(view->displayedState(), parseAlgOrEmpty(asked));
  QVERIFY2(moves, "the case shown isn't T perm");
  typeMoves(*moves);
  QTRY_VERIFY(!lab->isBusy());
  finishBackgroundWrites();
  AlgLabStore saved(QDir(dataDir_).filePath("alglab.json"));
  QVERIFY(saved.load());
  QCOMPARE(saved.attempts().size(), std::size_t{2});
  const LabAttempt &v = saved.attempts()[1];
  QVERIFY(v.cube == CubeKind::Virtual);
  QVERIFY(!v.dnf);
  QVERIFY(!v.deviated);
  QVERIFY(!v.moves.empty());

  // Library shows the times beside the set's alg.
  auto *library = page<LibraryPage>();
  QVERIFY(switchTo(AppMode::Library, library));
  library->selectCase("pll", "T");
  auto *algs = library->findChild<AlgListWidget *>();
  QVERIFY(algs);
  QVERIFY(algs->topLevelItemCount() >= 1);
  // Interleaved, each alg was timed once: the set's on one of the cubes.
  QVERIFY(algs->topLevelItem(0)->text(2).contains("mean of 1"));
}

void GuiTest::fingertrickGuidance() {
  auto *lab = page<AlgLabPage>();
  QVERIFY(switchTo(AppMode::AlgLab, lab));
  lab->selectCase("pll", "T");
  lab->findChild<QTabWidget *>()->setCurrentIndex(0);
  QTableWidget *candidates = nullptr;
  for (QTableWidget *t : lab->findChildren<QTableWidget *>()) {
    if (t->columnCount() > 10 && t->horizontalHeaderItem(1) &&
        t->horizontalHeaderItem(1)->text() == "HTM") {
      candidates = t;
    }
  }
  QVERIFY(candidates);
  // The default score is the grip profile's.
  QComboBox *profiles = nullptr;
  for (QComboBox *b : lab->findChildren<QComboBox *>()) {
    if (b->findText(qs(std::string(kGripProfileName))) >= 0) {
      profiles = b;
    }
  }
  QVERIFY(profiles);
  QCOMPARE(profiles->currentText(), qs(std::string(kGripProfileName)));
  GripModel model = standardGripModel();
  double best = gripCost(parseAlgOrEmpty(kTPerm), model);
  QCOMPARE(candidates->item(0, 5)->text(), QString::number(best, 'f', 1));

  // Selecting the alg shows how to turn it: a column per move.
  auto *view = lab->findChild<GripView *>();
  QVERIFY(view);
  candidates->selectRow(0);
  QTableWidget *lanes = nullptr;
  for (QTableWidget *t : view->findChildren<QTableWidget *>()) {
    lanes = t;
  }
  QVERIFY(lanes);
  QCOMPARE(lanes->columnCount(), 14);
  QCOMPARE(lanes->item(0, 5)->text(), QString("F"));
  QCOMPARE(lanes->item(1, 5)->text(), QString("index push from the top"));
  QLabel *summary = view->findChild<QLabel *>();
  QVERIFY(summary->text().contains("Start:"));
  QVERIFY(!summary->text().contains("your picks"));

  // Another way to turn F: the plan follows, and the cost shows.
  lanes->selectColumn(5);
  auto *options = view->findChild<QListWidget *>();
  QVERIFY(options->count() >= 2);
  QVERIFY(options->item(0)->text().startsWith("✓"));
  options->setCurrentRow(1);
  emit options->itemClicked(options->item(1));
  QVERIFY(summary->text().contains("your picks"));
  QVERIFY(lanes->item(1, 5)->text() != "index push from the top" ||
          lanes->item(2, 5)->text() != "");

  // Library shows the same guidance for the selected alg.
  auto *library = page<LibraryPage>();
  QVERIFY(switchTo(AppMode::Library, library));
  library->selectCase("pll", "T");
  auto *algs = library->findChild<AlgListWidget *>();
  algs->setCurrentItem(algs->topLevelItem(0));
  auto *libView = library->findChild<GripView *>();
  QVERIFY(libView && libView->isVisible());
  QVERIFY(libView->findChild<QLabel *>()->text().contains("Start:"));
  QCOMPARE(libView->findChild<QTableWidget *>()->columnCount(), 14);
}

void GuiTest::timedPhysicalSolve() {
  auto *timed = page<TimerPage>();
  QVERIFY(switchTo(AppMode::Timed, timed));
  // Let go too early: nothing starts.
  QTest::keyPress(window_.get(), Qt::Key_Space);
  QTest::keyRelease(window_.get(), Qt::Key_Space);
  QVERIFY(!timed->isBusy());

  // Held and let go (again, if the first scramble wasn't ready yet).
  QVERIFY(QTest::qWaitFor(
      [&] {
        holdSpace();
        return timed->isBusy();
      },
      10000));
  // Running: the mode can't be left from the Mode menu (a key would stop
  // the timer first).
  QVERIFY(timed->isBusy());
  QAction *learnMode = nullptr;
  for (QAction *a : window_->findChildren<QAction *>()) {
    if (a->shortcut() == AppMode::shortcut(AppMode::Learn)) {
      learnMode = a;
    }
  }
  QVERIFY(learnMode);
  learnMode->trigger();
  QCOMPARE(shownPage(*window_), timed);
  QVERIFY(timed->isBusy());
  QTest::qWait(400);
  QTest::keyClick(window_.get(), Qt::Key_A); // any key stops it
  QVERIFY(!timed->isBusy());

  finishBackgroundWrites();
  SessionStore saved(QDir(dataDir_).filePath("sessions.json"));
  QVERIFY(saved.load());
  const Session &s = saved.sessions().front();
  QVERIFY(s.cube == CubeKind::Physical);
  QCOMPARE(s.solves.size(), std::size_t{1});
  QVERIFY(s.solves[0].timeMs >= 300);
  QVERIFY(s.solves[0].penalty == Penalty::None);
  QVERIFY(!s.solves[0].scramble.empty());
  QVERIFY(s.solves[0].moves.empty());
}

void GuiTest::untimedVirtualSolve() {
  auto *untimed = page<UntimedPage>();
  QVERIFY(switchTo(AppMode::Untimed, untimed));
  CubeView *view = waitForCase(untimed);
  QVERIFY(view);
  std::optional<Alg> solution = solutionFor(view->displayedState());
  QVERIFY(solution);
  QVERIFY(!solution->empty());

  // Face turns don't turn the cube before the solve starts.
  FaceletCube before = view->displayedState();
  typeMoves({Move{MoveType::R, 1}});
  view->finishAnimations();
  QCOMPARE(view->displayedState(), before);

  holdSpace();
  QVERIFY(untimed->isBusy());
  typeMoves(*solution);
  view->finishAnimations();
  QVERIFY(view->displayedState().isSolved());
  // Auto stop: solved ends the solve.
  QTRY_VERIFY(!untimed->isBusy());

  finishBackgroundWrites();
  MoveCountStore saved(QDir(dataDir_).filePath("untimed.json"),
                       SolveLog::Untimed);
  QVERIFY(saved.load());
  const MoveCountSession &s = saved.sessions().front();
  QVERIFY(s.cube == CubeKind::Virtual);
  QCOMPARE(s.attempts.size(), std::size_t{1});
  QVERIFY(!s.attempts[0].dnf);
  QCOMPARE(s.attempts[0].moves, static_cast<int>(solution->size()));
  QCOMPARE(s.attempts[0].solution, toString(*solution));
}

void GuiTest::learnStartsACase() {
  auto *learn = page<LearnPage>();
  QVERIFY(switchTo(AppMode::Learn, learn));
  // Nothing is due until a case is put in the learn queue.
  QPushButton *start = nullptr;
  for (QPushButton *b : learn->findChildren<QPushButton *>()) {
    if (b->text() == "Start learning") {
      start = b;
    }
  }
  QVERIFY(start);
  QVERIFY(start->isVisible());
  start->click();
  // The one case with an alg is set up on the cube.
  CubeView *view = waitForCase(learn);
  QVERIFY(view);
  std::optional<Alg> moves =
      withAufs(view->displayedState(), parseAlgOrEmpty(kTPerm));
  QVERIFY2(moves, "the case shown isn't T perm");
  typeMoves(*moves);
  view->finishAnimations();
  QVERIFY(view->displayedState().isSolved());

  // Solved with its alg and no hint: graded and saved.
  QTRY_VERIFY_WITH_TIMEOUT(
      [&] {
        SetProgress p = savedProgress("pll");
        return p.contains("T") && p["T"].card.reps > 0;
      }(),
      5000);
  SetProgress p = savedProgress("pll");
  QVERIFY(!p["T"].attempts.empty());
  QVERIFY(!p["T"].attempts.back().failed);
  QVERIFY(p["T"].attempts.back().mode == AttemptMode::Learn);
}

void GuiTest::drillArmsAndStopsItself() {
  auto *drill = page<DrillPage>();
  QVERIFY(switchTo(AppMode::Drill, drill));
  // Drill every case, not only those learned in Learn.
  QComboBox *pool = nullptr;
  for (QComboBox *box : drill->findChildren<QComboBox *>()) {
    if (box->findText("Being learned") >= 0) {
      pool = box;
    }
  }
  QVERIFY(pool);
  int all = pool->findText("All");
  QVERIFY(all >= 0);
  pool->setCurrentIndex(all);
  emit pool->activated(all);
  CubeView *view = waitForCase(drill);
  QVERIFY(view);
  std::optional<Alg> moves =
      withAufs(view->displayedState(), parseAlgOrEmpty(kTPerm));
  QVERIFY2(moves, "the case shown isn't T perm");
  // A tap of Space arms the timer; the first face turn starts it.
  QTest::keyClick(window_.get(), Qt::Key_Space);
  QVERIFY(!drill->isBusy());
  typeMoves(*moves);
  // The step is done: the timer stopped by itself.
  QTRY_VERIFY(!drill->isBusy());
  SetProgress p = savedProgress("pll");
  QCOMPARE(p["T"].drillCount(AttemptMode::VirtualDrill), std::size_t{1});
  QCOMPARE(p["T"].failRate(AttemptMode::VirtualDrill), 0.0);
}

void GuiTest::libraryAddsAnAlgAndUndoes() {
  auto *library = page<LibraryPage>();
  QVERIFY(switchTo(AppMode::Library, library));
  library->selectCase("pll", "Ua");
  QLineEdit *edit = nullptr;
  for (QLineEdit *e : library->findChildren<QLineEdit *>()) {
    if (e->placeholderText().startsWith("New alg")) {
      edit = e;
    }
  }
  QVERIFY(edit);
  QVERIFY(edit->isEnabled());
  const std::string ua = "R U' R U R U R U' R' U' R2";
  QVERIFY(savedAlgs("pll", "Ua").empty());
  edit->setFocus();
  edit->setText(qs(ua));
  QTest::keyClick(edit, Qt::Key_Return);
  QCOMPARE(savedAlgs("pll", "Ua"), std::vector<std::string>{ua});
  QVERIFY(edit->text().isEmpty());

  // Ctrl+Z, with the focus back on the window.
  edit->clearFocus();
  window_->setFocus();
  QTest::keySequence(window_.get(), QKeySequence::Undo);
  QCOMPARE(savedAlgs("pll", "Ua"), std::vector<std::string>{});
  QTest::keySequence(window_.get(), QKeySequence::Redo);
  QCOMPARE(savedAlgs("pll", "Ua"), std::vector<std::string>{ua});
  QTest::keySequence(window_.get(), QKeySequence::Undo);
  QCOMPARE(savedAlgs("pll", "Ua"), std::vector<std::string>{});
  // The T perm alg was never touched.
  QCOMPARE(savedAlgs("pll", "T"), std::vector<std::string>{kTPerm});
}

void GuiTest::settingsDialogOpensAndCloses() {
  QVERIFY(switchTo(AppMode::Timed, page<TimerPage>()));
  bool settingsShown = false;
  {
    ModalWatcher watcher([&](QWidget *m) {
      settingsShown = settingsShown || qobject_cast<SettingsDialog *>(m);
    });
    QTest::keySequence(window_.get(), QKeySequence(Qt::CTRL | Qt::Key_Comma));
    QTRY_VERIFY(settingsShown);
    QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(watcher.seen, QStringList{"vcube::SettingsDialog"});
  }
  // The window has the keyboard again.
  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
  // Closed without OK: the settings are as they were.
  QCOMPARE(settings_.holdMs, AppSettings{}.holdMs);
  QCOMPARE(shownPage(*window_), page<TimerPage>());
}

void GuiTest::helpPanelShowsTheKeys() {
  auto *help = page<HelpPanel>();
  QVERIFY(!help->isVisible());
  QTest::keyClick(window_.get(), Qt::Key_F1);
  QTRY_VERIFY(help->isVisible());
  auto *text = help->findChild<QTextBrowser *>();
  QVERIFY(text);
  QString timed = text->toPlainText();
  QVERIFY(timed.contains("Space"));
  // It follows the mode.
  QVERIFY(switchTo(AppMode::Library, page<LibraryPage>()));
  QTRY_VERIFY(text->toPlainText() != timed);
  QTest::keyClick(window_.get(), Qt::Key_F1);
  QTRY_VERIFY(!help->isVisible());
}

// docs/keys.md is written from the key table; VCUBE_WRITE_DOCS=1 writes
// it again after the table changed.
void GuiTest::keysDocMatchesTheKeyTable() {
  QFile file(QStringLiteral(VCUBE_SOURCE_DIR "/docs/keys.md"));
  QString expected = controlsMarkdown();
  if (qEnvironmentVariableIsSet("VCUBE_WRITE_DOCS")) {
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(expected.toUtf8());
    return;
  }
  QVERIFY(file.open(QIODevice::ReadOnly));
  QString actual = QString::fromUtf8(file.readAll()).remove('\r');
  QVERIFY2(actual == expected,
           "docs/keys.md is out of date: run the GUI tests once with "
           "VCUBE_WRITE_DOCS=1");
}

void GuiTest::cleanupTestCase() {
  if (window_) {
    window_->close();
    window_.reset();
  }
  instance_.reset();
  QThreadPool::globalInstance()->waitForDone();
  finishBackgroundWrites();
}

int main(int argc, char **argv) {
  // Before anything reads a path: the offscreen platform, and settings,
  // data and cache in a temporary folder. XDG_* folders must be absolute,
  // or Qt falls back to the user's own.
  QTemporaryDir root;
  if (!root.isValid()) {
    qFatal("no temporary folder");
  }
  tempRoot = &root;
  const QByteArray base = QDir(root.path()).absolutePath().toLocal8Bit();
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("XDG_CONFIG_HOME", base + "/config");
  qputenv("XDG_DATA_HOME", base + "/data");
  qputenv("XDG_CACHE_HOME", base + "/cache");
  qputenv("XDG_STATE_HOME", base + "/state");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     QString::fromLocal8Bit(base + "/config"));
#ifdef VCUBE_QT_PLUGIN_DIR
  // Where the platform plugins are when Qt's libraries were copied next
  // to the test (Windows).
  QCoreApplication::addLibraryPath(QStringLiteral(VCUBE_QT_PLUGIN_DIR));
#endif

  QApplication app(argc, argv);
  QApplication::setOrganizationName("v-cube");
  QApplication::setApplicationName("v-cube");
  GuiTest test;
  int result = QTest::qExec(&test, argc, argv);
  tempRoot = nullptr;
  return result;
}

#include "test_gui.moc"
