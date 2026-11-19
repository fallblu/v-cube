#include "app/main_window.hpp"

#include "io/backup.hpp"
#include "io/data_file.hpp"

#include "app/alglab_page.hpp"
#include "app/command_palette.hpp"
#include "app/drill_page.hpp"
#include "app/eo_cross_page.hpp"
#include "app/f2l_page.hpp"
#include "app/help_panel.hpp"
#include "app/key_router.hpp"
#include "app/learn_page.hpp"
#include "app/library_page.hpp"
#include "app/move_count_page.hpp"
#include "app/platform.hpp"
#include "app/review_dialog.hpp"
#include "app/settings_dialog.hpp"
#include "app/sounds.hpp"
#include "app/text_style.hpp"
#include "app/timer_page.hpp"
#include "app/today_page.hpp"
#include "app/tsle_page.hpp"
#include "app/ui_state.hpp"
#include "app/ui_text.hpp"
#include "app/untimed_page.hpp"
#include "app/widgets.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace vcube {

namespace {

// Swallows every tooltip in the app, so nothing pops up beside the mouse.
class NoToolTips : public QObject {
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject *, QEvent *e) override {
    return e->type() == QEvent::ToolTip;
  }
};

} // namespace

MainWindow::MainWindow(AppSettings &settings, QString dataDir, QWidget *parent)
    : QMainWindow(parent), settings_(settings), data_(std::move(dataDir)) {
  setWindowTitle("v-cube");
  setWriteReporter([this](const QString &path, const QString &error) {
    writeReported(path, error);
  });
  AppData::LoadReport loaded = data_.load();
  if (!loaded.backupError.isEmpty()) {
    failedWrites_[data_.backupsDir()] = loaded.backupError;
  }
  for (const QString &warning : loaded.warnings) {
    QMessageBox::warning(this, "v-cube", warning);
  }

  stack_ = new QStackedWidget(this);
  learnPage_ = new LearnPage(settings_, data_.library, stack_);
  drillPage_ = new DrillPage(settings_, data_.library, stack_);
  libraryPage_ = new LibraryPage(settings_, data_.library, stack_);
  timedPage_ = new TimerPage(settings_, data_.sessions, data_.library, stack_);
  untimedPage_ =
      new UntimedPage(settings_, data_.untimed, data_.library, stack_);
  moveCountPage_ =
      new MoveCountPage(settings_, data_.moveCounts, data_.library, stack_);
  eoCrossPage_ = new EoCrossPage(settings_, data_.dataDir(), stack_);
  algLabPage_ = new AlgLabPage(settings_, data_.library, data_.lab, stack_);
  f2lPage_ =
      new F2LPage(settings_, data_.library, data_.lab, data_.dataDir(), stack_);
  tslePage_ = new TslePage(settings_, data_.dataDir(), stack_);
  todayPage_ =
      new TodayPage(settings_, data_.library, data_.sessions, data_.untimed,
                    data_.moveCounts, data_.lab, data_.dataDir(), stack_);
  pages_ = {learnPage_,   drillPage_,     libraryPage_, timedPage_,
            untimedPage_, moveCountPage_, eoCrossPage_, algLabPage_,
            f2lPage_,     tslePage_,      todayPage_};
  for (ModePage *page : pages_) {
    stack_->addWidget(page);
    connect(page, &ModePage::message, this, &MainWindow::showMessage);
  }
  connect(libraryPage_, &LibraryPage::setsChanged, this,
          [this](const QString &setId) {
            learnPage_->reloadSets();
            drillPage_->reloadSets();
            timedPage_->refreshScrambleTypes(setId);
            untimedPage_->refreshScrambleTypes(setId);
            algLabPage_->reloadSets();
            f2lPage_->reloadSets();
            updateBadges();
          });
  libraryPage_->setLab(&data_.lab);
  connect(libraryPage_, &LibraryPage::practiceRequested, this,
          [this](int mode, const QString &setId, const QString &caseId) {
            if (!setMode(mode)) {
              return;
            }
            CasePage *page = mode == AppMode::Drill
                                 ? static_cast<CasePage *>(drillPage_)
                                 : static_cast<CasePage *>(learnPage_);
            page->showCase(setId, caseId);
          });
  connect(libraryPage_, &LibraryPage::algLabRequested, this,
          [this](const QString &setId, const QString &caseId) {
            if (setMode(AppMode::AlgLab)) {
              algLabPage_->selectCase(setId, caseId);
            }
          });
  algLabPage_->setSetUndo([this] { return libraryPage_->undoLast(); });
  algLabPage_->setAlgEditor([this](const QString &setId, const QString &caseId,
                                   std::vector<std::string> algs,
                                   const QString &text) {
    return libraryPage_->setCaseAlgs(setId, caseId, std::move(algs), text);
  });
  connect(algLabPage_, &AlgLabPage::setsChanged, this,
          [this](const QString &setId) {
            learnPage_->reloadSets();
            drillPage_->reloadSets();
            timedPage_->refreshScrambleTypes(setId);
            untimedPage_->refreshScrambleTypes(setId);
            updateBadges();
          });
  connect(todayPage_, &TodayPage::learnSetRequested, this,
          &MainWindow::learnSet);
  connect(todayPage_, &TodayPage::practiceRequested, this, [this](int mode) {
    PracticePage *page = mode == AppMode::F2L
                             ? static_cast<PracticePage *>(f2lPage_)
                             : static_cast<PracticePage *>(tslePage_);
    if (setMode(mode)) {
      page->showCards();
    }
  });
  for (CasePage *page : {static_cast<CasePage *>(learnPage_),
                         static_cast<CasePage *>(drillPage_)}) {
    connect(page, &CasePage::editCaseRequested, this,
            [this](const QString &setId, const QString &caseId) {
              if (setMode(AppMode::Library)) {
                libraryPage_->selectCase(setId, caseId);
              }
            });
  }

  auto *central = new QWidget(this);
  auto *layout = new QHBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  sidebar_ = buildSidebar();
  layout->addWidget(sidebar_);
  auto *right = new QVBoxLayout;
  right->setContentsMargins(0, 0, 0, 0);
  right->setSpacing(0);
  right->addWidget(buildBanner());
  right->addWidget(stack_, 1);
  layout->addLayout(right, 1);
  setCentralWidget(central);

  focusHint_ = new QLabel(central);
  focusHint_->setAttribute(Qt::WA_TransparentForMouseEvents);
  focusHint_->setStyleSheet(
      "QLabel { background: rgba(0, 0, 0, 170); color: white; "
      "border-radius: 6px; padding: 6px 14px; }");
  focusHint_->hide();
  focusHintTimer_ = new QTimer(this);
  focusHintTimer_->setSingleShot(true);
  connect(focusHintTimer_, &QTimer::timeout, focusHint_, &QWidget::hide);

  // Each mode's side panel, hidden or shown as it was left.
  for (int m = 0; m < AppMode::Count; ++m) {
    ModePage *page = pages_[static_cast<std::size_t>(m)];
    QString id = AppMode::settingsId(m);
    page->setSideHidden(saved::sideHidden(id).get());
    connect(page, &ModePage::sideHiddenToggled, this,
            [this, page, id](bool hidden) {
              saved::sideHidden(id).set(hidden);
              if (page == currentPage() && sidePanelAction_) {
                sidePanelAction_->setChecked(!hidden);
              }
            });
  }

  badgeTimer_ = new QTimer(this);
  badgeTimer_->setInterval(60 * 1000);
  connect(badgeTimer_, &QTimer::timeout, this, &MainWindow::updateBadges);
  badgeTimer_->start();

  help_ = new HelpPanel(this);
  addDockWidget(Qt::RightDockWidgetArea, help_);
  resizeDocks({help_}, {340}, Qt::Horizontal);
  help_->hide();
  buildMenus();
  qApp->installEventFilter(new NoToolTips(this));
  qApp->installEventFilter(
      new KeyRouter(this, [this] { return currentPage(); }, this));

  if (!data_.library.errors().isEmpty()) {
    QMessageBox::warning(this, "v-cube",
                         tr("Some alg set files had problems:") + "\n\n" +
                             data_.library.errors().join("\n"));
  }
  restoreGeometry(saved::windowGeometry().get());
  if (!saved::windowGeometry().exists()) {
    resize(1280, 800);
  }
  restoreState(saved::windowState().get());
  // The app always opens out of full screen and focus mode, however it was
  // closed; the window keeps its size, position and maximised state.
  setWindowState(windowState() & ~Qt::WindowFullScreen);
  fitToScreen();
  restoreLayout();
  setSidebarShown(saved::sidebarShown().get());
  int mode = AppMode::fromSettingsId(saved::windowMode().get());
  if (mode < 0) {
    mode = AppMode::fromSavedTab(saved::legacyTab().get());
  }
  saved::removeObsolete();
  applyWindowSettings();
  setMode(mode);
  updateBadges();
  updateBanner();
}

MainWindow::~MainWindow() { setWriteReporter({}); }

void MainWindow::raiseWindow() {
  if (isMinimized()) {
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  }
  show();
  raise();
  activateWindow();
}

QWidget *MainWindow::buildSidebar() {
  auto *bar = new QWidget(this);
  bar->setObjectName("modeSidebar");
  bar->setAutoFillBackground(true);
  bar->setBackgroundRole(QPalette::Base); // follows the theme
  bar->setFocusPolicy(Qt::NoFocus);
  auto *layout = new QVBoxLayout(bar);
  layout->setContentsMargins(8, 10, 8, 10);
  layout->setSpacing(2);
  modeButtons_ = new QButtonGroup(this);
  modeButtons_->setExclusive(true);
  auto header = [&](const QString &text) {
    auto *label = new QLabel(text, bar);
    setTextRole(label, TextRole::Caption, true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setContentsMargins(6, 10, 0, 4);
    layout->addWidget(label);
  };
  for (int m : AppMode::sidebarOrder()) {
    if (QString h = AppMode::sidebarHeader(m); !h.isEmpty()) {
      header(h);
    }
    auto *b = new QToolButton(bar);
    b->setText(AppMode::label(m));
    b->setToolTip(tr("%1 (%2)").arg(
        AppMode::label(m),
        AppMode::shortcut(m).toString(QKeySequence::NativeText)));
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    modeButtons_->addButton(b, m);
    layout->addWidget(b);
  }
  layout->addStretch();
  connect(modeButtons_, &QButtonGroup::idClicked, this,
          [this](int m) { setMode(m); });
  return bar;
}

void MainWindow::styleSidebar() {
  // Set again after a theme change so that the palette() colours follow.
  sidebar_->setStyleSheet(
      "QToolButton { text-align: left; padding: 6px 18px 6px 14px; border: "
      "none; border-radius: 4px; }"
      "QToolButton:hover { background: palette(button); }"
      "QToolButton:checked { background: palette(highlight); color: "
      "palette(highlighted-text); }");
}

QWidget *MainWindow::buildBanner() {
  banner_ = new QWidget(this);
  banner_->setObjectName("saveBanner");
  banner_->setAttribute(Qt::WA_StyledBackground);
  // The same in both themes: white on a dark red.
  banner_->setStyleSheet("#saveBanner { background: #8e1f1f; }"
                         "#saveBanner QLabel { color: white; }");
  auto *layout = new QHBoxLayout(banner_);
  layout->setContentsMargins(12, 6, 8, 6);
  bannerText_ = new QLabel(banner_);
  bannerText_->setWordWrap(true);
  bannerText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto *folder = new QPushButton(tr("Open data folder"), banner_);
  folder->setFocusPolicy(Qt::NoFocus);
  connect(folder, &QPushButton::clicked, this, &MainWindow::openDataFolder);
  auto *dismiss = new QPushButton(tr("Dismiss"), banner_);
  dismiss->setFocusPolicy(Qt::NoFocus);
  dismiss->setToolTip(tr("Hide this until a file can't be saved again"));
  connect(dismiss, &QPushButton::clicked, this, [this] {
    failedWrites_.clear();
    updateBanner();
  });
  layout->addWidget(bannerText_, 1);
  layout->addWidget(folder);
  layout->addWidget(dismiss);
  banner_->hide();
  return banner_;
}

void MainWindow::writeReported(const QString &path, const QString &error) {
  if (error.isEmpty()) {
    if (failedWrites_.erase(path) > 0) {
      updateBanner();
    }
    return;
  }
  failedWrites_[path] = error;
  updateBanner();
}

void MainWindow::updateBanner() {
  if (!banner_) {
    return;
  }
  if (failedWrites_.empty()) {
    banner_->hide();
    return;
  }
  QStringList lines;
  for (const auto &[path, error] : failedWrites_) {
    lines << QString("<b>%1</b>: %2")
                 .arg(QFileInfo(path).fileName().toHtmlEscaped(),
                      error.toHtmlEscaped());
  }
  bannerText_->setText(
      tr("Your changes are not being saved.") + "<br>" + lines.join("<br>") +
      "<br>" +
      tr("Check that the data folder can be written to; the next save "
         "tries again."));
  banner_->show();
}

void MainWindow::updateHelp() {
  if (help_) {
    help_->setContext(mode_, settings_.keymap);
  }
}

ModePage *MainWindow::currentPage() const {
  return mode_ >= 0 ? pages_[static_cast<std::size_t>(mode_)] : nullptr;
}

bool MainWindow::setMode(int mode) {
  mode = std::clamp(mode, 0, AppMode::Count - 1);
  ModePage *old = currentPage();
  if (mode == mode_) {
    return true;
  }
  if (old && old->isBusy()) {
    // Keep the sidebar on the running mode.
    modeButtons_->button(mode_)->setChecked(true);
    showMessage(tr("Finish or cancel the solve (Esc) before switching modes"));
    return false;
  }
  if (old) {
    // A text field on the old page must not keep the keyboard.
    if (QWidget *focus = QApplication::focusWidget();
        focus && old->isAncestorOf(focus)) {
      focus->clearFocus();
    }
    old->deactivated();
  }
  bool switching = mode_ >= 0;
  mode_ = mode;
  modeButtons_->button(mode)->setChecked(true);
  // Only the page shown sets the window's minimum size: the stack would
  // otherwise take the largest minimum of all of them.
  for (ModePage *page : pages_) {
    auto policy =
        page == currentPage() ? QSizePolicy::Preferred : QSizePolicy::Ignored;
    page->setSizePolicy(policy, policy);
  }
  stack_->setCurrentWidget(currentPage());
  currentPage()->activated();
  saved::windowMode().set(AppMode::settingsId(mode));
  if (switching && !sidebar_->isVisible()) {
    // Nothing else shows which mode this is.
    showOverlay(AppMode::label(mode), 1500);
  }
  if (modeMenu_) {
    modeMenu_->setTitle(AppMode::label(mode));
    modeMenu_->menuAction()->setVisible(currentPage()->hasModeMenu());
  }
  if (sidePanelAction_) {
    sidePanelAction_->setEnabled(currentPage()->sidePanel() != nullptr);
    sidePanelAction_->setChecked(!currentPage()->sideHidden());
  }
  if (pageUndoAction_) {
    // Library has its own Undo and Redo; these modes take Ctrl+Z.
    pageUndoAction_->setVisible(
        mode == AppMode::Learn || mode == AppMode::Drill ||
        mode == AppMode::Timed || mode == AppMode::Untimed ||
        mode == AppMode::MoveCount || mode == AppMode::AlgLab);
  }
  updateHelp();
  updateBadges();
  return true;
}

void MainWindow::updateBadges() {
  TodayPage::Totals t = todayPage_->totals();
  auto label = [&t](int m) {
    QString text = AppMode::label(m);
    if (t.due > 0) {
      text += QString("  (%1)").arg(t.due);
    }
    return text;
  };
  for (int m : {AppMode::Today, AppMode::Learn}) {
    auto *b = modeButtons_->button(m);
    b->setText(label(m));
    b->setToolTip(
        tr("%1 (%2): %n review(s) due", "", t.due)
            .arg(AppMode::label(m),
                 AppMode::shortcut(m).toString(QKeySequence::NativeText)) +
        (t.newLeft > 0 ? tr(", %n new case(s) left today", "", t.newLeft)
                       : QString()));
  }
}

void MainWindow::learnSet(const QString &setId) {
  if (!setMode(AppMode::Learn)) {
    return;
  }
  // Pick the set in Learn's set list, as a click would.
  for (QComboBox *box : learnPage_->findChildren<QComboBox *>()) {
    int i = box->findData(setId);
    if (i >= 0) {
      box->setCurrentIndex(i);
      emit box->activated(i);
      return;
    }
  }
}

void MainWindow::setFullScreen(bool on) {
  setWindowState(on ? windowState() | Qt::WindowFullScreen
                    : windowState() & ~Qt::WindowFullScreen);
}

void MainWindow::setFocusMode(bool on) {
  focusMode_ = on;
  sidebar_->setVisible(!on && sidebarAction_->isChecked());
  menuBar()->setVisible(!on);
  statusBar()->setVisible(!on);
  for (ModePage *page : pages_) {
    page->setFocusMode(on);
  }
  if (on) {
    showFocusHint();
  } else {
    focusHint_->hide();
  }
}

void MainWindow::setSidebarShown(bool on) {
  if (sidebarAction_->isChecked() != on) {
    sidebarAction_->setChecked(on); // comes back here
    return;
  }
  sidebar_->setVisible(on && !focusMode_);
  saved::sidebarShown().set(on);
}

void MainWindow::showFocusHint() {
  showOverlay(tr("Focus mode: F10 or right-click to show everything again"),
              3500);
}

void MainWindow::showOverlay(const QString &text, int ms) {
  focusHint_->setText(text);
  focusHint_->adjustSize();
  // Placed once the layout has settled (at start-up, after the window is
  // shown).
  QTimer::singleShot(0, focusHint_, [this] {
    QWidget *area = centralWidget();
    // At the bottom, clear of the page's header and scramble.
    focusHint_->move((area->width() - focusHint_->width()) / 2,
                     area->height() - focusHint_->height() - 16);
    focusHint_->raise();
    focusHint_->show();
  });
  focusHintTimer_->start(ms);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *e) {
  QMenu menu(this);
  menu.addAction(fullScreenAction_);
  menu.addAction(focusAction_);
  menu.addAction(sidebarAction_);
  menu.exec(e->globalPos());
}

void MainWindow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::WindowStateChange && fullScreenAction_) {
    fullScreenAction_->setChecked(isFullScreen());
  }
  if (e->type() == QEvent::ActivationChange && !isActiveWindow()) {
    if (ModePage *page = currentPage()) {
      page->windowDeactivated();
    }
  }
  QMainWindow::changeEvent(e);
}

void MainWindow::closeEvent(QCloseEvent *e) {
  if (ModePage *page = currentPage(); page && page->isBusy()) {
    // Ctrl+Q is a slip away from Q, a turning key.
    auto answer = QMessageBox::question(
        this, tr("Quit v-cube"),
        tr("A solve is running. Quit anyway? It won't be recorded."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      e->ignore();
      return;
    }
  }
  saved::windowGeometry().set(saveGeometry());
  saved::windowState().set(saveState());
  saveLayout();
  finishBackgroundWrites();
  QMainWindow::closeEvent(e);
}

void MainWindow::restoreLayout() {
  for (int m = 0; m < AppMode::Count; ++m) {
    if (m == AppMode::Learn) {
      continue; // Learn keeps its own splitters
    }
    const auto splitters =
        pages_[static_cast<std::size_t>(m)]->findChildren<QSplitter *>();
    for (qsizetype i = 0; i < splitters.size(); ++i) {
      // By name; a splitter with no name, or sizes kept before they had
      // names, by its place among the page's splitters.
      QString id = AppMode::settingsId(m);
      QString name = splitters[i]->objectName();
      QByteArray state =
          name.isEmpty() ? QByteArray() : saved::splitter(id, name).get();
      if (state.isEmpty()) {
        state = saved::splitter(id, static_cast<int>(i)).get();
      }
      if (!state.isEmpty()) {
        splitters[i]->restoreState(state);
      }
    }
  }
}

void MainWindow::saveLayout() {
  for (int m = 0; m < AppMode::Count; ++m) {
    if (m == AppMode::Learn) {
      continue; // Learn keeps its own splitters
    }
    const auto splitters =
        pages_[static_cast<std::size_t>(m)]->findChildren<QSplitter *>();
    for (qsizetype i = 0; i < splitters.size(); ++i) {
      QString id = AppMode::settingsId(m);
      QString name = splitters[i]->objectName();
      if (name.isEmpty()) {
        saved::splitter(id, static_cast<int>(i)).set(splitters[i]->saveState());
      } else {
        saved::splitter(id, name).set(splitters[i]->saveState());
      }
    }
  }
}

void MainWindow::fitToScreen() {
  if (isMaximized() || isFullScreen()) {
    return;
  }
  QScreen *screen = QGuiApplication::screenAt(geometry().center());
  if (!screen) {
    screen = QGuiApplication::primaryScreen(); // off every screen
  }
  if (!screen) {
    return;
  }
  QRect avail = screen->availableGeometry();
  QRect g = geometry();
  bool moved = !avail.intersects(g);
  if (g.width() > avail.width() || g.height() > avail.height()) {
    // Saved at another scaling: bigger than the screen now is.
    g.setSize(g.size().boundedTo(avail.size() * 0.9));
    moved = true;
  }
  if (moved) {
    g.moveCenter(avail.center());
    setGeometry(g);
  }
}

void MainWindow::buildMenus() {
  QMenu *file = menuBar()->addMenu(tr("&File"));
  file->addAction(tr("&Import alg set…"), this, &MainWindow::importAlgSet);
  file->addAction(tr("&Export alg set…"), this, &MainWindow::exportAlgSet);
  file->addSeparator();
  file->addAction(tr("Export all data…"), this, &MainWindow::exportAllData);
  file->addAction(tr("Open data folder"), this, &MainWindow::openDataFolder);
  file->addSeparator();
  file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu *edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(tr("&Command list…"), QKeySequence(Qt::CTRL | Qt::Key_K),
                  this, &MainWindow::openCommandPalette);
  edit->addSeparator();
  edit->addAction(tr("&Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                  this, [this] { openSettings(); });
  edit->addAction(tr("&Colour neutrality…"),
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this,
                  [this] { openSettings(true); });

  QMenu *view = menuBar()->addMenu(tr("&View"));
  fullScreenAction_ = view->addAction(tr("&Full screen"));
  fullScreenAction_->setCheckable(true);
  fullScreenAction_->setChecked(isFullScreen());
  QList<QKeySequence> fullScreenKeys = {QKeySequence(Qt::Key_F11)};
  for (const QKeySequence &k :
       QKeySequence::keyBindings(QKeySequence::FullScreen)) {
    if (!fullScreenKeys.contains(k)) {
      fullScreenKeys << k;
    }
  }
  fullScreenAction_->setShortcuts(fullScreenKeys);
  connect(fullScreenAction_, &QAction::toggled, this,
          &MainWindow::setFullScreen);
  focusAction_ = view->addAction(tr("F&ocus mode"));
  focusAction_->setCheckable(true);
  focusAction_->setShortcut(QKeySequence(Qt::Key_F10));
  focusAction_->setToolTip(
      tr("Hide everything but the scramble, timer, cube and algs"));
  connect(focusAction_, &QAction::toggled, this, &MainWindow::setFocusMode);
  sidebarAction_ = view->addAction(tr("&Sidebar"));
  sidebarAction_->setCheckable(true);
  sidebarAction_->setChecked(true);
  sidebarAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(sidebarAction_, &QAction::toggled, this,
          &MainWindow::setSidebarShown);
  sidePanelAction_ = view->addAction(tr("Side &panel"));
  sidePanelAction_->setCheckable(true);
  sidePanelAction_->setChecked(true);
  sidePanelAction_->setShortcut(QKeySequence(Qt::Key_F9));
  sidePanelAction_->setToolTip(
      tr("The mode's case list or session, beside the cube"));
  connect(sidePanelAction_, &QAction::triggered, this, [this](bool on) {
    if (ModePage *page = currentPage()) {
      page->setSideHidden(!on);
    }
  });
  QAction *help = help_->toggleViewAction();
  help->setText(tr("&Keyboard controls"));
  help->setShortcut(QKeySequence(Qt::Key_F1));
  view->addAction(help);

  QMenu *modes = menuBar()->addMenu(tr("&Go"));
  for (int m : AppMode::sidebarOrder()) {
    modes->addAction(AppMode::label(m), AppMode::shortcut(m), this,
                     [this, m] { setMode(m); });
    if (AppMode::menuSeparatorAfter(m)) {
      modes->addSeparator();
    }
  }

  QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->addAction(help);
  helpMenu->addAction(tr("About v-cube"), this, &MainWindow::showAbout);

  // The shortcuts must keep working while focus mode hides the menu bar.
  for (QAction *menu : menuBar()->actions()) {
    addActions(menu->menu()->actions());
  }

  // Library's own menus and Undo, shown only in Library mode; their keys
  // stay with the page (they would act on it from other modes).
  QAction *first = edit->actions().value(0);
  edit->insertActions(first, libraryPage_->editActions());
  // The other modes take Ctrl+Z themselves (see KeyRouter): the menu item
  // sends them the key, so that undo can be found in the menu everywhere.
  pageUndoAction_ = new QAction(tr("&Undo") + "\tCtrl+Z", this);
  connect(pageUndoAction_, &QAction::triggered, this, [this] {
    if (ModePage *page = currentPage()) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
      page->handleKey(&press, true);
    }
  });
  edit->insertAction(first, pageUndoAction_);
  edit->insertSeparator(first);
  for (QMenu *menu : libraryPage_->menus()) {
    menuBar()->insertMenu(view->menuAction(), menu);
  }
  // The current mode's own menu, filled each time it opens.
  modeMenu_ = new QMenu(this);
  menuBar()->insertMenu(view->menuAction(), modeMenu_);
  modeMenu_->menuAction()->setVisible(false);
  connect(modeMenu_, &QMenu::aboutToShow, this, [this] {
    modeMenu_->clear();
    if (ModePage *page = currentPage(); page && !page->isBusy()) {
      page->fillModeMenu(*modeMenu_);
    }
  });
}

void MainWindow::openCommandPalette() {
  if (currentPage() && currentPage()->isBusy()) {
    return;
  }
  std::vector<CommandPalette::Command> commands =
      CommandPalette::fromMenus(menuBar());
  for (auto &c : CommandPalette::fromControls(currentPage())) {
    commands.push_back(std::move(c));
  }
  CommandPalette palette(std::move(commands), this);
  palette.exec();
}

void MainWindow::openDataFolder() {
  if (openFolder(data_.dataDir())) {
    return;
  }
  // No file manager could be started: show the paths so they can be opened
  // by hand.
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Open data folder"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *intro = new QLabel(
      tr("Could not open a file manager. Your files are here:"), &dialog);
  layout->addWidget(intro);
  auto *grid = new QGridLayout;
  const std::pair<QString, QString> rows[] = {
      {tr("Data folder"), QDir::toNativeSeparators(data_.dataDir())},
      {tr("Config file"), QDir::toNativeSeparators(saved::settingsFile())},
  };
  int row = 0;
  for (const auto &[label, path] : rows) {
    auto *edit = new QLineEdit(path, &dialog);
    edit->setReadOnly(true);
    edit->setMinimumWidth(edit->fontMetrics().horizontalAdvance(path) + 24);
    auto *copy = new QPushButton(tr("Copy"), &dialog);
    copy->setAutoDefault(false);
    connect(copy, &QPushButton::clicked, &dialog, [this, path, label] {
      QGuiApplication::clipboard()->setText(path);
      showMessage(tr("%1 path copied").arg(label));
    });
    grid->addWidget(new QLabel(label, &dialog), row, 0);
    grid->addWidget(edit, row, 1);
    grid->addWidget(copy, row, 2);
    ++row;
  }
  layout->addLayout(grid);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}

void MainWindow::exportAllData() {
  QString parent = QFileDialog::getExistingDirectory(
      this, tr("Export all data into a new folder in…"),
      saved::exportFolder().get());
  if (parent.isEmpty()) {
    return;
  }
  saved::exportFolder().set(parent);
  QString base = QString("v-cube-export-%1")
                     .arg(QDate::currentDate().toString(Qt::ISODate));
  QString target = QDir(parent).filePath(base);
  for (int n = 2; QFileInfo::exists(target); ++n) {
    target = QDir(parent).filePath(QString("%1-%2").arg(base).arg(n));
  }
  QString error;
  if (!vcube::exportAllData(data_.dataDir(), saved::settingsFile(), target,
                            &error)) {
    QMessageBox::warning(this, tr("Export all data"),
                         tr("The export failed: %1").arg(error));
    return;
  }
  QMessageBox::information(
      this, tr("Export all data"),
      tr("Your alg sets, progress, sessions and settings were copied to "
         "%1.\n\nTo move them to another computer, copy the data folder's "
         "contents into that computer's data folder (File > Open data "
         "folder) while v-cube is closed. Dated backups are also kept in "
         "the data folder's backups folder, and deleted sets in "
         "algsets/deleted.")
          .arg(QDir::toNativeSeparators(target)));
}

void MainWindow::showMessage(const QString &text) {
  statusBar()->showMessage(text, 8000);
  if (focusMode_) {
    showOverlay(text, 5000); // the status bar is hidden
  }
}

void MainWindow::openSettings(bool neutrality) {
  if (currentPage() && currentPage()->isBusy()) {
    return;
  }
  SettingsDialog dialog(settings_, this);
  if (neutrality) {
    dialog.showNeutralityTab();
  }
  auto apply = [this](const AppSettings &now) {
    SettingsParts changed = changedParts(settings_, now);
    settings_ = now;
    settings_.save();
    settingsChanged(changed);
  };
  connect(&dialog, &SettingsDialog::applied, this, apply);
  if (dialog.exec() == QDialog::Accepted) {
    apply(dialog.result());
  }
}

void MainWindow::settingsChanged(SettingsParts parts) {
  if (parts.testFlag(SettingsPart::Theme)) {
    applyTheme(*qApp, settings_.darkTheme);
  }
  applyWindowSettings();
  for (ModePage *page : pages_) {
    // A new theme changes the colours of every page.
    SettingsParts used = page->settingsUsed() | SettingsPart::Theme;
    if (used & parts) {
      page->applySettings();
    }
  }
  updateBadges();
}

void MainWindow::applyWindowSettings() {
  // First, as the other sizes are worked out from it.
  setInterfaceTextPercent(settings_.interfaceTextPercent);
  styleSidebar();
  setDisplayFontSizes(settings_.scrambleFontPt, settings_.timerFontPt);
  // The hints under the timer and cube (muted, wrapped labels) and the
  // status bar, which are otherwise small in a large window.
  double scale = settings_.hintTextPercent / 100.0;
  QFont base = QApplication::font();
  QFont hint = base;
  hint.setPointSizeF(base.pointSizeF() * scale);
  for (ModePage *page : pages_) {
    for (QLabel *label : page->findChildren<QLabel *>()) {
      // Labels given a font of their own keep it.
      bool isHint = label->property("vcubeHint").toBool() ||
                    (label->wordWrap() &&
                     label->foregroundRole() == QPalette::PlaceholderText &&
                     !label->testAttribute(Qt::WA_SetFont));
      if (isHint) {
        label->setProperty("vcubeHint", true);
        label->setFont(hint);
      }
    }
  }
  statusBar()->setFont(hint);
  if (settings_.inspectionSounds) {
    prepareSounds();
    setInspectionCallHandler([](int seconds) { playInspectionCall(seconds); });
  } else {
    setInspectionCallHandler({});
  }
  updateHelp();
}

void MainWindow::importAlgSet() {
  if (setMode(AppMode::Library)) {
    libraryPage_->importSetFile();
  }
}

void MainWindow::exportAlgSet() {
  if (setMode(AppMode::Library)) {
    libraryPage_->exportSetFile();
  }
}

void MainWindow::showAbout() {
  QMessageBox box(this);
  box.setWindowTitle(tr("About v-cube"));
  box.setIconPixmap(windowIcon().pixmap(64, 64));
  box.setTextFormat(Qt::RichText);
  box.setText(
      tr("<h3>v-cube %1</h3><p>Create, learn and drill 3x3 alg sets; "
         "scramble and time solves with a real or a virtual cube.</p>"
         "<p>Random-state scrambles use Kociemba's two-phase algorithm. "
         "Inspired by cstimer and Alg-Trainer.</p>"
         "<p>Data folder: <code>%2</code><br>Qt %3, platform "
         "<code>%4</code></p>")
          .arg(QApplication::applicationVersion().toHtmlEscaped(),
               QDir::toNativeSeparators(data_.dataDir()).toHtmlEscaped(),
               qVersion(), QGuiApplication::platformName()));
  box.exec();
}

} // namespace vcube
