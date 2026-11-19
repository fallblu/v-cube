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

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
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
#include <QHeaderView>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTabBar>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace vcube {

namespace {

// The tip of a widget at `pos` (in its coordinates): an item's or a
// header section's in a view, a tab's, a menu action's, else the widget's
// own or the nearest parent's.
QString tipAt(QWidget *w, QPoint pos) {
  if (auto *view = qobject_cast<QAbstractItemView *>(w->parentWidget());
      view && view->viewport() == w) {
    if (auto *header = qobject_cast<QHeaderView *>(view)) {
      int section = header->logicalIndexAt(pos);
      return section < 0 || !header->model()
                 ? QString()
                 : header->model()
                       ->headerData(section, header->orientation(),
                                    Qt::ToolTipRole)
                       .toString();
    }
    QString tip = view->indexAt(pos).data(Qt::ToolTipRole).toString();
    return tip.isEmpty() ? view->toolTip() : tip;
  }
  if (auto *tabs = qobject_cast<QTabBar *>(w)) {
    int tab = tabs->tabAt(pos);
    return tab < 0 ? tabs->toolTip() : tabs->tabToolTip(tab);
  }
  if (auto *menu = qobject_cast<QMenu *>(w)) {
    QAction *action = menu->actionAt(pos);
    // An action's tip is its text unless one is set.
    if (!menu->toolTipsVisible() || !action ||
        action->toolTip() == QString(action->text()).remove('&')) {
      return {};
    }
    return action->toolTip();
  }
  for (QWidget *at = w; at; at = at->parentWidget()) {
    if (!at->toolTip().isEmpty()) {
      return at->toolTip();
    }
    if (at->isWindow()) {
      break;
    }
  }
  return {};
}

// Tooltips never pop up beside the mouse, where they would cover the cube
// and the lists: the tip of what the mouse is over is written in the status
// bar instead, as soon as the mouse comes onto it.
class TipsInStatusBar : public QObject {
public:
  explicit TipsInStatusBar(QMainWindow *window)
      : QObject(window), window_(window) {}

protected:
  bool eventFilter(QObject *o, QEvent *e) override {
    auto *w = qobject_cast<QWidget *>(o);
    switch (e->type()) {
    case QEvent::ToolTip:
      // The windows in front (dialogs) have no status bar: their tips pop
      // up as tooltips do.
      if (!w || !inMainWindow(w)) {
        return false;
      }
      show(w, tipAt(w, static_cast<QHelpEvent *>(e)->pos()));
      return true; // never as a tooltip
    case QEvent::Enter:
      if (w && !w->toolTip().isEmpty()) {
        show(w, w->toolTip());
      }
      break;
    case QEvent::MouseMove:
      // Over a list, a header, a tab bar or a menu, the tip changes with
      // the item under the mouse.
      if (w && (qobject_cast<QAbstractItemView *>(w->parentWidget()) ||
                qobject_cast<QTabBar *>(w) || qobject_cast<QMenu *>(w))) {
        show(w, tipAt(w, static_cast<QMouseEvent *>(e)->position().toPoint()));
      }
      break;
    case QEvent::Leave:
    case QEvent::Hide:
      if (w && w == shownFor_) {
        clear();
      }
      break;
    default:
      break;
    }
    return false;
  }

private:
  QMainWindow *window_;
  QPointer<QWidget> shownFor_;
  QString shown_;

  // Whether w is in the main window or in a popup (a menu) of it.
  bool inMainWindow(QWidget *w) const {
    QWidget *top = w->window();
    return top == window_ || (top->windowType() == Qt::Popup &&
                              QApplication::activeWindow() == window_);
  }

  void show(QWidget *w, QString tip) {
    if (!inMainWindow(w)) {
      return;
    }
    if (Qt::mightBeRichText(tip)) {
      tip = QTextDocumentFragment::fromHtml(tip).toPlainText();
    }
    tip = tip.simplified();
    if (tip.isEmpty()) {
      if (w == shownFor_) {
        clear();
      }
      return;
    }
    QStatusBar *bar = window_->statusBar();
    // A message of the page's own stays until it times out.
    if (!bar->currentMessage().isEmpty() && bar->currentMessage() != shown_) {
      return;
    }
    shown_ = tip;
    shownFor_ = w;
    bar->showMessage(tip);
  }

  void clear() {
    QStatusBar *bar = window_->statusBar();
    if (!shown_.isEmpty() && bar->currentMessage() == shown_) {
      bar->clearMessage();
    }
    shown_.clear();
    shownFor_ = nullptr;
  }
};

// Names every button that shows only an icon, a colour or "…" by what its
// tooltip says, as it is first shown, so that assistive technology can
// say what it is (the tooltip itself never pops up, see TipsInStatusBar).
class AccessibleNames : public QObject {
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject *obj, QEvent *e) override {
    if (e->type() == QEvent::Polish) {
      if (auto *b = qobject_cast<QAbstractButton *>(obj);
          b && b->accessibleName().isEmpty() && !b->toolTip().isEmpty()) {
        QString text = b->text().remove('&').trimmed();
        static const QStringList noWords = {"…", "⋯", "...", "▾", "▸"};
        if (text.isEmpty() || noWords.contains(text)) {
          b->setAccessibleName(b->toolTip());
        }
      }
    }
    return false;
  }
};

// A mode's button in the sidebar: its name on the left and the reviews
// due, if any, on the right, so that the names form one edge to scan.
class SidebarButton : public QToolButton {
public:
  using QToolButton::QToolButton;

  void setBadge(const QString &badge) {
    badge_ = badge;
    updateGeometry();
    update();
  }

  QSize sizeHint() const override {
    QSize s = QToolButton::sizeHint();
    if (!badge_.isEmpty()) {
      s.rwidth() += fontMetrics().horizontalAdvance(badge_) + kGap;
    }
    return s;
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QStylePainter p(this);
    QStyleOptionToolButton opt;
    initStyleOption(&opt);
    opt.text.clear(); // the background and frame only
    p.drawComplexControl(QStyle::CC_ToolButton, opt);
    // As the style sheet's padding: 14 px on the left, 18 on the right.
    QRect r = rect().adjusted(14, 0, -18, 0);
    p.setPen(palette().color(isChecked() ? QPalette::HighlightedText
                                         : QPalette::Text));
    if (!badge_.isEmpty()) {
      QFont bold = font();
      bold.setBold(true);
      p.setFont(bold);
      p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, badge_);
      r.setRight(r.right() - QFontMetrics(bold).horizontalAdvance(badge_) -
                 kGap);
      p.setFont(font());
    }
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
               text());
  }

private:
  static constexpr int kGap = 12;
  QString badge_;
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
  todayPage_ = new TodayPage(
      settings_, data_.library, data_.sessions, data_.untimed, data_.moveCounts,
      data_.lab, eoCrossPage_->data(), f2lPage_->data(), tslePage_->data(),
      [f2l = f2lPage_] { return f2l->cardPool(); },
      [tsle = tslePage_] { return tsle->cardPool(); }, stack_);
  pages_ = {learnPage_,   drillPage_,     libraryPage_, timedPage_,
            untimedPage_, moveCountPage_, eoCrossPage_, algLabPage_,
            f2lPage_,     tslePage_,      todayPage_};
  for (ModePage *page : pages_) {
    stack_->addWidget(page);
    connect(page, &ModePage::message, this, &MainWindow::showMessage);
  }
  // Alg Lab says so itself only when it saves a set without Library (it
  // has no alg editor): with one, Library makes the change and says so.
  connect(libraryPage_, &LibraryPage::setsChanged, this,
          &MainWindow::librarySetsChanged);
  connect(algLabPage_, &AlgLabPage::setsChanged, this,
          &MainWindow::librarySetsChanged);
  libraryPage_->setLab(&data_.lab);
  connect(
      libraryPage_, &LibraryPage::practiceRequested, this,
      [this](AppMode::Id mode, const QString &setId, const QString &caseId) {
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
  connect(todayPage_, &TodayPage::learnSetRequested, this,
          &MainWindow::learnSet);
  connect(todayPage_, &TodayPage::practiceRequested, this, [this](int index) {
    // Today's rows carry the mode as a number.
    AppMode::Id mode = AppMode::fromIndex(index).value_or(AppMode::Tsle);
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
    connect(page, &CasePage::pasteAlgsRequested, this,
            [this](const QString &setId) {
              if (setMode(AppMode::Library)) {
                libraryPage_->pasteAlgsInto(setId);
              }
            });
    connect(page, &CasePage::learnSetRequested, this, &MainWindow::learnSet);
  }
  connect(learnPage_, &LearnPage::drillSetRequested, this,
          [this](const QString &setId, bool weakest) {
            if (setMode(AppMode::Drill)) {
              drillPage_->drillSet(setId, weakest);
            }
          });

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
  for (AppMode::Id m : AppMode::all()) {
    ModePage *page = pages_[static_cast<std::size_t>(m)];
    QString id = AppMode::settingsId(m);
    page->setSideHidden(saved::sideHidden(id).get());
    connect(page, &ModePage::sideHiddenToggled, this,
            [id](bool hidden) { saved::sideHidden(id).set(hidden); });
    connect(page, &ModePage::sideShownChanged, this, [this, page](bool shown) {
      if (page == currentPage() && sidePanelAction_) {
        sidePanelAction_->setChecked(shown);
      }
    });
  }

  badgeTimer_ = new QTimer(this);
  badgeTimer_->setInterval(60 * 1000);
  connect(badgeTimer_, &QTimer::timeout, this, &MainWindow::updateBadges);
  // A copy left open for days still makes each day's backup.
  connect(badgeTimer_, &QTimer::timeout, this, [this] {
    if (std::optional<QString> error = data_.backUpIfNewDay()) {
      writeReported(data_.backupsDir(), *error);
    }
  });
  badgeTimer_->start();

  help_ = new HelpPanel(this);
  addDockWidget(Qt::RightDockWidgetArea, help_);
  resizeDocks({help_}, {340}, Qt::Horizontal);
  help_->hide();
  buildMenus();
  qApp->installEventFilter(new TipsInStatusBar(this));
  qApp->installEventFilter(new AccessibleNames(this));
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
  std::optional<AppMode::Id> mode =
      AppMode::fromSettingsId(saved::windowMode().get());
  if (!mode) {
    mode = AppMode::fromSavedTab(saved::legacyTab().get());
  }
  saved::removeObsolete();
  applyWindowSettings();
  setMode(mode.value_or(AppMode::Learn));
  updateBadges();
  updateBanner();
  if (settings_.droppedKeyBindings > 0) {
    showMessage(plural(settings_.droppedKeyBindings,
                       tr("%n key binding in the settings file couldn't be "
                          "read and was ignored"),
                       tr("%n key bindings in the settings file couldn't be "
                          "read and were ignored")));
  }
}

MainWindow::~MainWindow() {
  setWriteReporter({});
  // The pages refer to data_, which is destroyed before the widgets the
  // window owns: they go first. A window still shown is hidden before that,
  // while everything is whole, as hiding it tells the pages that the focus
  // has gone.
  hide();
  for (ModePage *&page : pages_) {
    delete std::exchange(page, nullptr);
  }
}

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
    setTextRole(label, TextRole::Body, true);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setContentsMargins(6, 10, 0, 4);
    layout->addWidget(label);
  };
  for (AppMode::Id m : AppMode::sidebarOrder()) {
    if (QString h = AppMode::sidebarHeader(m); !h.isEmpty()) {
      header(h);
    }
    auto *b = new SidebarButton(bar);
    b->setText(AppMode::label(m));
    b->setAccessibleName(AppMode::label(m));
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
  connect(modeButtons_, &QButtonGroup::idClicked, this, [this](int id) {
    if (auto mode = AppMode::fromIndex(id)) {
      setMode(*mode);
    }
  });
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
  banner_->setStyleSheet(QString("#saveBanner { background: %1; }"
                                 "#saveBanner QLabel { color: %2; }")
                             .arg(roleColour(Role::Banner).name(),
                                  roleColour(Role::OnBadge).name()));
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
  return mode_ ? pages_[static_cast<std::size_t>(*mode_)] : nullptr;
}

bool MainWindow::setMode(AppMode::Id mode) {
  ModePage *old = currentPage();
  if (mode == mode_) {
    return true;
  }
  if (old && old->isBusy()) {
    // Keep the sidebar on the running mode.
    modeButtons_->button(*mode_)->setChecked(true);
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
  bool switching = mode_.has_value();
  // What the last page said is about that page, not this one.
  statusBar()->clearMessage();
  mode_ = mode;
  modeButtons_->button(mode)->setChecked(true);
  // The page gets the width of the one before: in a narrow window its side
  // panel is hidden before it can widen the window.
  if (old) {
    currentPage()->setNarrow(stack_->width() < ModePage::kNarrowWidth);
  }
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
    sidePanelAction_->setChecked(currentPage()->sideShown());
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

void MainWindow::librarySetsChanged(const QString &setId) {
  learnPage_->reloadSets();
  drillPage_->reloadSets();
  timedPage_->refreshScrambleTypes(setId);
  untimedPage_->refreshScrambleTypes(setId);
  algLabPage_->reloadSets();
  f2lPage_->reloadSets();
  updateBadges(); // Today reads the sets as it is shown
}

void MainWindow::updateBadges() {
  TodayPage::Totals t = todayPage_->totals();
  for (AppMode::Id m : {AppMode::Today, AppMode::Learn}) {
    auto *b = static_cast<SidebarButton *>(modeButtons_->button(m));
    b->setBadge(t.due > 0 ? QString::number(t.due) : QString());
    b->setAccessibleName(t.due > 0 ? plural(t.due, tr("%1, %n review due"),
                                            tr("%1, %n reviews due"))
                                         .arg(AppMode::label(m))
                                   : AppMode::label(m));
    b->setToolTip(
        plural(t.due, tr("%1 (%2): %n review due"),
               tr("%1 (%2): %n reviews due"))
            .arg(AppMode::label(m),
                 AppMode::shortcut(m).toString(QKeySequence::NativeText)) +
        (t.newLeft > 0 ? plural(t.newLeft, tr(", %n new case left today"),
                                tr(", %n new cases left today"))
                       : QString()));
  }
}

void MainWindow::learnSet(const QString &setId) {
  if (setMode(AppMode::Learn)) {
    learnPage_->pickSet(setId);
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
  for (ModePage *page : pages_) {
    if (page) {
      page->aboutToClose();
    }
  }
  finishBackgroundWrites();
  // The last writes report from the writer's thread through the queue:
  // take those in now, or a save that failed just before quitting would
  // never be seen.
  QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
  if (!failedWrites_.empty()) {
    QStringList lines;
    for (const auto &[path, error] : failedWrites_) {
      lines << QString("%1: %2").arg(QFileInfo(path).fileName(), error);
    }
    auto answer = QMessageBox::warning(
        this, tr("Quit v-cube"),
        tr("Some changes could not be saved:") + "\n\n" + lines.join("\n") +
            "\n\n" + tr("Quit anyway? They will be lost."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      e->ignore();
      return;
    }
  }
  QMainWindow::closeEvent(e);
}

void MainWindow::restoreLayout() {
  for (AppMode::Id m : AppMode::all()) {
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
  for (AppMode::Id m : AppMode::all()) {
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
      page->setSideShown(on);
      sidePanelAction_->setChecked(page->sideShown());
    }
  });
  QAction *help = help_->toggleViewAction();
  help->setText(tr("&Keyboard controls"));
  help->setShortcut(QKeySequence(Qt::Key_F1));
  view->addAction(help);

  QMenu *modes = menuBar()->addMenu(tr("&Go"));
  for (AppMode::Id m : AppMode::sidebarOrder()) {
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
  // The mode's menu is filled as it opens: fill it now, so that what it
  // offers can be found too.
  if (modeMenu_ && currentPage()->hasModeMenu()) {
    modeMenu_->clear();
    currentPage()->fillModeMenu(*modeMenu_);
  }
  // Under three headings: the modes, what the page offers (its own menus
  // and controls), and the other menus.
  const QString go = tr("Go"), page = tr("This page"), menus = tr("Menus");
  QStringList pageMenus;
  if (modeMenu_ && currentPage()->hasModeMenu()) {
    pageMenus << QString(modeMenu_->title()).remove('&');
  }
  if (mode_ == AppMode::Library) {
    for (QMenu *menu : libraryPage_->menus()) {
      pageMenus << QString(menu->title()).remove('&');
    }
  }
  std::vector<CommandPalette::Command> commands;
  std::vector<CommandPalette::Command> others;
  for (auto &c : CommandPalette::fromMenus(menuBar())) {
    if (c.group == QString(tr("&Go")).remove('&')) {
      c.group = go;
      commands.push_back(std::move(c));
    } else {
      c.group = pageMenus.contains(c.group) ? page : menus;
      others.push_back(std::move(c));
    }
  }
  for (auto &c : CommandPalette::fromControls(currentPage())) {
    c.group = page;
    commands.push_back(std::move(c));
  }
  std::stable_partition(others.begin(), others.end(),
                        [&](const auto &c) { return c.group == page; });
  for (auto &c : others) {
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
