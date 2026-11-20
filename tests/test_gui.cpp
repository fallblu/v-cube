// The app's pages driven through the main window on the offscreen platform:
// keys go in as QTest events and what was saved is read back from the data
// folder. The settings and data folders are temporary ones, set before the
// QApplication exists, so the user's own data is never read or written.
#include "app/alglab_page.hpp"
#include "app/app_settings.hpp"
#include "app/colours.hpp"
#include "app/command_palette.hpp"
#include "app/cube_painter.hpp"
#include "app/cube_view.hpp"
#include "app/drill_page.hpp"
#include "app/eo_cross_page.hpp"
#include "app/f2l_page.hpp"
#include "app/grip_view.hpp"
#include "app/help_panel.hpp"
#include "app/help_text.hpp"
#include "app/learn_page.hpp"
#include "app/library_page.hpp"
#include "app/main_window.hpp"
#include "app/mode_table.hpp"
#include "app/move_count_page.hpp"
#include "app/page_parts.hpp"
#include "app/practice_html.hpp"
#include "app/practice_keys.hpp"
#include "app/quiz_practice.hpp"
#include "app/review_dialog.hpp"
#include "app/settings_dialog.hpp"
#include "app/single_instance.hpp"
#include "app/text_style.hpp"
#include "app/timed_driver.hpp"
#include "app/timer_page.hpp"
#include "app/today_page.hpp"
#include "app/train_keys.hpp"
#include "app/tsle_page.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "app/untimed_page.hpp"
#include "app/widgets.hpp"
#include "cube/solver.hpp"
#include "io/alglab_store.hpp"
#include "io/data_file.hpp"
#include "io/library.hpp"
#include "io/move_counts.hpp"
#include "io/practice_store.hpp"
#include "io/sessions.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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

// The offscreen platform prints this for every window that is shown, tens
// of times a run, which buries a real failure.
QtMessageHandler testMessageHandler = nullptr;

void dropNoise(QtMsgType type, const QMessageLogContext &context,
               const QString &message) {
  if (message.contains(
          QLatin1String("does not support propagateSizeHints()"))) {
    return;
  }
  if (testMessageHandler) {
    testMessageHandler(type, context, message);
  }
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

// The last child of `parent` of type W that `pred` holds for, or null.
template <typename W, typename Pred>
W *lastChild(const QObject *parent, Pred pred) {
  W *found = nullptr;
  for (W *w : parent->findChildren<W *>()) {
    if (pred(w)) {
      found = w;
    }
  }
  return found;
}

// The keys a list of hints names.
QStringList keyNames(const QList<KeyHint> &hints) {
  QStringList names;
  for (const KeyHint &h : hints) {
    names << h.key;
  }
  return names;
}

// The rows under the headings of a practice page's list.
int caseRows(const QTreeWidget *tree) {
  int n = 0;
  for (int g = 0; g < tree->topLevelItemCount(); ++g) {
    n += tree->topLevelItem(g)->childCount();
  }
  return n;
}

// Writes a file, making its folder if needed; false if it can't be opened.
bool writeFile(const QString &path, const QByteArray &text) {
  QFile f(path);
  if (QDir().mkpath(QFileInfo(path).absolutePath()) &&
      f.open(QIODevice::WriteOnly)) {
    f.write(text);
    return true;
  }
  return false;
}

// Alg Lab's table of candidate algs.
QTableWidget *candidateTable(const QWidget *lab) {
  return lastChild<QTableWidget>(lab, [](QTableWidget *t) {
    return t->columnCount() > 10 && t->horizontalHeaderItem(1) &&
           t->horizontalHeaderItem(1)->text() == "HTM";
  });
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

// A key (and what is held with it) pressed on the window, then the phase
// the practice page is in.
#define KEY_TO(page, to, ...)                                                  \
  do {                                                                         \
    QTest::keyClick(window_.get(), __VA_ARGS__);                               \
    QCOMPARE((page)->phase(), Phase::to);                                      \
  } while (false)

// `var`, the page of type `Type`, switched to with its mode's shortcut.
#define OPEN_PAGE(var, Type, mode)                                             \
  auto *var = page<Type>();                                                    \
  QVERIFY(switchTo(AppMode::mode, var))

// That `text` contains `part`, saying what the text is if not.
#define VERIFY_CONTAINS(text, part)                                            \
  QVERIFY2((text).contains(part), qPrintable(text))

class GuiTest : public QObject {
  Q_OBJECT

  AppSettings settings_;
  QString dataDir_;
  std::unique_ptr<SingleInstance> instance_;
  std::unique_ptr<MainWindow> window_;

  using Phase = PracticePage::Phase;
  using Tab = PracticePage::Tab;

  template <typename Page> Page *page() {
    auto *p = window_->findChild<Page *>();
    if (!p) {
      qFatal("page not found");
    }
    return p;
  }

  // Switches mode with its shortcut, as the user does.
  bool switchTo(AppMode::Id mode, ModePage *expected) {
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

  // Space held past the hold time and let go, on the window or `target`.
  void holdSpace(QWidget *target = nullptr) {
    target = target ? target : window_.get();
    QTest::keyPress(target, Qt::Key_Space);
    QTest::qWait(settings_.holdMs + 150);
    QTest::keyRelease(target, Qt::Key_Space);
  }

  // Space held and let go until the page is busy (again, if the first
  // scramble wasn't ready yet).
  bool holdSpaceUntilBusy(const ModePage *page, QWidget *target = nullptr) {
    return QTest::qWaitFor(
        [&] {
          holdSpace(target);
          return page->isBusy();
        },
        10000);
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
  void oddSettingsAreSetRight();
  void modesSwitch();
  void layoutFitsHalfAScreen();
  void sidePanelHidesInANarrowWindow();
  void keyChipsAreClickable();
  void timedPhysicalSolve();
  void untimedVirtualSolve();
  void learnStartsACase();
  void quizAsksAmongLookAlikes();
  void drillArmsAndStopsItself();
  void libraryAddsAnAlgAndUndoes();
  void algLabTimesEachAlg();
  void fingertrickGuidance();
  void f2lLearnAndTimed();
  void tsleLearnAndTimed();
  void eoCrossLearnAndTimed();
  void practiceModesComeBackAsLeft();
  void practiceResultsAreWorded();
  void practiceKeysFollowThePhase();
  void themeColoursAreReadable();
  void pagesTakeTheKeyboard();
  void keysAreReadTheSameForEveryPage();
  void commandListGroupsAndRemembers();
  void settingsDialogOpensAndCloses();
  void interfaceTextSizeScalesTheText();
  void tipsShowInTheStatusBar();
  void trendChartNamesWhatItCharts();
  void trendChartHoverKeepsThePlot();
  void helpPanelShowsTheKeys();
  void floatingHelpPanelPassesKeysOn();
  void aDayStartsAfterAMissingMidnight();
  void spaceHoldEndsWhenTheWindowIsLeft();
  void aLockFromAnotherHostCanBeTakenOver();
  void keysDocMatchesTheKeyTable();
  void theSameKeyTwiceArrivesTwice();
  void undoneDeletionKeepsProgressUnderANewId();
  void timedDriverDoesWhatTheFlowAsks();
  void mouseOnlyControlsHaveAKey();
  void reviewButtonsHaveKeys();
  void saveWarningCanBeDismissedFromTheMenu();
  void timerSaysItsResult();
  void bareAltReachesTheMenuBar();
  // Last: a second window takes over the process's write reporter.
  void startupProblemsShowInOneWindow();
  void cleanupTestCase();
};

void GuiTest::initTestCase() {
  // After QTest's own handler is in place (qExec installs it), so that the
  // rest still goes to the test log.
  testMessageHandler = qInstallMessageHandler(dropNoise);
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

  // What an older version or an edit by hand could have left in the
  // settings (see oddSettingsAreSetRight()).
  {
    QSettings q;
    q.setValue("learn/set", "pll");
    q.setValue("trainer/sets/pll/top", 9);
    q.setValue("srs/lapseFactor", "nan");
    q.setValue("srs/easyDays", "nan");
    q.setValue("alglab/statsWindow", 99);
    q.setValue("alglab/lengthMode", -3);
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
  // Wide enough for the side panels, which a narrow window hides (the
  // offscreen screen is small).
  window_->resize(1280, 800);
  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  QVERIFY(shownPage(*window_));
}

// Values out of range in the settings file fall back to the defaults.
void GuiTest::oddSettingsAreSetRight() {
  // A top colour that isn't one, from before the holds were kept as text.
  QVERIFY(settings_.neutrality.main == kWcaHold);
  QVERIFY(settings_.neutrality.holds == std::vector<Hold>{kWcaHold});
  // "nan" is a number to QVariant.
  QCOMPARE(settings_.srs.lapseFactor, SrsConfig().lapseFactor);
  QCOMPARE(settings_.srs.easyDays, SrsConfig().easyDays);
  // A drop-down position that isn't there doesn't leave the box blank.
  int boxes = 0;
  for (QComboBox *box : page<AlgLabPage>()->findChildren<QComboBox *>()) {
    if (box->itemText(0) == "All time" || box->itemText(0) == "Shortest +") {
      QCOMPARE(box->currentIndex(), 0);
      ++boxes;
    }
  }
  QCOMPARE(boxes, 2);
}

void GuiTest::modesSwitch() {
  const std::array<ModePage *, AppMode::Count> pages = {
      page<LearnPage>(),   page<DrillPage>(),   page<LibraryPage>(),
      page<TimerPage>(),   page<UntimedPage>(), page<MoveCountPage>(),
      page<EoCrossPage>(), page<AlgLabPage>(),  page<F2LPage>(),
      page<TslePage>(),    page<TodayPage>()};
  for (AppMode::Id m : AppMode::sidebarOrder()) {
    QVERIFY2(switchTo(m, pages[static_cast<std::size_t>(m)]),
             qPrintable(AppMode::label(m)));
    QVERIFY(pages[static_cast<std::size_t>(m)]->isVisible());
  }
  // And back to the first, from the last.
  QVERIFY(switchTo(AppMode::Learn, pages[AppMode::Learn]));

  // Practice is EO Cross, F2L, TSLE, at the end of the sidebar.
  const auto &order = AppMode::sidebarOrder();
  QCOMPARE(order[order.size() - 3], AppMode::EoCross);
  QCOMPARE(order[order.size() - 2], AppMode::F2L);
  QCOMPARE(order[order.size() - 1], AppMode::Tsle);
  QCOMPARE(AppMode::sidebarHeader(AppMode::EoCross), QString("PRACTICE"));
  QCOMPARE(AppMode::shortcut(AppMode::EoCross),
           QKeySequence(Qt::CTRL | Qt::Key_8));
  QCOMPARE(AppMode::shortcut(AppMode::Tsle),
           QKeySequence(Qt::CTRL | Qt::Key_Minus));
  // A window last left in the old Inspection mode opens in EO Cross.
  QCOMPARE(AppMode::fromSettingsId("inspection"),
           std::optional<AppMode::Id>(AppMode::EoCross));
}

void GuiTest::layoutFitsHalfAScreen() {
  // Every mode fits a window of half a 1080p screen, and switching to it
  // doesn't resize the window.
  const QSize before = window_->size();
  window_->resize(950, 650);
  QTRY_COMPARE(window_->size(), QSize(950, 650));
  for (AppMode::Id m : AppMode::sidebarOrder()) {
    QTest::keySequence(window_.get(), AppMode::shortcut(m));
    QTest::qWait(20);
    QApplication::processEvents();
    const QSize min = window_->minimumSizeHint();
    QVERIFY2(min.width() <= 950 && min.height() <= 650,
             qPrintable(QString("%1: %2x%3")
                            .arg(AppMode::label(m))
                            .arg(min.width())
                            .arg(min.height())));
    QVERIFY2(window_->size() == QSize(950, 650), qPrintable(AppMode::label(m)));
    // Buttons and drop-downs are a comfortable target.
    ModePage *shown = shownPage(*window_);
    for (QWidget *w : shown->findChildren<QWidget *>()) {
      if ((qobject_cast<QPushButton *>(w) || qobject_cast<QComboBox *>(w)) &&
          w->isVisible()) {
        QVERIFY2(w->height() >= 28, qPrintable(AppMode::label(m) + ": " +
                                               w->metaObject()->className()));
      }
    }
  }
  window_->resize(before);
  QVERIFY(switchTo(AppMode::Learn, page<LearnPage>()));
}

void GuiTest::sidePanelHidesInANarrowWindow() {
  OPEN_PAGE(timer, TimerPage, Timed);
  const QSize before = window_->size();
  QWidget *panel = timer->sidePanel();
  QVERIFY(panel);
  QVERIFY(!timer->sideHidden());
  window_->resize(1600, 900);
  QTRY_VERIFY(panel->isVisible());
  // Narrow: the panel gives the stage its room, without being turned off.
  window_->resize(800, 600);
  QTRY_VERIFY(!panel->isVisible());
  QVERIFY(!timer->sideHidden());
  // F9 brings it back while narrow; wide again, it stays.
  QTest::keyClick(window_.get(), Qt::Key_F9);
  QTRY_VERIFY(panel->isVisible());
  QTest::keyClick(window_.get(), Qt::Key_F9);
  QTRY_VERIFY(!panel->isVisible());
  QVERIFY(!timer->sideHidden());
  window_->resize(1600, 900);
  QTRY_VERIFY(panel->isVisible());
  // Hidden by hand, it stays hidden however wide.
  QTest::keyClick(window_.get(), Qt::Key_F9);
  QTRY_VERIFY(!panel->isVisible());
  QVERIFY(timer->sideHidden());
  QTest::keyClick(window_.get(), Qt::Key_F9);
  QTRY_VERIFY(panel->isVisible());
  QVERIFY(!timer->sideHidden());
  window_->resize(before);
}

void GuiTest::keyChipsAreClickable() {
  // In a widget never shown, so that the main window keeps the keyboard.
  QWidget host;
  auto *strip = new KeyStrip(&host);
  int clicked = 0;
  strip->setKeys({{"Delete", "delete it", [&clicked] { ++clicked; }},
                  {"Esc", "try it again"}});
  QVERIFY(!strip->isHidden());
  // Only a key with an action is a link.
  QVERIFY(strip->text().contains("chip:0"));
  QVERIFY(!strip->text().contains("chip:1"));
  emit strip->linkActivated("chip:1");
  emit strip->linkActivated("chip:0");
  QTRY_COMPARE(clicked, 1);
  strip->setKeys({});
  QVERIFY(strip->isHidden());
}

void GuiTest::algLabTimesEachAlg() {
  OPEN_PAGE(lab, AlgLabPage, AlgLab);
  lab->selectCase("pll", "T");
  QTabWidget *tabs = lab->findChild<QTabWidget *>();
  QVERIFY(tabs);
  tabs->setCurrentIndex(0);
  QTableWidget *candidates = candidateTable(lab);
  QVERIFY(candidates);
  // The set's alg is a candidate.
  QCOMPARE(candidates->rowCount(), 1);
  QCOMPARE(candidates->item(0, 0)->data(Qt::UserRole).toString(),
           qs(labAlgKey(kTPerm)));

  // Another T perm, typed in.
  const std::string other = "R2 F2 U R2 U2 F' U F' R2 F U' F' U F2";
  auto *add = lastChild<QLineEdit>(lab, [](QLineEdit *e) {
    return e->placeholderText().startsWith("Type an alg");
  });
  QVERIFY(add);
  add->setText(qs(other));
  QMetaObject::invokeMethod(add, "returnPressed");
  QCOMPARE(candidates->rowCount(), 2);

  // Both into the session, then timed on a physical cube.
  candidates->selectAll();
  auto *toSession = lastChild<QPushButton>(
      lab, [](QPushButton *b) { return b->text() == "Add to session"; });
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
  auto *cube = lastChild<QComboBox>(
      lab, [](QComboBox *box) { return box->findText("Virtual cube") >= 0; });
  QVERIFY(cube);
  QTest::qWait(400);
  QTest::keyClick(window_.get(), Qt::Key_Space); // the next alg
  cube->setCurrentIndex(cube->findText("Virtual cube"));
  QTest::keyClick(window_.get(), Qt::Key_Escape); // set up on the cube
  auto *view =
      lastChild<CubeView>(lab, [](CubeView *v) { return v->isVisible(); });
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
  OPEN_PAGE(library, LibraryPage, Library);
  library->selectCase("pll", "T");
  auto *algs = library->findChild<AlgListWidget *>();
  QVERIFY(algs);
  QVERIFY(algs->topLevelItemCount() >= 1);
  // Interleaved, each alg was timed once: the set's on one of the cubes.
  QVERIFY(algs->topLevelItem(0)->text(2).contains("mean of 1"));
}

void GuiTest::fingertrickGuidance() {
  OPEN_PAGE(lab, AlgLabPage, AlgLab);
  lab->selectCase("pll", "T");
  lab->findChild<QTabWidget *>()->setCurrentIndex(0);
  QTableWidget *candidates = candidateTable(lab);
  QVERIFY(candidates);
  // The default score is the grip profile's.
  auto *profiles = lastChild<QComboBox>(
      lab, [](QComboBox *b) { return b->findText(qs(kGripProfileName)) >= 0; });
  QVERIFY(profiles);
  QCOMPARE(profiles->currentText(), qs(kGripProfileName));
  GripModel model = standardGripModel();
  double best = gripCost(parseAlgOrEmpty(kTPerm), model);
  QCOMPARE(candidates->item(0, 5)->text(), QString::number(best, 'f', 1));

  // Selecting the alg shows how to turn it: a column per move.
  auto *view = lab->findChild<GripView *>();
  QVERIFY(view);
  candidates->selectRow(0);
  auto *lanes =
      lastChild<QTableWidget>(view, [](QTableWidget *) { return true; });
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
  OPEN_PAGE(library, LibraryPage, Library);
  library->selectCase("pll", "T");
  auto *algs = library->findChild<AlgListWidget *>();
  algs->setCurrentItem(algs->topLevelItem(0));
  auto *libView = library->findChild<GripView *>();
  QVERIFY(libView && libView->isVisible());
  QVERIFY(libView->findChild<QLabel *>()->text().contains("Start:"));
  QCOMPARE(libView->findChild<QTableWidget *>()->columnCount(), 14);
}

void GuiTest::f2lLearnAndTimed() {
  OPEN_PAGE(f2l, F2LPage, F2L);
  // ZZ F2L - 1 leaving FR or FL, as a new install has it.
  QVERIFY(f2l->config().eo);
  QVERIFY(f2l->config().lastSlot);
  QVERIFY(f2l->config().leaves(Slot::FR) && f2l->config().leaves(Slot::FL));
  QVERIFY(!f2l->config().leaves(Slot::BL));
  auto shownCube = [f2l] {
    CubeView *view = f2l->findChild<CubeView *>();
    view->finishAnimations();
    return centredCubie(view->displayedState());
  };
  auto card = [f2l](const std::string &key) {
    return f2l->data().cards.at(key).card;
  };
  // A chosen solution as it is done from the state on the cube.
  auto chosenHere = [f2l](std::size_t i = 0) {
    std::vector<Alg> chosen = f2l->chosenSolutions(f2l->currentCard());
    std::optional<Alg> here;
    if (i < chosen.size()) {
      here = chosenFor(f2l->startState(), *f2l->target(), chosen[i],
                       f2l->config());
    }
    return here.value_or(Alg{});
  };

  // Learn: the first card is new, so its solutions are chosen first, from
  // the best ones for its side's own slot.
  QCOMPARE(f2l->tab(), Tab::Learn);
  QCOMPARE(f2l->phase(), Phase::Choose);
  const std::vector<std::string> pool = pairCardPool(Edges::Oriented);
  QCOMPARE(f2l->currentCard(), pool.front());
  QVERIFY(f2l->target());
  QVERIFY(sideOf(*f2l->target()) == PairSide::Front);
  QVERIFY(f2lBase(f2l->startState(), f2l->config()));
  QCOMPARE(cardKey(pairCardOf(f2l->startState(), *f2l->target())),
           pool.front());
  QVERIFY(f2l->promptHtml().contains("New case"));
  QTRY_VERIFY_WITH_TIMEOUT(f2l->candidatesReady(), 30000);
  QVERIFY(!f2l->candidates().empty());
  QVERIFY(f2l->candidates().size() <= F2LPage::kCandidates);
  // Nothing ticked: Enter doesn't do; turning keys don't turn.
  KEY_TO(f2l, Choose, Qt::Key_Return);
  typeMoves({Move{MoveType::R, 1}});
  QVERIFY(f2l->cube() == f2l->startCube());
  QTest::keyClick(window_.get(), Qt::Key_1);
  QVERIFY(f2l->candidates().front().ticked);
  KEY_TO(f2l, Demo, Qt::Key_Return);
  QCOMPARE(f2l->chosenSolutions(pool.front()).size(), std::size_t{1});
  // The demo is the solution chosen, as done from here.
  QVERIFY(!chosenHere().empty());
  QVERIFY(f2l->shownDemo() == chosenHere());
  QTest::keyClick(window_.get(), Qt::Key_Return);
  QVERIFY(f2l->cube() == f2l->startCube());
  typeMoves(chosenHere());
  QCOMPARE(f2l->phase(), Phase::Done);
  QVERIFY(!f2l->grade());
  QCOMPARE(card(pool.front()).state, CardState::Learning);
  QVERIFY(card(pool.front()).introduced != 0);
  // Two more come in (each chosen, then Space skips the showing); then no
  // more until fewer are in their first steps.
  for (int i = 1; i <= 2; ++i) {
    KEY_TO(f2l, Choose, Qt::Key_Space);
    QCOMPARE(f2l->currentCard(), pool[static_cast<std::size_t>(i)]);
    QTRY_VERIFY_WITH_TIMEOUT(f2l->candidatesReady(), 30000);
    QTest::keyClick(window_.get(), Qt::Key_1);
    KEY_TO(f2l, Demo, Qt::Key_Return);
    KEY_TO(f2l, Done, Qt::Key_Space);
  }
  QVERIFY(sideOf(*f2l->target()) == PairSide::Front ||
          cardFromKey(pool[1])->side == PairSide::Back);
  // Only shown, it has no try to judge when its solutions change.
  QVERIFY(f2l->verdictHtml().contains("Shown"));
  KEY_TO(f2l, Choose, Qt::Key_0);
  QTRY_VERIFY_WITH_TIMEOUT(f2l->candidatesReady(), 30000);
  KEY_TO(f2l, Done, Qt::Key_Return);
  VERIFY_CONTAINS(f2l->verdictHtml(), "Shown");
  QVERIFY(!f2l->offersToAdd());
  KEY_TO(f2l, Idle, Qt::Key_Space);
  VERIFY_CONTAINS(f2l->promptHtml(), "Nothing is due");

  // Space takes a learning card early. Solved with a chosen solution it is
  // Good; 1-4 change the grade.
  KEY_TO(f2l, Solving, Qt::Key_Space);
  QVERIFY(f2l->graded());
  const std::string key = f2l->currentCard();
  QCOMPARE(cardKey(pairCardOf(f2l->startState(), *f2l->target())), key);
  // The case isn't named while it is recognised.
  QVERIFY(!f2l->promptHtml().contains("New case"));
  typeMoves(chosenHere());
  QCOMPARE(f2l->phase(), Phase::Done);
  auto done = shownCube();
  QVERIFY(done);
  QVERIFY(slotSolved(*done, *f2l->target()));
  QVERIFY(edgesOriented(*done));
  QVERIFY(crossSolved(*done));
  QVERIFY(f2l->grade() == Grade::Good);
  QCOMPARE(card(key).reps, 1);
  VERIFY_CONTAINS(f2l->verdictHtml(), "Your solution");
  QVERIFY(f2l->verdictHtml().contains("Good"));
  QVERIFY(!f2l->offersToAdd());
  const std::size_t attempts = f2l->data().cards.at(key).attempts.size();
  QTest::keyClick(window_.get(), Qt::Key_1);
  QVERIFY(f2l->grade() == Grade::Again);
  QVERIFY(f2l->verdictHtml().contains("Again"));
  QVERIFY(f2l->data().cards.at(key).attempts.back().failed);
  QCOMPARE(f2l->data().cards.at(key).attempts.size(), attempts);
  QTest::keyClick(window_.get(), Qt::Key_3);
  QVERIFY(f2l->grade() == Grade::Good);

  // The details are behind Tab; Enter plays your solution.
  QVERIFY(!f2l->detailShown());
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  QVERIFY(f2l->detailShown());
  QVERIFY(f2l->detailHtml().contains("Your solutions"));
  QVERIFY(f2l->detailHtml().contains("play:"));
  QTest::keyClick(window_.get(), Qt::Key_Return);
  auto played = shownCube();
  QVERIFY(played && slotSolved(*played, *f2l->target()));

  // 0 changes the solutions: Esc leaves them as they were; one of your own
  // can be typed in (as done for the slot shown).
  KEY_TO(f2l, Choose, Qt::Key_0);
  QTRY_VERIFY_WITH_TIMEOUT(f2l->candidatesReady(), 30000);
  QVERIFY(std::any_of(f2l->candidates().begin(), f2l->candidates().end(),
                      [](const F2LPage::Candidate &c) { return c.ticked; }));
  QVERIFY(!f2l->addOwnSolution("D2").isEmpty()); // doesn't solve it
  {
    // A bad alg typed and entered keeps the field; Esc leaves it, and only
    // then is Esc the page's again.
    auto *own = f2l->findChild<QLineEdit *>("f2lOwnSolution");
    QVERIFY(own);
    QCOMPARE(own->accessibleName(), QString("Your own solution"));
    own->setFocus();
    QTRY_VERIFY(own->hasFocus());
    QTest::keyClicks(own, "D2");
    QTest::keyClick(own, Qt::Key_Return);
    QVERIFY(own->hasFocus());
    QTest::keyClick(own, Qt::Key_Escape);
    QVERIFY(!own->hasFocus());
    QCOMPARE(f2l->phase(), Phase::Choose);
    window_->setFocus();
  }
  KEY_TO(f2l, Done, Qt::Key_Escape);
  QCOMPARE(f2l->chosenSolutions(key).size(), std::size_t{1});
  QTRY_VERIFY_WITH_TIMEOUT(f2l->analysisReady(), 30000);
  std::optional<Alg> other;
  for (const RankedSolution &s : f2l->shownSolutions()) {
    if (comparableSolution(s.alg) != comparableSolution(chosenHere())) {
      other = s.alg;
      break;
    }
  }
  if (other) {
    KEY_TO(f2l, Choose, Qt::Key_0);
    QCOMPARE(f2l->addOwnSolution(qs(toString(*other))), QString());
    KEY_TO(f2l, Done, Qt::Key_Return);
    QCOMPARE(f2l->chosenSolutions(key).size(), std::size_t{2});
    QVERIFY(f2l->grade() == Grade::Good); // kept as it was
  }

  // The same state again isn't recorded or graded; a hint is in words.
  KEY_TO(f2l, Solving, Qt::Key_Escape);
  QVERIFY(!f2l->graded());
  QVERIFY(f2l->cube() == f2l->startCube());
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  // Under the cube, larger than the text around it.
  auto *hint = f2l->findChild<QLabel *>("practiceHint");
  QVERIFY(hint && hint->isVisible());
  QVERIFY2(hint->text().startsWith("Hint:"), qPrintable(hint->text()));
  QVERIFY(hint->font().pointSizeF() > QApplication::font().pointSizeF());
  QVERIFY(f2l->cube() == f2l->startCube());
  KEY_TO(f2l, Done, Qt::Key_Space, Qt::ShiftModifier);
  QCOMPARE(f2l->data().cards.at(key).attempts.size(), attempts);
  QCOMPARE(card(key).reps, 1);

  // A case picked from the list is practice: not graded, its solutions
  // chosen first when it has none.
  const std::string wanted =
      cardKey({PairSide::Back, pairCases(Edges::Oriented)[20]});
  f2l->practiseCard(wanted);
  QCOMPARE(f2l->phase(), Phase::Choose);
  QVERIFY(sideOf(*f2l->target()) == PairSide::Back);
  QVERIFY(cardKey(pairCardOf(f2l->startState(), *f2l->target())) == wanted);
  KEY_TO(f2l, Choose, Qt::Key_Escape); // nothing to go back to
  // One typed in while the candidates are still being searched for stays,
  // ticked, once they come.
  {
    AnalysisOptions own;
    own.config = f2l->config();
    own.profile = builtinErgoProfiles().front();
    const std::vector<RankedSolution> found =
        analysePair(f2l->startState(), *f2l->target(), own).solutions;
    QVERIFY(!found.empty());
    QCOMPARE(f2l->addOwnSolution(qs(toString(found.back().alg))), QString());
    const Alg typed = comparableSolution(sideToSlot(
        fixedFrame(found.back().alg), PairSide::Back, *f2l->target()));
    // The search's own come too (it finds more than one).
    QTRY_VERIFY_WITH_TIMEOUT(f2l->candidates().size() > 1, 30000);
    QVERIFY(std::any_of(f2l->candidates().begin(), f2l->candidates().end(),
                        [&typed](const F2LPage::Candidate &c) {
                          return c.ticked &&
                                 comparableSolution(c.solution.alg) == typed;
                        }));
    // Untick it again, to choose as before.
    for (std::size_t i = 0; i < f2l->candidates().size(); ++i) {
      if (comparableSolution(f2l->candidates()[i].solution.alg) == typed) {
        QTest::keyClick(window_.get(),
                        static_cast<Qt::Key>(Qt::Key_1 + static_cast<int>(i)));
      }
    }
  }
  QTRY_VERIFY_WITH_TIMEOUT(f2l->candidatesReady(), 30000);
  QTest::keyClick(window_.get(), Qt::Key_2);
  KEY_TO(f2l, Solving, Qt::Key_Return);
  QVERIFY(!f2l->graded());
  typeMoves(chosenHere());
  QCOMPARE(f2l->phase(), Phase::Done);
  QVERIFY(f2l->verdictHtml().contains("Your solution"));
  QVERIFY(isQueued(card(wanted)));

  // Untimed: the whole F2L, every slot open, done with a front slot left;
  // a line on each pair as it goes in.
  AnalysisOptions options;
  options.config = f2l->config();
  options.profile = builtinErgoProfiles().front();
  auto solveWhole = [&] {
    CubieCube state = f2l->startState();
    for (int algs = 0; !f2lDone(state, options.config); ++algs) {
      if (algs >= 4) {
        return false;
      }
      std::vector<PairRank> ranks = rankPairs(state, options);
      if (ranks.empty()) {
        return false;
      }
      const Alg &alg = ranks.front().pair.best()->alg;
      typeMoves(alg);
      if (algs == 0) {
        QTest::qWait(20); // a time for the splits
      }
      state.apply(alg);
    }
    // A front slot is left, unless an alg happened to solve a second slot
    // with its own and took the last one too.
    for (Slot left : unsolvedSlots(state)) {
      if (!f2l->config().leaves(left)) {
        return false;
      }
    }
    return unsolvedSlots(state).size() <= 1;
  };
  f2l->selectTab(Tab::Untimed);
  QCOMPARE(f2l->phase(), Phase::Solving);
  QVERIFY(!f2l->target());
  QVERIFY(f2l->currentCard().empty());
  QCOMPARE(unsolvedSlots(f2l->startState()).size(), std::size_t{4});
  {
    // The first pair in: its line, then what it was against the best.
    CubieCube state = f2l->startState();
    std::vector<PairRank> ranks = rankPairs(state, options);
    QVERIFY(!ranks.empty());
    typeMoves(ranks.front().pair.best()->alg);
    QCOMPARE(f2l->progressLines().size(), qsizetype{1});
    QTRY_VERIFY_WITH_TIMEOUT(!f2l->progressLines().front().contains("…"),
                             30000);
    // (An alg that happens to put a second pair in with its own is said
    // to do them together, with nothing to set it against.)
    QVERIFY2(f2l->progressLines().front().contains("its best") ||
                 f2l->progressLines().front().contains("together"),
             qPrintable(f2l->progressLines().front()));
    QTest::keyClick(window_.get(), Qt::Key_Escape); // from the start again
    QVERIFY(f2l->progressLines().isEmpty());
  }
  QVERIFY(solveWhole());
  QCOMPARE(f2l->phase(), Phase::Done);
  QCOMPARE(f2l->data().learn.at("whole").size(), std::size_t{1});
  QVERIFY(f2l->data().learn.at("whole").back().moves > 0);
  VERIFY_CONTAINS(f2l->verdictHtml(), "F2L in");
  QTRY_VERIFY_WITH_TIMEOUT(f2l->analysisReady(), 60000);
  QVERIFY(f2l->data().learn.at("whole").back().extra >= 0.0);
  QVERIFY(f2l->data().learn.at("whole").back().best > 0);
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  QVERIFY(f2l->detailHtml().contains("Best for that pair"));

  // Timed, as Drill: nothing turns but the whole cube until Space arms
  // the timer; the first face turn starts it and the F2L done stops it.
  // The result is about the time; the efficiency is behind Tab.
  f2l->selectTab(Tab::Timed);
  QCOMPARE(f2l->timedPhase(), DrillFlow::Phase::Waiting);
  QCOMPARE(unsolvedSlots(f2l->startState()).size(), std::size_t{4});
  typeMoves({Move{MoveType::R, 1}});
  QVERIFY(f2l->cube() == f2l->startCube());
  QVERIFY(!f2l->isBusy());
  QTest::keyClick(window_.get(), Qt::Key_Space);
  QVERIFY(solveWhole());
  QCOMPARE(f2l->timedPhase(), DrillFlow::Phase::Revealed);
  QVERIFY(!f2l->isBusy());
  VERIFY_CONTAINS(f2l->verdictHtml(), "TPS");
  QVERIFY(!f2l->verdictHtml().contains("cheapest"));
  QTest::qWait(DrillFlow::kGuardMs + 50); // keys right after are held back
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  VERIFY_CONTAINS(f2l->detailHtml(), "Where the time went");
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  const std::string timedGroup = timedKey(CubeKind::Virtual, "zz-1");
  QCOMPARE(f2l->data().timed.at(timedGroup).size(), std::size_t{1});
  QVERIFY(f2l->data().timed.at(timedGroup).back().ms > 0);
  QVERIFY(f2l->data().timed.at(timedGroup).back().moves > 0);
  QVERIFY(!f2l->data().timed.at(timedGroup).back().dnf);
  // Backspace makes it a DNF and back; Delete deletes it.
  QTest::keyClick(window_.get(), Qt::Key_Backspace);
  QVERIFY(f2l->data().timed.at(timedGroup).back().dnf);
  QTest::keyClick(window_.get(), Qt::Key_Backspace);
  QVERIFY(!f2l->data().timed.at(timedGroup).back().dnf);
  QTest::keyClick(window_.get(), Qt::Key_Delete);
  QVERIFY(!f2l->data().timed.contains(timedGroup));
  QTest::keyClick(window_.get(), Qt::Key_Space);
  QCOMPARE(f2l->timedPhase(), DrillFlow::Phase::Waiting);

  // On a physical cube: a scramble, Space held and let go, any key stops.
  f2l->setPhysical(true);
  QCOMPARE(f2l->timedPhase(), DrillFlow::Phase::Waiting);
  QVERIFY(f2l->cubeHidden());
  QVERIFY(!f2l->ticking()); // nothing to redraw while it waits
  QTRY_VERIFY_WITH_TIMEOUT(!f2l->scrambleText().isEmpty(), 30000);
  {
    // The scramble gives the state, held as the solve is.
    FaceletCube scrambled;
    scrambled.apply(parseAlgOrEmpty(f2l->scrambleText().toStdString()));
    QVERIFY(!scrambled.isSolved());
    QVERIFY(!stepReachedHolds(scrambled, SolveStep::EOCross).empty());
  }
  holdSpace();
  QCOMPARE(f2l->timedPhase(), DrillFlow::Phase::Running);
  QVERIFY(f2l->isBusy());
  QVERIFY(f2l->ticking());
  QTest::qWait(DrillFlow::kMinPhysicalMs + 100);
  QTest::keyClick(window_.get(), Qt::Key_A);
  QCOMPARE(f2l->timedPhase(), DrillFlow::Phase::Revealed);
  QVERIFY(!f2l->ticking());
  const std::string physicalGroup = timedKey(CubeKind::Physical, "zz-1");
  QCOMPARE(f2l->data().timed.at(physicalGroup).size(), std::size_t{1});
  QCOMPARE(f2l->data().timed.at(physicalGroup).back().moves, 0);
  QVERIFY(!f2l->cubeHidden()); // back, to play the best way on
  f2l->setPhysical(false);

  // CFOP has the cases with flipped edges too, each front and back.
  f2l->selectTab(Tab::Learn);
  F2LConfig cfop = f2l->config();
  applyMethod(cfop, F2LMethod::CFOP);
  f2l->setConfig(cfop);
  auto *tree = f2l->findChild<QTreeWidget *>("practiceList");
  QVERIFY(tree);
  QCOMPARE(caseRows(tree), 334);
  // A solution chosen with CFOP that breaks EO isn't one with ZZ: the case
  // is chosen for again there, and the CFOP one kept for CFOP.
  {
    auto quarterFB = [](const Alg &alg) {
      return std::any_of(alg.begin(), alg.end(), [](Move m) {
        return (m.type == MoveType::F || m.type == MoveType::B) &&
               m.amount != 2;
      });
    };
    AnalysisOptions wide;
    wide.config = f2l->config();
    wide.profile = builtinErgoProfiles().front();
    wide.keep = 100;
    std::string cfopKey;
    Alg breaking;
    for (std::size_t i = 40; i < pool.size() && cfopKey.empty(); ++i) {
      f2l->practiseCard(pool[i]);
      QCOMPARE(f2l->phase(), Phase::Choose);
      for (const RankedSolution &s :
           analysePair(f2l->startState(), *f2l->target(), wide).solutions) {
        if (quarterFB(s.alg)) {
          cfopKey = pool[i];
          breaking = s.alg;
          break;
        }
      }
    }
    QVERIFY(!cfopKey.empty());
    QTRY_VERIFY_WITH_TIMEOUT(f2l->candidates().size() > 1, 30000);
    for (std::size_t i = 0; i < f2l->candidates().size(); ++i) {
      if (f2l->candidates()[i].ticked) {
        QTest::keyClick(window_.get(),
                        static_cast<Qt::Key>(Qt::Key_1 + static_cast<int>(i)));
      }
    }
    QCOMPARE(f2l->addOwnSolution(qs(toString(breaking))), QString());
    KEY_TO(f2l, Solving, Qt::Key_Return);
    QCOMPARE(f2l->chosenSolutions(cfopKey).size(), std::size_t{1});
    QCOMPARE(f2l->usableSolutions().size(), std::size_t{1});

    F2LConfig zz = cfop;
    applyMethod(zz, F2LMethod::ZZMinus1);
    f2l->setConfig(zz);
    f2l->practiseCard(cfopKey);
    QCOMPARE(f2l->phase(), Phase::Choose);
    QVERIFY(f2l->usableSolutions().empty());
    QTRY_VERIFY_WITH_TIMEOUT(f2l->candidates().size() > 1, 30000);
    // Searched for again with ZZ's moves, not CFOP's kept.
    for (const F2LPage::Candidate &c : f2l->candidates()) {
      QVERIFY(!quarterFB(c.solution.alg));
      QVERIFY(!c.ticked);
    }
    QTest::keyClick(window_.get(), Qt::Key_1);
    KEY_TO(f2l, Solving, Qt::Key_Return);
    QCOMPARE(f2l->chosenSolutions(cfopKey).size(), std::size_t{2});
    QCOMPARE(f2l->usableSolutions().size(), std::size_t{1});

    f2l->setConfig(cfop);
    f2l->practiseCard(cfopKey);
    QCOMPARE(f2l->phase(), Phase::Solving);
    QCOMPARE(f2l->usableSolutions().size(), std::size_t{2});
  }
  applyMethod(cfop, F2LMethod::ZZMinus1);
  f2l->setConfig(cfop);

  // What was recorded is in the file.
  finishBackgroundWrites();
  PracticeStore saved(QDir(dataDir_).filePath("f2l.json"));
  QVERIFY(saved.load());
  QVERIFY(saved.data == f2l->data());
}

void GuiTest::tsleLearnAndTimed() {
  OPEN_PAGE(tsle, TslePage, Tsle);
  QVERIFY(tsle->lastSlots()[0] && tsle->lastSlots()[1]);
  auto options = [tsle] {
    return tsle->solver(tsle->slot()).options(tsle->startState());
  };
  // Types the first trigger that isn't good.
  auto typeNotGood = [&] {
    for (const TsleOption &o : options()) {
      if (!o.good) {
        typeMoves(o.alg);
        break;
      }
    }
  };

  // Learn: the cases come in fewest steps first; a new one is shown, and
  // done with a good trigger.
  QCOMPARE(tsle->tab(), Tab::Learn);
  QCOMPARE(tsle->phase(), Phase::Demo);
  const std::vector<std::string> pool = tsleCardPool();
  QCOMPARE(tsle->currentCard(), pool.front());
  QCOMPARE(tsleKey(tsleCase(tsle->startState())), pool.front());
  QVERIFY(tsle->shownDemo() == options().front().alg);
  // A trigger that isn't good doesn't do.
  typeNotGood();
  QCOMPARE(tsle->phase(), Phase::Demo);
  QTest::keyClick(window_.get(), Qt::Key_Escape);
  QVERIFY(tsle->cube() == tsle->startCube());
  typeMoves(options().front().alg);
  QCOMPARE(tsle->phase(), Phase::Done);
  QVERIFY(tsle->data().cards.at(pool.front()).card.introduced != 0);
  for (int i = 0; i < 2; ++i) {
    KEY_TO(tsle, Demo, Qt::Key_Space);
    QTest::keyClick(window_.get(), Qt::Key_Space);
  }
  KEY_TO(tsle, Idle, Qt::Key_Space);

  // The best trigger from the AUF at hand is Good...
  KEY_TO(tsle, Solving, Qt::Key_Space);
  QVERIFY(tsle->graded());
  std::string key = tsle->currentCard();
  QCOMPARE(tsleKey(tsleCase(tsle->startState())), key);
  typeMoves(options().front().alg);
  QCOMPARE(tsle->phase(), Phase::Done);
  QVERIFY(tsle->grade() == Grade::Good);
  VERIFY_CONTAINS(tsle->verdictHtml(), "best trigger");
  QVERIFY(tsle->bestSolution() == options().front().alg);
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  QVERIFY(tsle->detailHtml().contains("Every good trigger"));
  // ...and one that leaves no fewer steps is Again.
  QTest::keyClick(window_.get(), Qt::Key_Space);
  if (tsle->phase() == Phase::Idle) {
    QTest::keyClick(window_.get(), Qt::Key_Space);
  }
  QCOMPARE(tsle->phase(), Phase::Solving);
  key = tsle->currentCard();
  const CaseProgress before = tsle->data().cards.at(key);
  typeNotGood();
  QCOMPARE(tsle->phase(), Phase::Done);
  QVERIFY(tsle->grade() == Grade::Again);
  QVERIFY(tsle->verdictHtml().contains("Not a good trigger"));
  QVERIFY(tsle->data().cards.at(key).attempts.back().failed);
  // Ctrl+Z takes the grade back, as in Learn mode, and the case comes up
  // again to be done over.
  QTest::keyClick(window_.get(), Qt::Key_Z, Qt::ControlModifier);
  QVERIFY(tsle->data().cards.at(key) == before);
  QCOMPARE(tsle->currentCard(), key);
  QCOMPARE(tsle->phase(), Phase::Solving);
  QVERIFY(tsle->graded());
  typeNotGood();
  QVERIFY(tsle->grade() == Grade::Again);

  // Untimed: the whole step, a line on each trigger; the best way to the
  // end is as short as it, typed a step at a time.
  auto solveWhole = [&] {
    QVERIFY(!tsle->startState().done());
    bool first = true;
    for (const Alg &step :
         tsle->solver(tsle->slot()).bestPath(tsle->startState())) {
      typeMoves(step);
      if (first) {
        QTest::qWait(20);
        first = false;
      }
    }
  };
  tsle->selectTab(Tab::Untimed);
  QCOMPARE(tsle->phase(), Phase::Solving);
  QVERIFY(tsle->currentCard().empty());
  {
    const std::vector<Alg> path =
        tsle->solver(tsle->slot()).bestPath(tsle->startState());
    if (path.size() > 1) {
      typeMoves(path.front());
      QCOMPARE(tsle->progressLines().size(), qsizetype{1});
      QVERIFY2(tsle->progressLines().front().contains("trigger") ||
                   tsle->progressLines().front().contains("OCLL"),
               qPrintable(tsle->progressLines().front()));
      QTest::keyClick(window_.get(), Qt::Key_Escape);
      QVERIFY(tsle->progressLines().isEmpty());
    }
  }
  solveWhole();
  QCOMPARE(tsle->phase(), Phase::Done);
  QVERIFY(tsle->currentState() && tsle->currentState()->done());
  QCOMPARE(tsle->data().learn.at("whole").size(), std::size_t{1});
  QCOMPARE(tsle->data().learn.at("whole").back().extra, 0.0);
  VERIFY_CONTAINS(tsle->verdictHtml(), "as short as the best way");

  // Timed: the whole step, kept by the case it started from; its splits
  // behind Tab.
  tsle->selectTab(Tab::Timed);
  QCOMPARE(tsle->timedPhase(), DrillFlow::Phase::Waiting);
  const std::string group =
      timedKey(CubeKind::Virtual, tsleKey(tsleCase(tsle->startState())));
  QTest::keyClick(window_.get(), Qt::Key_Space);
  solveWhole();
  QCOMPARE(tsle->timedPhase(), DrillFlow::Phase::Revealed);
  QCOMPARE(tsle->data().timed.at(group).size(), std::size_t{1});
  QVERIFY(!tsle->data().timed.at(group).back().dnf);
  QCOMPARE(tsle->data().timed.at(group).back().extra, 0.0);
  QTest::qWait(DrillFlow::kGuardMs + 50);
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  VERIFY_CONTAINS(tsle->detailHtml(), "Where the time went");

  // The lists: 97 cards in Learn, the 104 starting cases in Timed.
  auto *tree = tsle->findChild<QTreeWidget *>("practiceList");
  QVERIFY(tree);
  QCOMPARE(caseRows(tree), 104);
  QCOMPARE(tree->columnCount(), 5); // with TPS
  tsle->selectTab(Tab::Learn);
  QCOMPARE(caseRows(tree), 97);

  finishBackgroundWrites();
  PracticeStore saved(QDir(dataDir_).filePath("tsle.json"));
  QVERIFY(saved.load());
  QVERIFY(saved.data == tsle->data());
}

void GuiTest::eoCrossLearnAndTimed() {
  OPEN_PAGE(eo, EoCrossPage, EoCross);
  const SolveStep step = SolveStep::EOCross;
  eo->setStep(step);
  const std::vector<Hold> holds = eo->allowedHolds();
  QCOMPARE(holds.size(), std::size_t{2}); // one cross, either EO axis
  auto tries = [](const PracticeTries &all, const char *group) {
    auto it = all.find(group);
    return it == all.end() ? std::size_t{0} : it->second.size();
  };
  // The best hold's optimal solution, typed from the hold shown.
  auto solveOptimally = [&](bool pause = false) {
    std::vector<HeldOptimum> ranked =
        solveStepHeld(eo->scrambled(), eo->step(), holds, 1);
    typeMoves(holdRotation(ranked.front().hold));
    const Alg &moves = ranked.front().optimum.solutions.front();
    for (std::size_t i = 0; i < moves.size(); ++i) {
      typeMoves({moves[i]});
      if (pause && i == 0) {
        QTest::qWait(20); // a time to measure
      }
    }
    return ranked.front().optimum.length;
  };

  // Inspection: nothing timed starts by itself. Space shows a state and
  // starts the clock; rotations look, the first face turn hides the cube.
  QCOMPARE(eo->tab(), Tab::Inspection);
  QCOMPARE(eo->inspectionLimit(), 15);
  QCOMPARE(eo->phase(), Phase::Ready);
  QVERIFY(eo->promptHtml().contains("Ready"));
  QVERIFY(!eo->ticking());
  KEY_TO(eo, Solving, Qt::Key_Space);
  QVERIFY(eo->inspecting());
  QVERIFY(eo->ticking());
  QCOMPARE(eo->shownHold(), settings_.neutrality.main);
  QVERIFY(!doneHold(eo->cube(), step, holds));
  typeMoves(parseAlgOrEmpty("y"));
  QVERIFY(eo->inspecting());
  QVERIFY(!eo->cubeHidden());
  typeMoves(parseAlgOrEmpty("y'"));
  QTest::qWait(30);
  int optimum = solveOptimally();
  QVERIFY(!eo->inspecting());
  QVERIFY(!eo->ticking());
  QVERIFY(eo->inspectedMs() > 0);
  QVERIFY(eo->cubeHidden());
  QCOMPARE(eo->phase(), Phase::Solving);
  KEY_TO(eo, Done, Qt::Key_Space);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QVERIFY(!eo->cubeHidden());
  QCOMPARE(tries(eo->data().inspection, "eocross"), std::size_t{1});
  QCOMPARE(eo->data().inspection.at("eocross").back().extra, 0.0);
  QCOMPARE(eo->data().inspection.at("eocross").back().moves, optimum);
  QVERIFY(eo->data().inspection.at("eocross").back().inspectMs > 0);
  QVERIFY(!eo->data().inspection.at("eocross").back().over);
  QVERIFY2(eo->verdictHtml().contains("optimal") &&
               eo->verdictHtml().contains("looked"),
           qPrintable(eo->verdictHtml()));
  // The next state comes at once; a plan that didn't do the step counts
  // as not done.
  KEY_TO(eo, Solving, Qt::Key_Space);
  QVERIFY(eo->inspecting());
  typeMoves(parseAlgOrEmpty("R"));
  QVERIFY(eo->cubeHidden());
  KEY_TO(eo, Done, Qt::Key_Space);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QVERIFY(eo->data().inspection.at("eocross").back().dnf);
  VERIFY_CONTAINS(eo->verdictHtml(), "Not done");
  // Esc while planning gives the same state again, not recorded.
  QTest::keyClick(window_.get(), Qt::Key_Space);
  CubieCube planned = eo->scrambled();
  typeMoves(parseAlgOrEmpty("R"));
  KEY_TO(eo, Solving, Qt::Key_Escape);
  QVERIFY(eo->inspecting());
  QVERIFY(!eo->cubeHidden());
  QVERIFY(eo->scrambled() == planned);
  // Backspace only says which key gives up (Drill's: Shift+Space)...
  auto *bar = eo->findChild<HintBar *>();
  QVERIFY(bar);
  QVERIFY(keyNames(bar->keys()).contains("Shift+Space"));
  KEY_TO(eo, Solving, Qt::Key_Backspace);
  VERIFY_CONTAINS(bar->message(), "Shift+Space");
  KEY_TO(eo, Done, Qt::Key_Space, Qt::ShiftModifier);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QCOMPARE(tries(eo->data().inspection, "eocross"), std::size_t{2});
  // ...and Delete takes a try just recorded back (a retry has none).
  auto hasDelete = [bar] { return keyNames(bar->keys()).contains("Delete"); };
  QVERIFY(!hasDelete());
  KEY_TO(eo, Solving, Qt::Key_Space);
  KEY_TO(eo, Done, Qt::Key_Space, Qt::ShiftModifier);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QCOMPARE(tries(eo->data().inspection, "eocross"), std::size_t{3});
  QVERIFY(eo->data().inspection.at("eocross").back().dnf);
  QVERIFY(hasDelete());
  QTest::keyClick(window_.get(), Qt::Key_Delete);
  QCOMPARE(tries(eo->data().inspection, "eocross"), std::size_t{2});
  QVERIFY(!hasDelete());
  QTest::keyClick(window_.get(), Qt::Key_Delete); // only the one just made
  QCOMPARE(tries(eo->data().inspection, "eocross"), std::size_t{2});
  auto *tree = eo->findChild<QTreeWidget *>("practiceList");
  QCOMPARE(tree->columnCount(), 5);

  // Untimed: a scramble to solve at once, in view.
  eo->selectTab(Tab::Untimed);
  QCOMPARE(eo->phase(), Phase::Solving);
  QVERIFY(!eo->inspecting());
  optimum = solveOptimally();
  QCOMPARE(eo->phase(), Phase::Done);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QVERIFY(eo->analysisReady());
  QCOMPARE(tries(eo->data().learn, "eocross"), std::size_t{1});
  QCOMPARE(eo->data().learn.at("eocross").back().extra, 0.0);
  QCOMPARE(eo->data().learn.at("eocross").back().moves, optimum);
  VERIFY_CONTAINS(eo->verdictHtml(), "optimal");
  QVERIFY(!eo->verdictHtml().contains("looked"));
  // The details list the holds; a solution listed does the step from the
  // start, and so does the best one on Enter.
  QVERIFY(eo->detailHtml().isEmpty());
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  QVERIFY(eo->detailHtml().contains("play:"));
  QVERIFY(eo->detailHtml().contains("Where the moves went"));
  QVERIFY(eo->shownSolutions().size() >= 2);
  QTest::keyClick(window_.get(), Qt::Key_Return);
  QVERIFY(doneHold(eo->cube(), step, holds).has_value());
  // A retry isn't recorded.
  CubieCube state = eo->scrambled();
  KEY_TO(eo, Solving, Qt::Key_Escape);
  QVERIFY(eo->scrambled() == state);
  QVERIFY(eo->cube() == eo->startCube());
  KEY_TO(eo, Done, Qt::Key_Space, Qt::ShiftModifier);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QCOMPARE(tries(eo->data().learn, "eocross"), std::size_t{1});

  // Another step is kept apart.
  eo->setStep(SolveStep::EO);
  QCOMPARE(eo->phase(), Phase::Solving);
  solveOptimally();
  QCOMPARE(eo->phase(), Phase::Done);
  QTRY_VERIFY_WITH_TIMEOUT(eo->analysisReady(), 30000); // the verdict waits
  QCOMPARE(tries(eo->data().learn, "eo"), std::size_t{1});
  QCOMPARE(tries(eo->data().learn, "eocross"), std::size_t{1});
  eo->setStep(step);

  // Timed: looking (rotations) before the start, then the clock runs from
  // the first face turn to the step done.
  eo->selectTab(Tab::Timed);
  QCOMPARE(eo->timedPhase(), DrillFlow::Phase::Waiting);
  QTest::keyClick(window_.get(), Qt::Key_Space);
  optimum = solveOptimally(true);
  QCOMPARE(eo->timedPhase(), DrillFlow::Phase::Revealed);
  const std::string group = timedKey(CubeKind::Virtual, "eocross");
  QCOMPARE(eo->data().timed.at(group).size(), std::size_t{1});
  QCOMPARE(eo->data().timed.at(group).back().moves, optimum);
  QCOMPARE(eo->data().timed.at(group).back().extra, 0.0);
  QVERIFY(eo->data().timed.at(group).back().ms > 0);
  VERIFY_CONTAINS(eo->verdictHtml(), "TPS");
  QTest::qWait(DrillFlow::kGuardMs + 50);
  QTest::keyClick(window_.get(), Qt::Key_Tab);
  QVERIFY(eo->detailHtml().contains("optimal"));

  finishBackgroundWrites();
  PracticeStore saved(QDir(dataDir_).filePath("eocross.json"));
  QVERIFY(saved.load());
  QVERIFY(saved.data == eo->data());
  eo->selectTab(Tab::Untimed);
}

void GuiTest::practiceModesComeBackAsLeft() {
  auto *eo = page<EoCrossPage>();
  auto *tsle = page<TslePage>();
  // Untimed isn't timed: a solve under way is there on the way back.
  QVERIFY(switchTo(AppMode::EoCross, eo));
  QCOMPARE(eo->tab(), Tab::Untimed);
  if (eo->phase() == Phase::Done) {
    QTest::keyClick(window_.get(), Qt::Key_Space);
  }
  QCOMPARE(eo->phase(), Phase::Solving);
  typeMoves(parseAlgOrEmpty("R U"));
  const FaceletCube turned = eo->cube();
  const CubieCube state = eo->scrambled();
  QVERIFY(switchTo(AppMode::Tsle, tsle));
  QVERIFY(switchTo(AppMode::EoCross, eo));
  QCOMPARE(eo->phase(), Phase::Solving);
  QVERIFY(eo->scrambled() == state);
  QVERIFY(eo->cube() == turned);

  // Inspection is: a state whose clock ran is put away, and the way back
  // waits for Space.
  eo->selectTab(Tab::Inspection);
  QCOMPARE(eo->phase(), Phase::Ready);
  QTest::keyClick(window_.get(), Qt::Key_Space);
  QVERIFY(eo->inspecting());
  const std::size_t inspected = eo->data().inspection.size();
  QVERIFY(switchTo(AppMode::Tsle, tsle));
  QVERIFY(switchTo(AppMode::EoCross, eo));
  QCOMPARE(eo->phase(), Phase::Ready);
  QCOMPARE(eo->data().inspection.size(), inspected);

  // Timed: an armed timer is disarmed by leaving, and a new state waits
  // on the way back; nothing is recorded.
  eo->selectTab(Tab::Timed);
  QCOMPARE(eo->timedPhase(), DrillFlow::Phase::Waiting);
  const std::size_t before = eo->data().timed.size();
  QTest::keyClick(window_.get(), Qt::Key_Space);
  QVERIFY(!eo->isBusy());
  QVERIFY(switchTo(AppMode::Tsle, tsle));
  QVERIFY(switchTo(AppMode::EoCross, eo));
  QCOMPARE(eo->timedPhase(), DrillFlow::Phase::Waiting);
  QCOMPARE(eo->data().timed.size(), before);
  // While the clock runs the mode can't be left.
  QTest::keyClick(window_.get(), Qt::Key_Space);
  typeMoves(parseAlgOrEmpty("R"));
  QCOMPARE(eo->timedPhase(), DrillFlow::Phase::Running);
  QVERIFY(eo->isBusy());
  QTest::keySequence(window_.get(), AppMode::shortcut(AppMode::Tsle));
  QVERIFY(shownPage(*window_) == eo);
  QTest::keyClick(window_.get(), Qt::Key_Escape);
  QVERIFY(!eo->isBusy());
  eo->selectTab(Tab::Untimed);
}

void GuiTest::timedPhysicalSolve() {
  OPEN_PAGE(timed, TimerPage, Timed);
  // Let go too early: nothing starts.
  QTest::keyPress(window_.get(), Qt::Key_Space);
  QTest::keyRelease(window_.get(), Qt::Key_Space);
  QVERIFY(!timed->isBusy());

  // Held and let go (again, if the first scramble wasn't ready yet).
  QVERIFY(holdSpaceUntilBusy(timed));
  // Running: the mode can't be left from the Mode menu (a key would stop
  // the timer first).
  QVERIFY(timed->isBusy());
  auto *learnMode = lastChild<QAction>(window_.get(), [](QAction *a) {
    return a->shortcut() == AppMode::shortcut(AppMode::Learn);
  });
  QVERIFY(learnMode);
  learnMode->trigger();
  QCOMPARE(shownPage(*window_), timed);
  QVERIFY(timed->isBusy());
  // Settings and the command list say why they don't open.
  for (QAction *a : window_->findChildren<QAction *>()) {
    if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_K) ||
        a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Comma)) {
      window_->statusBar()->clearMessage();
      a->trigger();
      QCOMPARE(window_->statusBar()->currentMessage(),
               QString("Finish or cancel the solve (Esc) first"));
    }
  }
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

  // Backspace toggles the DNF on the solve just finished, and not once
  // another key has been pressed.
  auto penalty = [this] {
    finishBackgroundWrites();
    SessionStore store(QDir(dataDir_).filePath("sessions.json"));
    return store.load() ? store.sessions().front().solves.back().penalty
                        : Penalty::Plus2;
  };
  QTest::keyClick(window_.get(), Qt::Key_Backspace);
  QVERIFY(penalty() == Penalty::Dnf);
  QTest::keyClick(window_.get(), Qt::Key_Backspace);
  QVERIFY(penalty() == Penalty::None);
  QTest::keyClick(window_.get(), Qt::Key_Escape);
  QTest::keyClick(window_.get(), Qt::Key_Backspace);
  QVERIFY(penalty() == Penalty::None);
}

void GuiTest::untimedVirtualSolve() {
  OPEN_PAGE(untimed, UntimedPage, Untimed);
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
  OPEN_PAGE(learn, LearnPage, Learn);
  // Nothing is due until a case is put in the learn queue.
  auto *start = lastChild<QPushButton>(
      learn, [](QPushButton *b) { return b->text() == "Add new cases"; });
  QVERIFY(start);
  QVERIFY(start->isVisible());
  start->click();
  // The case waits for Space: nothing of it is shown, and no clock runs.
  QVERIFY(learn->waitingForSpace());
  QVERIFY(mainCube(learn)->displayedState().isSolved());
  typeMoves({Move{MoveType::R, 1}});
  QVERIFY(mainCube(learn)->displayedState().isSolved());
  QTest::keyClick(window_.get(), Qt::Key_Space);
  QVERIFY(!learn->waitingForSpace());
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

  // Resetting it from the case list goes on the undo stack: Ctrl+Z
  // brings its schedule and Learn attempts back.
  const SrsCard card = p["T"].card;
  const std::vector<Attempt> attempts = p["T"].attempts;
  QTreeWidgetItem *row = nullptr;
  for (auto *tree : learn->findChildren<QTreeWidget *>()) {
    for (QTreeWidgetItemIterator it(tree); *it && !row; ++it) {
      if ((*it)->data(0, Qt::UserRole).toString() == "T") {
        row = *it;
        tree->setCurrentItem(row);
      }
    }
  }
  QVERIFY(row);
  QMenu menu;
  learn->fillModeMenu(menu);
  QAction *reset = nullptr;
  for (QAction *a : menu.actions()) {
    if (a->text() == "Reset learning progress") {
      reset = a;
    }
  }
  QVERIFY(reset);
  reset->trigger();
  p = savedProgress("pll");
  QCOMPARE(p["T"].card.reps, 0);
  QVERIFY(p["T"].attempts.empty());
  QTest::keyClick(window_.get(), Qt::Key_Z, Qt::ControlModifier);
  p = savedProgress("pll");
  QVERIFY(p["T"].card == card);
  QVERIFY(p["T"].attempts == attempts);
}

void GuiTest::quizAsksAmongLookAlikes() {
  AlgSet set;
  std::vector<std::string> ticked;
  auto addGroup = [&](const std::string &prefix, int cases) {
    CaseGroup g;
    g.name = prefix;
    for (int i = 1; i <= cases; ++i) {
      AlgCase c;
      c.id = prefix + std::to_string(i);
      c.name = "Case " + c.id;
      ticked.push_back(c.id);
      g.cases.push_back(c);
    }
    set.groups.push_back(g);
  };
  addGroup("a", 8);
  addGroup("b", 2);
  Rng rng = makeRng();
  auto none = [](const std::string &) { return false; };
  auto nameOf = [](const std::string &id) { return qs("Case " + id); };

  // One case is nothing to choose from.
  QVERIFY(!QuizPractice::pick(set, {"a1"}, none, {}, rng));
  QVERIFY(!QuizPractice::pick(set, {}, none, {}, rng));

  for (int i = 0; i < 60; ++i) {
    auto quiz = QuizPractice::pick(set, ticked, none, "a1", rng);
    QVERIFY(quiz);
    const std::string id = quiz->caseId();
    QVERIFY(id != "a1"); // not the last one again
    QStringList names = quiz->names(set);
    QCOMPARE(static_cast<std::size_t>(names.size()), quiz->choices());
    QVERIFY(quiz->answer() < quiz->choices());
    QCOMPARE(names[static_cast<qsizetype>(quiz->answer())], nameOf(id));
    QCOMPARE(names.removeDuplicates(), qsizetype{0});
    // The choices are the case's look-alikes: its own group, six at most.
    QCOMPARE(quiz->choices(), id.front() == 'a' ? std::size_t{6} : 2u);
    for (const QString &name : names) {
      QVERIFY(name.startsWith(qs("Case ") + QChar(id.front())));
    }
  }

  // Cases already started are asked first, once there are two.
  auto started = [](const std::string &id) { return id.front() == 'b'; };
  for (int i = 0; i < 20; ++i) {
    auto quiz = QuizPractice::pick(set, ticked, started, "b1", rng);
    QVERIFY(quiz);
    QCOMPARE(quiz->caseId(), std::string("b2"));
    QCOMPARE(quiz->choices(), std::size_t{2});
  }
  auto one = [](const std::string &id) { return id == "b1"; };
  bool other = false;
  for (int i = 0; i < 60 && !other; ++i) {
    other = QuizPractice::pick(set, ticked, one, {}, rng)->caseId() != "b1";
  }
  QVERIFY(other);

  // The answer is timed from the case being shown.
  auto quiz = QuizPractice::pick(set, ticked, started, "b1", rng);
  QVERIFY(quiz);
  quiz->shown(100);
  const QuizPractice::Result right = quiz->answered(quiz->answer(), 350);
  QVERIFY(right.right);
  QCOMPARE(right.ms, std::int64_t{250});
  QVERIFY(!quiz->resultText(&set, right).contains("Case b2"));
  const QuizPractice::Result wrong = quiz->answered(1 - quiz->answer(), 350);
  QVERIFY(!wrong.right);
  QCOMPARE(quiz->resultText(&set, wrong), QString("It was Case b2."));
}

void GuiTest::drillArmsAndStopsItself() {
  OPEN_PAGE(drill, DrillPage, Drill);
  // Drill every case, not only those learned in Learn.
  auto *pool = lastChild<QComboBox>(drill, [](QComboBox *box) {
    return box->findText("Being learned") >= 0;
  });
  QVERIFY(pool);
  int all = pool->findText("All");
  QVERIFY(all >= 0);
  pool->setCurrentIndex(all);
  emit pool->activated(all);
  // Kept under the name it always had.
  QCOMPARE(saved::drillPool("pll").get(), QString("ticked"));
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
  // Delete deletes the attempt and Ctrl+Z (only by itself) puts it back,
  // as in Timed.
  QTest::qWait(DrillFlow::kGuardMs + 50);
  QTest::keyClick(window_.get(), Qt::Key_Delete);
  QCOMPARE(savedProgress("pll")["T"].drillCount(AttemptMode::VirtualDrill),
           std::size_t{0});
  QTest::keyClick(window_.get(), Qt::Key_Z,
                  Qt::ControlModifier | Qt::ShiftModifier);
  QCOMPARE(savedProgress("pll")["T"].drillCount(AttemptMode::VirtualDrill),
           std::size_t{0});
  QTest::keyClick(window_.get(), Qt::Key_Z, Qt::ControlModifier);
  p = savedProgress("pll");
  QCOMPARE(p["T"].drillCount(AttemptMode::VirtualDrill), std::size_t{1});
  QCOMPARE(p["T"].failRate(AttemptMode::VirtualDrill), 0.0);
  // Once more does nothing: it is back.
  QTest::keyClick(window_.get(), Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(savedProgress("pll")["T"].drillCount(AttemptMode::VirtualDrill),
           std::size_t{1});
}

void GuiTest::libraryAddsAnAlgAndUndoes() {
  OPEN_PAGE(library, LibraryPage, Library);
  library->selectCase("pll", "Ua");
  auto *edit = lastChild<QLineEdit>(library, [](QLineEdit *e) {
    return e->placeholderText().startsWith("New alg");
  });
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

namespace {

// The WCAG contrast ratio of two colours.
double contrast(const QColor &a, const QColor &b) {
  auto luminance = [](const QColor &c) {
    auto channel = [](double v) {
      return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) +
           0.0722 * channel(c.blueF());
  };
  double la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

void GuiTest::practiceResultsAreWorded() {
  // The splits: a part's time runs from the end of the one before, its
  // pause to its first turn, and its TPS is of the turning alone.
  const Alg moves = parseAlgOrEmpty("R U R' y F");
  const std::vector<std::int64_t> at = {1000, 1500, 2000, 2500, 4000};
  const std::vector<PracticeSplit> parts = {{"pair", 3}};
  QVERIFY(splitsHtml({}, moves, at, 2, "#888888").isEmpty());
  QVERIFY(splitsHtml(parts, moves, {1000}, 2, "#888888").isEmpty());
  QVERIFY(splitsHtml(parts, {}, {}, 2, "#888888").isEmpty());
  auto row = [](const QString &name, std::int64_t time, std::int64_t pause,
                const QString &htm, const QString &tps) {
    return QString("<tr><td>%1</td><td align='right'>%2</td><td "
                   "align='right'>%3</td><td align='right'>%4</td><td "
                   "align='right'>%5</td></tr>")
        .arg(name, timeText(time, 2), timeText(pause, 2), htm, tps);
  };
  const QString splits = splitsHtml(parts, moves, at, 2, "#888888");
  VERIFY_CONTAINS(splits, row("pair", 2000, 1000, "3", "3.0"));
  // What is left after the parts: the rotation before its turn is looking,
  // and one move has no speed.
  VERIFY_CONTAINS(splits, row("the rest", 2000, 2000, "1", "-"));
  // A part that ends past the solve takes what there is, and nothing is
  // left for the rest.
  const QString whole = splitsHtml({{"all", 9}}, moves, at, 2, "#888888");
  VERIFY_CONTAINS(whole, row("all", 4000, 1000, "4", "1.3"));
  QVERIFY(!whole.contains("the rest"));

  // A timed solve: its moves and speed, and how it compares.
  QCOMPARE(
      timedLine({.moves = 20, .ms = 10000, .dnf = false, .meanBefore = {}}, 2),
      QString("20 moves · 2.00 TPS"));
  const QString under = timedLine(
      {.moves = 0, .ms = 10000, .dnf = false, .meanBefore = 12000.0}, 2);
  VERIFY_CONTAINS(under, QString("%1 under your mean (%2)")
                             .arg(timeText(2000, 2), timeText(12000, 2)));
  const QString over = timedLine(
      {.moves = 0, .ms = 10000, .dnf = false, .meanBefore = 9000.0}, 2);
  QCOMPARE(over, QString("%1 over your mean (%2)")
                     .arg(timeText(1000, 2), timeText(9000, 2)));
  const QString dnf = timedLine(
      {.moves = 20, .ms = 10000, .dnf = true, .meanBefore = 9000.0}, 2);
  QVERIFY2(dnf.contains("DNF") && dnf.endsWith("20 moves") &&
               !dnf.contains("TPS") && !dnf.contains("mean"),
           qPrintable(dnf));

  // The Inspection clock counts down whole seconds, then up over the
  // limit; stopped, it says the time looked.
  const QColor ink(1, 2, 3);
  auto clock = [&ink](std::int64_t used, std::int64_t limit, bool running) {
    return inspectionClock(used, limit, running, ink);
  };
  QCOMPARE(clock(1000, 15000, true),
           QString("<span style='color:#010203'>14</span>"));
  QVERIFY(clock(12500, 15000, true).endsWith(">3</span>"));
  QVERIFY(!clock(12500, 15000, true).contains("#010203"));
  QVERIFY(clock(16000, 15000, true).endsWith(">+1.0</span>"));
  QCOMPARE(clock(9300, 0, true),
           QString("<span style='color:#010203'>9.3</span>"));
  QVERIFY(clock(9300, 15000, false).endsWith(">Looked 9.3 s</span>"));
  QVERIFY(inspectionLine(0, 15000).isEmpty());
  QCOMPARE(inspectionLine(9300, 15000), QString("looked 9.3 s"));
  QVERIFY(inspectionLine(16000, 15000)
              .contains("looked 16.0 s, over the 15.0 s limit"));

  // The verdict: nothing of the result while it is being worked out.
  PracticeVerdict v;
  v.result = "2 moves over";
  v.best = "Best: <b>R U R'</b>";
  v.note = "Deleted";
  QCOMPARE(verdictHtml(v, "#888888"),
           QString("2 moves over<br>Best: <b>R U R'</b><br>"
                   "<span style='color:#888888'>Deleted</span>"));
  v.waiting = true;
  QVERIFY(!verdictHtml(v, "#888888").contains("2 moves over"));
  QVERIFY(verdictHtml({}, "#888888").isEmpty());
  PracticeOutcome o;
  QVERIFY(bestLine(o).isEmpty() && efficiencyHtml(o).isEmpty());
  o.best = parseAlgOrEmpty("R U R'");
  QCOMPARE(bestLine(o), QString("Best: <b>R U R'</b>"));
  o.bestLabel = "Yours";
  o.bestText = "<i>as chosen</i>";
  QCOMPARE(bestLine(o), QString("Yours: <b><i>as chosen</i></b>"));

  // When a card comes back.
  const std::int64_t now = 1000000, dayEnd = now + 7200;
  QCOMPARE(dueSentence(now, now, dayEnd), QString("now"));
  QCOMPARE(dueSentence(now + 30, now, dayEnd), QString("in <1 min"));
  QCOMPARE(dueSentence(now + 90, now, dayEnd), QString("in 2 min"));
  QCOMPARE(dueSentence(now + 3600, now, dayEnd), QString("in 1 h"));
  QCOMPARE(dueSentence(dayEnd, now, dayEnd), QString("tomorrow"));
  QCOMPARE(dueSentence(dayEnd + 3 * 86400, now, dayEnd), QString("in 4 d"));
  // Every page rounds a length of time the same way.
  QCOMPARE(durationText(59), QString("<1 min"));
  QCOMPARE(durationText(90), QString("2 min"));
  QCOMPARE(durationText(5500), QString("2 h"));
  QCOMPARE(durationText(3 * 86400 + 3600), QString("3 d"));
  CaseProgress card;
  QCOMPARE(dueCell(nullptr, now, dayEnd), QString("-"));
  QCOMPARE(dueCell(&card, now, dayEnd), QString("-"));
  card.card.state = CardState::Learning;
  card.card.due = now + 90;
  QCOMPARE(dueCell(&card, now, dayEnd), QString("2 min"));
  card.card.state = CardState::Review;
  QCOMPARE(dueCell(&card, now, dayEnd), QString("today"));
  card.card.due = dayEnd + 86400;
  QCOMPARE(dueCell(&card, now, dayEnd), QString("1 d"));
}

void GuiTest::practiceKeysFollowThePhase() {
  auto names = [](const PracticeKeys &k) { return keyNames(k.keys).join(' '); };
  using P = PracticePhase;
  auto untimed = [](P phase, auto set, const QList<KeyHint> &modeKeys = {}) {
    PracticeFlow::State s;
    s.phase = phase;
    set(s);
    return untimedKeys(s, modeKeys);
  };
  auto asIs = [](PracticeFlow::State &) {};
  QCOMPARE(names(untimed(P::Idle, asIs)), QString("Space"));
  QCOMPARE(names(untimed(P::Choose, asIs)), QString("1-9 Enter"));
  QCOMPARE(names(untimed(P::Choose, [](auto &s) { s.choosingAgain = true; })),
           QString("1-9 Enter Esc"));
  QCOMPARE(names(untimed(P::Solving, [](auto &s) { s.hints = true; })),
           QString("Tab Shift+Space Esc"));
  // Drill's give-up key, whichever it is.
  QCOMPARE(
      names(untimed(P::Solving, [](auto &s) { s.shiftSpaceGivesUp = false; })),
      QString("Backspace Esc"));
  // A hidden cube has no hint to give, and says so.
  const PracticeKeys hidden =
      untimed(P::Solving, [](auto &s) { s.hidden = s.hints = true; });
  QCOMPARE(names(hidden), QString("Space Shift+Space Esc"));
  QVERIFY(!hidden.message.isEmpty());
  // Nothing goes on while the verdict is worked out.
  const PracticeKeys waiting =
      untimed(P::Done, [](auto &s) { s.waiting = s.best = true; });
  QVERIFY(waiting.keys.isEmpty() && !waiting.message.isEmpty());
  QCOMPARE(names(untimed(P::Done,
                         [](auto &s) { s.best = s.graded = s.chooser = true; },
                         {{"+", "add"}})),
           QString("Space Enter Tab 1-4 Ctrl+Z + 0 Esc"));
  QCOMPARE(names(untimed(P::Done, asIs)), QString("Space Tab Esc"));
  // An Untimed or Inspection try just kept can be deleted.
  QCOMPARE(names(untimed(P::Done, [](auto &s) { s.recorded = true; })),
           QString("Space Tab Delete Esc"));

  // Every key a hint names does something in the state it is shown in:
  // the hints and the keys are read from the same state.
  auto keyOf = [](const QString &name) -> std::optional<TrainKeyEvent> {
    TrainKeyEvent k;
    if (name == "Space" || name == "Shift+Space") {
      k.key = TrainKey::Space;
      k.shift = name == "Shift+Space";
    } else if (name == "Backspace") {
      k.key = TrainKey::Backspace;
    } else if (name == "Enter") {
      k.key = TrainKey::Enter;
    } else if (name == "Tab") {
      k.key = TrainKey::Tab;
    } else if (name == "Esc") {
      k.key = TrainKey::Escape;
    } else if (name == "Delete") {
      k.key = TrainKey::Delete;
    } else if (name == "Ctrl+Z") {
      k.key = TrainKey::Undo;
    } else if (name == "1-9" || name == "1-4" || name == "0") {
      k.key = TrainKey::Digit;
      k.digit = name == "0" ? 0 : 1;
    } else {
      return std::nullopt;
    }
    return k;
  };
  for (int bits = 0; bits < (1 << 10); ++bits) {
    for (P phase :
         {P::Idle, P::Ready, P::Choose, P::Demo, P::Solving, P::Done}) {
      PracticeFlow::State s;
      s.phase = phase;
      s.tab = bits & 1 ? PracticeTab::Inspection : PracticeTab::Learn;
      s.hidden = bits & 2;
      s.waiting = bits & 4;
      s.hints = bits & 8;
      s.graded = bits & 16;
      s.chooser = bits & 32;
      s.recorded = bits & 64;
      s.choosingAgain = bits & 128;
      s.shiftSpaceGivesUp = bits & 256;
      s.best = bits & 512;
      for (const KeyHint &h : untimedKeys(s).keys) {
        const std::optional<TrainKeyEvent> k = keyOf(h.key);
        QVERIFY2(k, qPrintable(h.key));
        const PracticeFlow::Step step = PracticeFlow::key(*k, s);
        QVERIFY2(step.handled && step.action != PracticeFlow::Action::None,
                 qPrintable(h.key));
      }
    }
  }

  DrillFlow flow;
  DrillFlow::Config config;
  DrillSettings drill;
  drill.tapToArm = true;
  drill.shiftSpaceGivesUp = true;
  TimedKeysState timed;
  QCOMPARE(names(timedKeys(flow, drill, timed)), QString("Space"));
  config.virtualCube = false;
  flow.setConfig(config);
  flow.setUp(0);
  const PracticeKeys awaited = timedKeys(flow, drill, timed);
  QCOMPARE(names(awaited), QString("hold Space Shift+Space"));
  QVERIFY(awaited.message.isEmpty());
  timed.scrambleShown = true;
  QVERIFY(!timedKeys(flow, drill, timed).message.isEmpty());
  drill.shiftSpaceGivesUp = false;
  config.virtualCube = true;
  flow.setConfig(config);
  flow.setUp(0);
  timed = {};
  QCOMPARE(names(timedKeys(flow, drill, timed)), QString("Space Backspace"));
  // The result: only an attempt that is kept can be marked or deleted.
  flow.finished(1000, DrillFlow::Result::Solved, {}, 0);
  QCOMPARE(names(timedKeys(flow, drill, timed)), QString("Space Tab Esc"));
  timed.best = timed.recorded = true;
  QCOMPARE(names(timedKeys(flow, drill, timed)),
           QString("Space Enter Tab Backspace Delete Esc"));

  QVERIFY(timedHintText(DrillFlow::Hint::None, drill).isEmpty());
  DrillSettings held = drill;
  held.tapToArm = false;
  QVERIFY(timedHintText(DrillFlow::Hint::OnlyRotations, drill) !=
          timedHintText(DrillFlow::Hint::OnlyRotations, held));
}

void GuiTest::themeColoursAreReadable() {
  for (bool dark : {true, false}) {
    applyTheme(*qApp, dark);
    QCOMPARE(darkTheme(), dark);
    const QPalette pal = QGuiApplication::palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor base = pal.color(QPalette::Base);
    auto readable = [&](const QColor &c, double need, const char *what) {
      for (const QColor &bg : {window, base}) {
        QVERIFY2(contrast(c, bg) >= need,
                 qPrintable(QString("%1 %2 on %3 (%4 theme): %5")
                                .arg(what, c.name(), bg.name(),
                                     dark ? "dark" : "light")
                                .arg(contrast(c, bg), 0, 'f', 2)));
      }
    };
    // Text.
    readable(roleColour(Role::Fail), 4.5, "fail");
    readable(roleColour(Role::Info), 4.5, "info");
    readable(roleColour(Role::Muted), 4.5, "muted");
    readable(statusColour(Status::Good), 4.5, "good");
    readable(statusColour(Status::Warning), 4.5, "warning");
    readable(statusColour(Status::Bad), 4.5, "bad");
    readable(pal.color(QPalette::Link), 4.5, "link");
    // The keys strip's chips are buttons with muted text on them.
    QVERIFY(contrast(roleColour(Role::Muted), pal.color(QPalette::Button)) >=
            4.5);
    // Chart lines, named in their colours.
    for (Role r : {Role::Series1, Role::Series2, Role::Series3}) {
      readable(roleColour(r), 4.5, "chart series");
    }
    // Alg Lab's series are marks with names beside them.
    for (std::size_t i = 0; i < 8; ++i) {
      readable(labSeriesColour(i), 3.0, "lab series");
    }
    readable(roleColour(Role::FieldEdge), 3.0, "field edge");
    // White text on a selection, a grade badge or the banner.
    QVERIFY(contrast(pal.color(QPalette::HighlightedText),
                     pal.color(QPalette::Highlight)) >= 4.5);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // The accent is the app's, not the desktop's.
    QCOMPARE(pal.color(QPalette::Accent), pal.color(QPalette::Highlight));
    QVERIFY(pal.isBrushSet(QPalette::Active, QPalette::Accent));
#endif
    for (Role r :
         {Role::GradeAgain, Role::GradeHard, Role::GradeGood, Role::GradeEasy,
          Role::GradeNone, Role::GradeWalked, Role::Banner}) {
      QVERIFY(contrast(roleColour(Role::OnBadge), roleColour(r)) >= 4.5);
    }
  }
  applyTheme(*qApp, settings_.darkTheme);

  // A selected row shows a case picture as it is.
  QPixmap picture(8, 8);
  picture.fill(QColor(0xff, 0xd5, 0x00));
  QIcon icon = cube_painter::caseIcon(picture);
  QCOMPARE(icon.pixmap(8, 8, QIcon::Selected).toImage(),
           icon.pixmap(8, 8, QIcon::Normal).toImage());

  // A flat sticker sits on a plate of the body colour, so that a white one
  // still has an edge on a light window.
  CubeLook look;
  QPixmap net(480, 360);
  net.fill(Qt::transparent);
  {
    QPainter p(&net);
    cube_painter::paintNet(p, QRectF(0, 0, 480, 360), FaceletCube(), look);
  }
  // Between the first two U stickers (cells of 40 px, U's block at x 120).
  QImage image = net.toImage();
  QCOMPARE(image.pixelColor(160, 20).rgb(), look.scheme.body.rgb());
}

// Today, Alg Lab's Search tab and the mode menus work from the keyboard
// (and the command list), and a page's message goes with it.
void GuiTest::pagesTakeTheKeyboard() {
  // Alg Lab's candidates: the arrows select, Enter adds to the session.
  OPEN_PAGE(lab, AlgLabPage, AlgLab);
  lab->selectCase("pll", "T");
  QTabWidget *tabs = lab->findChild<QTabWidget *>();
  QVERIFY(tabs);
  tabs->setCurrentIndex(0);
  QTableWidget *candidates = candidateTable(lab);
  QVERIFY(candidates);
  QVERIFY(candidates->rowCount() >= 1);
  candidates->clearSelection();
  window_->setFocus();
  QTest::keyClick(window_.get(), Qt::Key_Down);
  QVERIFY(!candidates->selectedItems().isEmpty());
  QTest::keyClick(window_.get(), Qt::Key_Return);
  QVERIFY2(window_->statusBar()->currentMessage().startsWith("Added"),
           qPrintable(window_->statusBar()->currentMessage()));
  // The times kept (by algLabTimesEachAlg): the time columns show.
  QVERIFY(!candidates->isColumnHidden(candidates->columnCount() - 1));

  // The message doesn't follow to another mode.
  OPEN_PAGE(today, TodayPage, Today);
  QCOMPARE(window_->statusBar()->currentMessage(), QString());

  // Today: one button for what is next, rows as tall in both tables, and
  // the keys pick and open a set.
  QPushButton *next = today->findChild<QPushButton *>("todayNext");
  QVERIFY(next);
  QVERIFY(!next->text().isEmpty());
  auto tables = today->findChildren<QTreeWidget *>();
  QCOMPARE(tables.size(), 2);
  QCOMPARE(tables[0]->visualItemRect(tables[0]->topLevelItem(0)).height(),
           tables[1]->visualItemRect(tables[1]->topLevelItem(0)).height());
  // A refresh with the same sets writes the rows in place: the items and
  // their buttons stay (and so the scroll position and the mouse's button).
  QTreeWidgetItem *firstRow = tables[0]->topLevelItem(0);
  QWidget *firstButton = tables[0]->itemWidget(firstRow, 5);
  QTreeWidgetItem *firstMode = tables[1]->topLevelItem(0);
  QVERIFY(firstButton);
  today->activated();
  QCOMPARE(tables[0]->topLevelItem(0), firstRow);
  QCOMPARE(tables[0]->itemWidget(firstRow, 5), firstButton);
  QCOMPARE(tables[1]->topLevelItem(0), firstMode);
  QVERIFY(!firstButton->findChild<QPushButton *>()->text().isEmpty());
  QTest::keyClick(window_.get(), Qt::Key_Down);
  QTreeWidgetItem *picked = tables[0]->currentItem();
  QVERIFY(picked && picked->isSelected());
  QCOMPARE(picked->text(0), QString("PLL"));
  QTest::keyClick(window_.get(), Qt::Key_Return);
  QTRY_COMPARE(shownPage(*window_), page<LearnPage>());

  // Drill's mode menu has what its buttons and the list's header offer.
  OPEN_PAGE(drill, DrillPage, Drill);
  QMenu menu;
  drill->fillModeMenu(menu);
  QStringList texts;
  for (QAction *a : menu.actions()) {
    texts << a->text();
  }
  QVERIFY2(texts.contains("Reset tally"), qPrintable(texts.join(", ")));
  QVERIFY(texts.contains("Tick all"));
  QVERIFY(texts.contains("Columns"));

  // Library shows only a line until a case is selected.
  OPEN_PAGE(library, LibraryPage, Library);
  library->selectCase("pll", {});
  auto *empty = lastChild<QLabel>(library, [](QLabel *l) {
    return l->text() == "Select a case to see and edit its algs";
  });
  QVERIFY(empty);
  QVERIFY(empty->isVisible());
  library->selectCase("pll", "T");
  QVERIFY(!empty->isVisible());
}

// The command list: headings that can't be selected, keys on the right,
// and the commands last run first when nothing is typed.
void GuiTest::keysAreReadTheSameForEveryPage() {
  using K = TrainKey;
  auto read = [](int key, Qt::KeyboardModifiers mods, KeyChords chords) {
    QKeyEvent e(QEvent::KeyPress, key, mods);
    return trainKeyEvent(&e, true, chords);
  };
  const auto all = {KeyChords::None, KeyChords::Undo, KeyChords::Attempt};
  // The keys by themselves are the same keys whatever the page's chords.
  const std::vector<std::pair<int, K>> plain = {
      {Qt::Key_Space, K::Space},
      {Qt::Key_Return, K::Enter},
      {Qt::Key_Enter, K::Enter},
      {Qt::Key_Tab, K::Tab},
      {Qt::Key_Backspace, K::Backspace},
      {Qt::Key_Delete, K::Delete},
      {Qt::Key_Escape, K::Escape},
      {Qt::Key_Left, K::Left},
      {Qt::Key_Right, K::Right},
      {Qt::Key_Home, K::Home},
      {Qt::Key_Shift, K::Modifier},
      {Qt::Key_Control, K::Modifier},
      {Qt::Key_Z, K::Other},
      {Qt::Key_F9, K::Other}};
  for (KeyChords chords : all) {
    for (const auto &[key, expected] : plain) {
      QCOMPARE(read(key, Qt::NoModifier, chords).key, expected);
    }
    for (int d = 0; d <= 9; ++d) {
      const TrainKeyEvent k = read(Qt::Key_0 + d, Qt::NoModifier, chords);
      QCOMPARE(k.key, K::Digit);
      QCOMPARE(k.digit, d);
      QVERIFY(!k.modified && !k.shift && k.press && !k.autoRepeat);
    }
    // The number pad's keys are the keys they show.
    QCOMPARE(read(Qt::Key_Enter, Qt::KeypadModifier, chords).key, K::Enter);
    QCOMPARE(read(Qt::Key_Delete, Qt::KeypadModifier, chords).key, K::Delete);
    // What is held is told apart from the key.
    const TrainKeyEvent shifted =
        read(Qt::Key_Space, Qt::ShiftModifier, chords);
    QCOMPARE(shifted.key, K::Space);
    QVERIFY(shifted.shift && !shifted.modified);
    const TrainKeyEvent ctrl = read(Qt::Key_Space, Qt::ControlModifier, chords);
    QCOMPARE(ctrl.key, K::Space);
    QVERIFY(ctrl.modified && !ctrl.shift);
    QVERIFY(read(Qt::Key_1, Qt::AltModifier, chords).modified);
    QVERIFY(read(Qt::Key_1, Qt::MetaModifier, chords).modified);
  }
  QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier, {},
                   true);
  QVERIFY(trainKeyEvent(&repeat, true, KeyChords::Attempt).autoRepeat);
  QVERIFY(!trainKeyEvent(&repeat, false, KeyChords::Attempt).press);

  // Ctrl+Z: nothing to Alg Lab, Undo in the rest only by itself
  // (Ctrl+Shift+Z is Library's redo).
  const Qt::KeyboardModifiers ctrlShift =
      Qt::ControlModifier | Qt::ShiftModifier;
  QCOMPARE(read(Qt::Key_Z, Qt::ControlModifier, KeyChords::None).key, K::Other);
  QCOMPARE(read(Qt::Key_Z, Qt::ControlModifier, KeyChords::Undo).key, K::Undo);
  QCOMPARE(read(Qt::Key_Z, ctrlShift, KeyChords::Undo).key, K::Other);
  QCOMPARE(read(Qt::Key_Z, Qt::ControlModifier, KeyChords::Attempt).key,
           K::Undo);
  QCOMPARE(read(Qt::Key_Z, ctrlShift, KeyChords::Attempt).key, K::Other);

  // The chords of Untimed and Move Count are theirs alone: in Learn Alt+3
  // is the digit and Ctrl+Right the arrow.
  QCOMPARE(read(Qt::Key_3, Qt::AltModifier, KeyChords::Attempt).key, K::Dnf);
  QCOMPARE(read(Qt::Key_C, Qt::AltModifier, KeyChords::Attempt).key,
           K::Comment);
  QCOMPARE(read(Qt::Key_R, Qt::AltModifier, KeyChords::Attempt).key, K::Review);
  QCOMPARE(read(Qt::Key_Right, Qt::ControlModifier, KeyChords::Attempt).key,
           K::Skip);
  QCOMPARE(
      read(Qt::Key_3, Qt::AltModifier | Qt::ShiftModifier, KeyChords::Attempt)
          .key,
      K::Digit);
  for (KeyChords chords : {KeyChords::None, KeyChords::Undo}) {
    const TrainKeyEvent three = read(Qt::Key_3, Qt::AltModifier, chords);
    QCOMPARE(three.key, K::Digit);
    QCOMPARE(three.digit, 3);
    QCOMPARE(read(Qt::Key_C, Qt::AltModifier, chords).key, K::Other);
    QCOMPARE(read(Qt::Key_Right, Qt::ControlModifier, chords).key, K::Right);
    // Delete deletes with anything held; in Untimed and Move Count only
    // by itself.
    QCOMPARE(read(Qt::Key_Delete, Qt::ShiftModifier, chords).key, K::Delete);
  }
  QCOMPARE(read(Qt::Key_Delete, Qt::ShiftModifier, KeyChords::Attempt).key,
           K::Other);
}

void GuiTest::commandListGroupsAndRemembers() {
  int ran = 0;
  auto commands = [&ran] {
    return std::vector<CommandPalette::Command>{
        {"Go: Learn", "Ctrl+1", [] {}, "Go"},
        {"Tick all", {}, [&ran] { ++ran; }, "This page"},
        {"View: Sidebar (on)", "Ctrl+B", [] {}, "Menus"}};
  };
  {
    CommandPalette palette(commands(), window_.get());
    auto *list = palette.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 6); // three headings
    QCOMPARE(list->item(0)->text(), QString("Go"));
    QVERIFY(!(list->item(0)->flags() & Qt::ItemIsSelectable));
    QCOMPARE(list->item(1)->text(), QString("Learn")); // under "Go"
    QCOMPARE(list->currentRow(), 1);
    auto *search = palette.findChild<QLineEdit *>();
    QVERIFY(search);
    QTest::keyClick(search, Qt::Key_Down); // over the heading
    QCOMPARE(list->currentItem()->text(), QString("Tick all"));
    QTest::keyClick(search, Qt::Key_Return);
  }
  QTRY_COMPARE(ran, 1);
  CommandPalette palette(commands(), window_.get());
  auto *list = palette.findChild<QListWidget *>();
  QVERIFY(list);
  QCOMPARE(list->item(0)->text(), QString("Recent"));
  QCOMPARE(list->item(1)->text(), QString("Tick all"));
  window_->activateWindow();
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

void GuiTest::interfaceTextSizeScalesTheText() {
  // A size scales in the unit the font gives it, points or pixels.
  {
    QFont points, pixels, f;
    points.setPointSizeF(10);
    pixels.setPixelSize(14);
    scaleSize(f, points, 1.5);
    QCOMPARE(f.pointSizeF(), 15.0);
    scaleSize(f, pixels, 1.5);
    QCOMPARE(f.pixelSize(), 21);
  }
  // Chooses an interface text size in Settings and presses OK.
  auto choose = [this](int percent) {
    ModalWatcher watcher([percent](QWidget *m) {
      auto *dialog = qobject_cast<SettingsDialog *>(m);
      if (!dialog) {
        return;
      }
      for (auto *box : dialog->findChildren<QComboBox *>()) {
        if (box->findData(108) >= 0 && box->findData(133) >= 0) {
          box->setCurrentIndex(box->findData(percent));
        }
      }
      dialog->findChild<QDialogButtonBox *>()
          ->button(QDialogButtonBox::Ok)
          ->click();
    });
    QTest::keySequence(window_.get(), QKeySequence(Qt::CTRL | Qt::Key_Comma));
    QTRY_VERIFY(!watcher.seen.isEmpty());
    QTRY_VERIFY(!QApplication::activeModalWidget());
    window_->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(window_.get()));
  };
  QVERIFY(switchTo(AppMode::Today, page<TodayPage>()));
  auto *title = lastChild<QLabel>(page<TodayPage>(), [](QLabel *label) {
    return label->text() == "Today";
  });
  QVERIFY(title);
  const double body = QApplication::font().pointSizeF();
  const double display = title->font().pointSizeF();
  QVERIFY(display > body * 1.5);

  // The largest size: the text and everything worked out from it grow,
  // at once.
  choose(133);
  QCOMPARE(settings_.interfaceTextPercent, 133);
  QVERIFY(QApplication::font().pointSizeF() > body * 1.3);
  QVERIFY(page<TodayPage>()->font().pointSizeF() > body * 1.3);
  QVERIFY(title->font().pointSizeF() > display * 1.3);
  QCOMPARE(AppSettings::load().interfaceTextPercent, 133);

  choose(100);
  QCOMPARE(QApplication::font().pointSizeF(), body);
  QCOMPARE(title->font().pointSizeF(), display);
}

void GuiTest::tipsShowInTheStatusBar() {
  QVERIFY(switchTo(AppMode::Timed, page<TimerPage>()));
  QWidget *tipped = nullptr;
  for (auto *w : page<TimerPage>()->findChildren<QWidget *>()) {
    if (w->isVisible() && !w->toolTip().isEmpty()) {
      tipped = w;
      break;
    }
  }
  QVERIFY(tipped);
  QStatusBar *bar = window_->statusBar();
  bar->clearMessage();
  // Never as a tooltip; in the status bar while the mouse is over it.
  QHelpEvent help(QEvent::ToolTip, QPoint(1, 1),
                  tipped->mapToGlobal(QPoint(1, 1)));
  QVERIFY(QApplication::sendEvent(tipped, &help));
  QCOMPARE(bar->currentMessage(), tipped->toolTip().simplified());
  bar->clearMessage();
  QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
  QApplication::sendEvent(tipped, &enter);
  QCOMPARE(bar->currentMessage(), tipped->toolTip().simplified());
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(tipped, &leave);
  QVERIFY(bar->currentMessage().isEmpty());

  // A dialog has no status bar: its tips pop up as tooltips.
  QDialog dialog(window_.get());
  auto *button = new QPushButton("Tipped", &dialog);
  button->setToolTip("What the button does");
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  QHelpEvent dialogHelp(QEvent::ToolTip, QPoint(1, 1),
                        button->mapToGlobal(QPoint(1, 1)));
  QApplication::sendEvent(button, &dialogHelp);
  QVERIFY(bar->currentMessage().isEmpty());
  QTRY_COMPARE(QToolTip::text(), button->toolTip());
  QToolTip::hideText();
  dialog.close();
  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
}

// A chart of something other than solves (the drill history's days) names
// its points and lines as it is told, and its text grows with the font.
void GuiTest::trendChartNamesWhatItCharts() {
  TrendChart chart;
  chart.setPointName("day mean");
  chart.setAverages(false);
  chart.setPointLabels({"Mon 1", "Tue 2", "Wed 3"});
  chart.setSeries({{"daily best", Role::Series3, {1000, 1100, 900}}});
  std::vector<Solve> means(3);
  means[0].timeMs = 1500;
  means[1].timeMs = 1600;
  means[2].timeMs = 1400;
  chart.setSolves(means);
  const QString said = chart.accessibleDescription();
  VERIFY_CONTAINS(said, "Wed 3: 1.40");
  VERIFY_CONTAINS(said, "daily best: 0.90");
  QVERIFY2(!said.contains("ao5") && !said.contains("#3"), qPrintable(said));
  // Paints at a large text size without trouble.
  QFont big = chart.font();
  big.setPointSizeF(big.pointSizeF() * 1.33);
  chart.setFont(big);
  chart.resize(260, 140);
  QVERIFY(!chart.grab().isNull());
}

// A long session: hovering only draws the marker over the plot drawn
// before, so it is quick, and leaves the plot as it was.
void GuiTest::trendChartHoverKeepsThePlot() {
  TrendChart chart;
  std::vector<Solve> solves(20000);
  for (std::size_t i = 0; i < solves.size(); ++i) {
    solves[i].timeMs = 9000 + static_cast<std::int64_t>((i * 7919) % 5000);
  }
  chart.setSolves(solves);
  chart.resize(760, 260); // not shown: the main window keeps the focus
  const QImage before = chart.grab().toImage();
  QElapsedTimer timer;
  timer.start();
  for (int x = 100; x < 300; x += 2) {
    QMouseEvent move(QEvent::MouseMove, QPointF(x, 100), QPointF(x, 100),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&chart, &move);
    (void)chart.grab(); // paints it
  }
  // 100 hovers: about a whole plot drawn each, before the plot was kept.
  QVERIFY2(timer.elapsed() < 2000,
           qPrintable(QString::number(timer.elapsed())));
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&chart, &leave);
  QCOMPARE(chart.grab().toImage(), before);
  // A new solve is charted.
  solves.push_back(solves.front());
  solves.back().timeMs = 60000;
  chart.setSolves(solves);
  QVERIFY(chart.grab().toImage() != before);
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
  // It takes no focus, but Alt+Page Down and Up scroll it.
  QScrollBar *scroll = text->verticalScrollBar();
  QTRY_VERIFY(scroll->maximum() > 0);
  scroll->setValue(0);
  QTest::keyClick(window_.get(), Qt::Key_PageDown, Qt::AltModifier);
  QVERIFY(scroll->value() > 0);
  QTest::keyClick(window_.get(), Qt::Key_PageUp, Qt::AltModifier);
  QCOMPARE(scroll->value(), 0);
  // It follows the mode.
  QVERIFY(switchTo(AppMode::Library, page<LibraryPage>()));
  QTRY_VERIFY(text->toPlainText() != timed);
  QTest::keyClick(window_.get(), Qt::Key_F1);
  QTRY_VERIFY(!help->isVisible());
}

// The help panel floated is a window of its own: keys pressed while it is
// the active window still reach the page (Space starts Timed's clock).
void GuiTest::floatingHelpPanelPassesKeysOn() {
  OPEN_PAGE(timed, TimerPage, Timed);
  auto *help = page<HelpPanel>();
  QTest::keyClick(window_.get(), Qt::Key_F1);
  QTRY_VERIFY(help->isVisible());
  help->setFloating(true);
  QVERIFY(help->isWindow());
  help->activateWindow();
  QTRY_COMPARE(QApplication::activeWindow(), static_cast<QWidget *>(help));
  QVERIFY(holdSpaceUntilBusy(timed, help));
  QTest::qWait(50);
  QTest::keyClick(help, Qt::Key_A); // any key stops it
  QVERIFY(!timed->isBusy());
  help->setFloating(false);
  help->hide();
  window_->activateWindow();
  QTRY_VERIFY(window_->isActiveWindow());
}

// Space held in Timed, then another window takes the keyboard (Alt+Tab)
// and the release goes there: the hold is dropped rather than left
// waiting, nothing starts, and the next hold works as before.
void GuiTest::spaceHoldEndsWhenTheWindowIsLeft() {
  OPEN_PAGE(timed, TimerPage, Timed);
  QTest::keyPress(window_.get(), Qt::Key_Space);
  QTest::qWait(settings_.holdMs + 150);
  QVERIFY(timed->isBusy()); // the hold is shown
  QWidget other;
  other.show();
  other.activateWindow();
  QTRY_COMPARE(QApplication::activeWindow(), &other);
  QVERIFY(!timed->isBusy());
  QTest::keyRelease(&other, Qt::Key_Space); // lost to the other window
  window_->activateWindow();
  QTRY_VERIFY(window_->isActiveWindow());
  QVERIFY(!timed->isBusy());
  QVERIFY(holdSpaceUntilBusy(timed));
  QTest::qWait(50);
  QTest::keyClick(window_.get(), Qt::Key_A); // any key stops it
  QVERIFY(!timed->isBusy());
}

// São Paulo's clocks went from 00:00 to 01:00 on 4 November 2018: that
// day started at 01:00 local time (03:00 UTC), and lasted 23 hours.
void GuiTest::aDayStartsAfterAMissingMidnight() {
  const QTimeZone zone("America/Sao_Paulo");
  if (!zone.isValid() ||
      zone.offsetFromUtc(QDateTime(QDate(2018, 11, 5), QTime(12, 0),
                                   QTimeZone::UTC)) != -2 * 3600) {
    QSKIP("no history for America/Sao_Paulo here");
  }
  const QDate day(2018, 11, 4);
  const std::int64_t start = dayStart(day, zone);
  QCOMPARE(start,
           QDateTime(day, QTime(3, 0), QTimeZone::UTC).toSecsSinceEpoch());
  QCOMPARE(dayStart(day.addDays(1), zone) - start, 23 * 3600);
  QCOMPARE(dayStart(day.addDays(-1), zone) + 24 * 3600, start);
}

// A lock left by a crash under another host name is never stale by
// itself: it is named, and Start anyway takes it over.
void GuiTest::aLockFromAnotherHostCanBeTakenOver() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QString lock = dir.filePath("v-cube.lock");
  QVERIFY(writeFile(lock, "999999\nv-cube\nsome-other-host\n"));
  SingleInstance instance(dir.path());
  QVERIFY(instance.claim() == SingleInstance::Claim::Running);
  QCOMPARE(instance.lockPath(), lock);
  VERIFY_CONTAINS(instance.lockHolder(), "some-other-host");
  QVERIFY(instance.takeOver());
  // Now this copy holds it, and another is turned away or signalled.
  SingleInstance second(dir.path());
  QVERIFY(second.claim() != SingleInstance::Claim::First);
}

// docs/keys.md is written from the key table; VCUBE_WRITE_DOCS=1 writes
// it again after the table changed.
void GuiTest::keysDocMatchesTheKeyTable() {
  QFile file(QStringLiteral(VCUBE_SOURCE_DIR "/docs/keys.md"));
  QString expected = controlsMarkdown();
  if (qEnvironmentVariableIsSet("VCUBE_WRITE_DOCS")) {
    QVERIFY(writeFile(file.fileName(), expected.toUtf8()));
    return;
  }
  QVERIFY(file.open(QIODevice::ReadOnly));
  QString actual = QString::fromUtf8(file.readAll()).remove('\r');
  QVERIFY2(actual == expected,
           "docs/keys.md is out of date: run the GUI tests once with "
           "VCUBE_WRITE_DOCS=1");
}

// Two key events that look alike (the same object sent twice has the same
// address, key and time) are two key presses.
void GuiTest::theSameKeyTwiceArrivesTwice() {
  OPEN_PAGE(today, TodayPage, Today);
  auto tables = today->findChildren<QTreeWidget *>();
  QCOMPARE(tables.size(), 2);
  QVERIFY(tables[0]->topLevelItemCount() >= 3);
  window_->setFocus();
  QTest::keyClick(window_.get(), Qt::Key_Home);
  QCOMPARE(tables[0]->indexOfTopLevelItem(tables[0]->currentItem()), 0);
  QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
  QApplication::sendEvent(window_.get(), &down);
  QCOMPARE(tables[0]->indexOfTopLevelItem(tables[0]->currentItem()), 1);
  QApplication::sendEvent(window_.get(), &down);
  QCOMPARE(tables[0]->indexOfTopLevelItem(tables[0]->currentItem()), 2);
}

// A deleted set whose file name was taken before the undo comes back
// under another id, with its progress.
void GuiTest::undoneDeletionKeepsProgressUnderANewId() {
  OPEN_PAGE(library, LibraryPage, Library);
  library->selectCase("pll", "T");
  const std::size_t graded = savedProgress("pll").size();
  QVERIFY(graded > 0);
  auto *deleteSet = lastChild<QAction>(library, [](QAction *a) {
    return a->text().remove('&').startsWith("Delete set");
  });
  QVERIFY(deleteSet);
  {
    ModalWatcher watcher([](QWidget *m) {
      if (auto *box = qobject_cast<QMessageBox *>(m)) {
        box->button(QMessageBox::Yes)->click();
      }
    });
    deleteSet->trigger();
    QCOMPARE(watcher.seen, QStringList{"QMessageBox"});
  }
  const QString sets = QDir(dataDir_).filePath("algsets");
  const QString taken = QDir(sets).filePath("pll.json");
  const QString oldProgress = QDir(sets).filePath("progress/pll.json");
  finishBackgroundWrites();
  QVERIFY(!QFile::exists(taken));
  QVERIFY(!QFile::exists(oldProgress));
  QVERIFY(writeFile(taken, "{}"));

  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
  window_->setFocus();
  QTest::keySequence(window_.get(), QKeySequence::Undo);
  QVERIFY(QFile::remove(taken));
  QCOMPARE(savedAlgs("pll-2", "T"), std::vector<std::string>{kTPerm});
  QCOMPARE(savedProgress("pll-2").size(), graded);
  QVERIFY(!QFile::exists(oldProgress));

  // Redo deletes the set that came back, and undo brings it back again.
  QTest::keySequence(window_.get(), QKeySequence::Redo);
  QVERIFY(!QFile::exists(QDir(sets).filePath("pll-2.json")));
  QTest::keySequence(window_.get(), QKeySequence::Undo);
  QCOMPARE(savedAlgs("pll-2", "T"), std::vector<std::string>{kTPerm});
  QCOMPARE(savedProgress("pll-2").size(), graded);
}

void GuiTest::timedDriverDoesWhatTheFlowAsks() {
  // The host Drill and the practice Timed tabs give the driver: what it
  // was asked to do, in order.
  struct Host : TimedHost {
    QStringList calls;
    bool done = false;
    void timedNext(bool fromIdle) override {
      calls << (fromIdle ? "next from idle" : "next");
    }
    void timedRetry() override { calls << "retry"; }
    void timedResetCube() override { calls << "reset"; }
    void timedRotate(Move) override { calls << "rotate"; }
    void timedStarted(std::int64_t) override { calls << "started"; }
    std::optional<DrillFlow::Turn> timedTurn(Move m, std::int64_t) override {
      calls << "turn";
      return isRotation(m.type) ? DrillFlow::Turn::Rotation
                                : DrillFlow::Turn::OffUdFace;
    }
    bool timedStepDone() override { return done; }
    void timedFinish(std::int64_t, DrillFlow::Result r) override {
      calls << (r == DrillFlow::Result::Solved   ? "solved"
                : r == DrillFlow::Result::Failed ? "failed"
                                                 : "gave up");
    }
    void timedPractise(bool) override { calls << "practise"; }
    void timedToggleWrong() override { calls << "wrong"; }
    void timedDelete() override { calls << "delete"; }
    void timedFinishAnimations() override {}
    void timedShowKeys() override {}
    void timedSay(const QString &text) override { calls << "say " + text; }
    void timedCancelled(const QString &why) override {
      calls << "cancelled " + why;
    }
  } host;
  DrillSettings drill;
  drill.tapToArm = true;
  drill.autoStop = true;
  drill.shiftSpaceGivesUp = true;
  TimeDisplay display;
  TimedDriver driver(host, drill);
  driver.setDisplay(&display);
  DrillFlow::Config config;
  config.virtualCube = true;
  driver.flow().setConfig(config);
  driver.flow().setUp(0);
  auto press = [](TrainKey key, bool shift = false) {
    TrainKeyEvent k;
    k.key = key;
    k.shift = shift;
    return k;
  };
  auto turn = [](MoveType t) {
    TrainKeyEvent k;
    k.move = Move{t, 1};
    return k;
  };
  auto release = [&press] {
    TrainKeyEvent k = press(TrainKey::Space);
    k.press = false;
    return k;
  };
  std::int64_t now = 1000;
  // A face turn before the timer can start is said, a rotation looks.
  QVERIFY(driver.key(turn(MoveType::R), now, Scramble::Shown));
  QCOMPARE(host.calls.takeFirst(),
           "say " + timedHintText(DrillFlow::Hint::OnlyRotations, drill));
  QVERIFY(driver.key(turn(MoveType::Y), now, Scramble::Shown));
  QCOMPARE(host.calls.takeFirst(), QString("rotate"));
  // Backspace says which key gives up.
  driver.key(press(TrainKey::Backspace), now, Scramble::Shown);
  QVERIFY(host.calls.takeFirst().contains("Shift+Space"));
  // Armed, the first face turn starts the timer and is a turn of the
  // solve; the turn that does the step stops it.
  driver.key(press(TrainKey::Space), now, Scramble::Shown);
  driver.key(release(), now, Scramble::Shown);
  host.calls.clear();
  driver.key(turn(MoveType::R), now += 100, Scramble::Shown);
  QCOMPARE(host.calls, QStringList({"started", "turn"}));
  host.calls.clear();
  host.done = true;
  driver.key(turn(MoveType::U), now += 500, Scramble::Shown);
  QCOMPARE(host.calls, QStringList({"turn", "solved"}));
  host.calls.clear();
  // Esc during a solve cancels it, saying so.
  driver.flow().setUp(now);
  driver.key(press(TrainKey::Space), now, Scramble::Shown);
  driver.key(release(), now, Scramble::Shown);
  host.done = false;
  driver.key(turn(MoveType::R), now += 100, Scramble::Shown);
  host.calls.clear();
  driver.key(press(TrainKey::Escape), now += 100, Scramble::Shown);
  QCOMPARE(host.calls, QStringList({"reset", "cancelled " + cancelledText()}));
  QCOMPARE(driver.flow().phase(), DrillFlow::Phase::Waiting);
  // Shift+Space gives up.
  host.calls.clear();
  driver.key(press(TrainKey::Space, true), now, Scramble::Shown);
  QCOMPARE(host.calls, QStringList({"gave up"}));
}

// The main window has no Tab focus (Tab is a page key): every control
// that can't take the focus can be reached from the keyboard another way,
// through the command list (which lists the page's buttons and choices),
// a key of its own or one its tip names.
void GuiTest::mouseOnlyControlsHaveAKey() {
  static const QRegularExpression namedKey(
      R"(\((?:[^()]*\b)?(?:Ctrl|Alt|Shift|F\d+|Space|Enter|Esc|Tab|Home|End|Delete|Backspace|[←→↑↓]|[A-Z0-9])(?![A-Za-z0-9])[^()]*\)\s*$)");
  QVERIFY2(namedKey.isValid(), qPrintable(namedKey.errorString()));
  for (AppMode::Id mode : AppMode::all()) {
    // The pages are stacked in the order of the modes.
    ModePage *expected = nullptr;
    for (QStackedWidget *stack : window_->findChildren<QStackedWidget *>()) {
      if (auto *p = qobject_cast<ModePage *>(stack->widget(mode))) {
        expected = p;
      }
    }
    QVERIFY(expected);
    QVERIFY(switchTo(mode, expected));
    ModePage *page = expected;
    QStringList listed;
    for (const auto &c : CommandPalette::fromControls(page)) {
      listed << c.text;
    }
    QStringList missing;
    auto reachable = [&](QWidget *w, const QString &text) {
      QString tip = w->toolTip().simplified();
      if (namedKey.match(tip).hasMatch()) {
        return true;
      }
      if (auto *b = qobject_cast<QAbstractButton *>(w);
          b && !b->shortcut().isEmpty()) {
        return true;
      }
      if (auto *t = qobject_cast<QToolButton *>(w);
          t && t->defaultAction() &&
          !t->defaultAction()->shortcut().isEmpty()) {
        return true;
      }
      return !text.isEmpty() &&
             std::ranges::any_of(listed, [&](const QString &entry) {
               return entry.startsWith(text) || entry.contains(": " + text);
             });
    };
    for (QAbstractButton *b : page->findChildren<QAbstractButton *>()) {
      if (b->focusPolicy() != Qt::NoFocus || !b->isVisibleTo(page) ||
          !b->isEnabled() || b->window() != window_.get()) {
        continue;
      }
      // As the command list writes it.
      QString text =
          QString(b->text()).remove('&').remove(QStringLiteral("…")).trimmed();
      if (!reachable(b, text)) {
        missing << QString("%1 button \"%2\" (%3)")
                       .arg(AppMode::label(mode), text, b->toolTip());
      }
    }
    for (QComboBox *box : page->findChildren<QComboBox *>()) {
      if (box->focusPolicy() != Qt::NoFocus || !box->isVisibleTo(page) ||
          !box->isEnabled() || box->isEditable() || box->count() == 0) {
        continue;
      }
      if (!reachable(box, box->itemText(0))) {
        missing << QString("%1 choice \"%2\" (%3)")
                       .arg(AppMode::label(mode), box->currentText(),
                            box->toolTip());
      }
    }
    QVERIFY2(missing.isEmpty(), qPrintable(missing.join("\n")));
  }
}

// The review's buttons take no focus: C, W and R do what they do, and the
// key strip says so.
void GuiTest::reviewButtonsHaveKeys() {
  ReviewedSolve solve;
  solve.title = "Test solve";
  solve.scramble = parseAlgOrEmpty("R U R' U'");
  solve.solution = parseAlgOrEmpty("U R U' R'");
  ReviewDialog dialog(solve, settings_, nullptr, window_.get());
  dialog.setRetryEnabled(true);
  QSignalSpy retried(&dialog, &ReviewDialog::retryRequested);
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  dialog.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&dialog));
  HintBar *keys = dialog.findChild<HintBar *>();
  QVERIFY(keys);
  const QStringList shown = keyNames(keys->keys());
  QVERIFY2(shown.contains("C") && shown.contains("W") && shown.contains("R"),
           qPrintable(shown.join(", ")));
  QApplication::clipboard()->clear();
  QTest::keyClick(&dialog, Qt::Key_C);
  VERIFY_CONTAINS(QApplication::clipboard()->text(), "R U R' U'");
  QTest::keyClick(&dialog, Qt::Key_R);
  QCOMPARE(retried.count(), 1);
  QVERIFY(!dialog.isVisible());
  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
}

// A file that can't be saved puts up the banner, which File > Dismiss save
// warning hides as its button does.
void GuiTest::saveWarningCanBeDismissedFromTheMenu() {
  auto *dismiss = lastChild<QAction>(window_.get(), [](QAction *a) {
    return a->text() == "Dismiss save warning";
  });
  QVERIFY(dismiss);
  QVERIFY(!dismiss->isVisible());
  QWidget *banner = window_->findChild<QWidget *>("saveBanner");
  QVERIFY(banner);
  reportWrite(QDir(dataDir_).filePath("nowhere.json"), false, "disk full");
  QTRY_VERIFY(banner->isVisible());
  QVERIFY(dismiss->isVisible());
  dismiss->trigger();
  QVERIFY(!banner->isVisible());
  QVERIFY(!dismiss->isVisible());
  // Another failure brings it back.
  reportWrite(QDir(dataDir_).filePath("other.json"), false, "disk full");
  QTRY_VERIFY(banner->isVisible());
  // Saved after all: nothing is left for the check when quitting.
  reportWrite(QDir(dataDir_).filePath("nowhere.json"), true, QString());
  reportWrite(QDir(dataDir_).filePath("other.json"), true, QString());
  QTRY_VERIFY(!banner->isVisible());
}

// The timer is named "Timer"; what it shows is said in its description,
// though not on every frame of a running solve.
void GuiTest::timerSaysItsResult() {
  TimeDisplay display;
  display.showTime(12340, Penalty::Plus2);
  QCOMPARE(display.accessibleName(), QString("Timer"));
  QCOMPARE(display.accessibleDescription(), display.text());
  QVERIFY(display.text().contains("+"));
  display.showText("DNF");
  QCOMPARE(display.accessibleDescription(), QString("DNF"));
}

// F10 is focus mode, so the menu bar is reached without a mnemonic by a
// bare Alt press, as the style offers (Fusion included).
void GuiTest::bareAltReachesTheMenuBar() {
  QVERIFY(switchTo(AppMode::Today, page<TodayPage>()));
  window_->activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
  QVERIFY(window_->style()->styleHint(QStyle::SH_MenuBar_AltKeyNavigation));
  QWidget *target =
      QApplication::focusWidget() ? QApplication::focusWidget() : window_.get();
  QTest::keyPress(target, Qt::Key_Alt, Qt::AltModifier);
  QTest::keyRelease(target, Qt::Key_Alt);
  QTRY_VERIFY(window_->menuBar()->activeAction() != nullptr);
  QTest::keyClick(window_->menuBar(), Qt::Key_Escape);
  QTRY_VERIFY(window_->menuBar()->activeAction() == nullptr);
  window_->setFocus();
}

// Everything that couldn't be read at start-up is listed in one window
// after the window is shown, not in a message box each.
void GuiTest::startupProblemsShowInOneWindow() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(writeFile(dir.filePath("sessions.json"), "not json"));
  QVERIFY(writeFile(dir.filePath("f2l.json"), "not json"));
  QVERIFY(writeFile(dir.filePath("tsle.json"), "not json either"));
  QVERIFY(writeFile(dir.filePath("algsets/broken.json"), "{"));
  ModalWatcher watcher;
  MainWindow second(settings_, dir.path());
  QCOMPARE(watcher.seen, QStringList{});
  const QStringList problems = second.startupProblems();
  QVERIFY2(problems.size() >= 4, qPrintable(problems.join("\n")));
  for (const char *file : {"f2l.json", "tsle.json", "broken.json"}) {
    QVERIFY2(std::ranges::any_of(
                 problems, [&](const QString &p) { return p.contains(file); }),
             file);
  }
  second.showStartupProblems();
  QCOMPARE(watcher.seen, QStringList{}); // not modal
  auto *list = second.findChild<QTextBrowser *>("startupProblems");
  QVERIFY(list);
  QVERIFY(list->window()->isVisible());
  QString shown = list->toPlainText();
  for (const QString &p : problems) {
    QVERIFY2(shown.contains(p), qPrintable(p));
  }
  QVERIFY(lastChild<QPushButton>(list->window(), [](QPushButton *b) {
    return b->text() == "Open data folder";
  }));
  list->window()->close();
  window_->activateWindow();
}

void GuiTest::cleanupTestCase() {
  if (window_) {
    window_->close();
    // The window is destroyed as one that is shown and active, with the
    // focus in Library: the pages hear that the focus has gone, which must
    // be before the data they refer to goes (a sanitizer build sees it).
    auto *library = page<LibraryPage>();
    window_->show();
    window_->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(window_.get()));
    QVERIFY(switchTo(AppMode::Library, library));
    {
      AlgLibrary lib(QDir(dataDir_).filePath("algsets"), ":/algsets");
      lib.load();
      QVERIFY(lib.find("oll"));
      library->selectCase("oll",
                          qs(lib.find("oll")->groups.front().cases.front().id));
    }
    auto *list = library->findChild<AlgListWidget *>();
    QVERIFY(list);
    list->setFocus();
    QCOMPARE(QApplication::focusWidget(), list);
    window_.reset();
  }
  instance_.reset();
  QThreadPool::globalInstance()->waitForDone();
  finishBackgroundWrites();
}

int main(int argc, char **argv) {
  // Unbuffered, so that what was printed is seen even if the test dies.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
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
