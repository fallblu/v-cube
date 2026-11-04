#include "app/virtual_page.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace vcube {

namespace {
QPushButton *button(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}
} // namespace

VirtualPage::VirtualPage(AppSettings &settings, AlgLibrary &library,
                         QWidget *parent)
    : ModePage(parent), settings_(settings), feed_(library) {
  auto *layout = new QVBoxLayout(this);
  cubeView_ = new CubeView(this);
  auto *top = new QHBoxLayout;
  typeBox_ = new ScrambleTypeBox(library, this);
  auto *scrambleButton = button(tr("Scramble"), this);
  scrambleButton->setToolTip(tr("Scramble the cube (Space)"));
  auto *resetButton = button(tr("Reset"), this);
  resetButton->setToolTip(tr("Return the cube to solved (Esc)"));
  top->addWidget(new QLabel(tr("View:"), this));
  top->addWidget(createViewSelector(cubeView_, "virtual", this));
  top->addSpacing(16);
  top->addWidget(new QLabel(tr("Scramble:"), this));
  top->addWidget(typeBox_, 1);
  top->addWidget(scrambleButton);
  top->addWidget(resetButton);
  layout->addLayout(top);

  scrambleLabel_ = new QLabel(this);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(15);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(44);
  layout->addWidget(scrambleLabel_);
  layout->addWidget(cubeView_, 1);
  statusLabel_ = new QLabel(this);
  statusLabel_->setAlignment(Qt::AlignCenter);
  statusLabel_->setWordWrap(true);
  layout->addWidget(statusLabel_);

  auto *algRow = new QHBoxLayout;
  algEdit_ = new QLineEdit(this);
  algEdit_->setFocusPolicy(Qt::ClickFocus);
  algEdit_->setPlaceholderText(
      tr("Type an alg and press Enter to apply it, e.g. R U R' U'"));
  algEdit_->installEventFilter(this);
  auto *applyButton = button(tr("Apply"), this);
  algRow->addWidget(new QLabel(tr("Alg:"), this));
  algRow->addWidget(algEdit_, 1);
  algRow->addWidget(applyButton);
  layout->addLayout(algRow);
  algError_ = new QLabel(this);
  algError_->setStyleSheet("color: #e66");
  algError_->setVisible(false);
  layout->addWidget(algError_);

  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &VirtualPage::feedChanged);
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            QSettings().setValue("virtual/scrambleType", type);
            feed_.setType(type);
          });
  connect(scrambleButton, &QPushButton::clicked, this, &VirtualPage::scramble);
  connect(resetButton, &QPushButton::clicked, this, &VirtualPage::reset);
  connect(algEdit_, &QLineEdit::returnPressed, this, &VirtualPage::applyAlg);
  connect(applyButton, &QPushButton::clicked, this, &VirtualPage::applyAlg);
  connect(algEdit_, &QLineEdit::textChanged, this,
          [this] { algError_->setVisible(false); });

  applySettings();
  typeBox_->setCurrentType(
      QSettings().value("virtual/scrambleType", "333").toString());
  feed_.setType(typeBox_->currentType()); // if the type didn't change
  reset();
}

void VirtualPage::applySettings() {
  CubeLook look = cubeView_->look();
  look.scheme = settings_.scheme;
  cubeView_->setLook(look);
  cubeView_->setAnimationMs(settings_.animationMs);
}

void VirtualPage::deactivated() {
  cubeView_->finishAnimations();
  returnFocus();
}

void VirtualPage::refreshScrambleTypes() {
  QString before = typeBox_->currentType();
  typeBox_->refresh();
  if (typeBox_->currentType() == before && before.startsWith("set:")) {
    feed_.regenerate();
  }
}

void VirtualPage::returnFocus() {
  if (algEdit_->hasFocus()) {
    algEdit_->clearFocus();
  }
}

bool VirtualPage::eventFilter(QObject *obj, QEvent *event) {
  if (obj == algEdit_ && event->type() == QEvent::KeyPress &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    returnFocus();
    return true;
  }
  return ModePage::eventFilter(obj, event);
}

void VirtualPage::reset() {
  wantScramble_ = false;
  scrambled_ = false;
  cube_ = FaceletCube();
  cubeView_->setState(cube_);
  scrambleLabel_->clear();
  statusLabel_->setText(
      tr("Turn the cube with the keyboard. Space scrambles, Esc resets. "
         "Click the alg field to type an alg."));
}

void VirtualPage::scramble() {
  wantScramble_ = true;
  feedChanged();
}

void VirtualPage::feedChanged() {
  if (!wantScramble_) {
    return;
  }
  const auto &current = feed_.current();
  if (!current) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    return;
  }
  wantScramble_ = false;
  QString text = current->text;
  cube_ = FaceletCube();
  cube_.apply(parseAlgOrEmpty(ss(text)));
  cubeView_->setState(cube_);
  scrambleLabel_->setText(text);
  scrambled_ = !cube_.isSolved();
  statusLabel_->setText(tr("Scrambled. Esc resets the cube."));
  feed_.advance(); // prepare the next one
}

void VirtualPage::applyAlg() {
  QString text = algEdit_->text().trimmed();
  if (text.isEmpty()) {
    returnFocus();
    return;
  }
  ParseResult parsed = parseAlg(ss(text));
  if (!parsed) {
    algError_->setText(tr("Can't read this alg at character %1: %2")
                           .arg(parsed.error->position + 1)
                           .arg(qs(parsed.error->message)));
    algError_->setVisible(true);
    return; // keep the field focused to fix it
  }
  for (Move m : parsed.alg) {
    turn(m);
  }
  algError_->setVisible(false);
  algEdit_->clear();
  returnFocus();
}

void VirtualPage::turn(Move m) {
  cube_.apply(m);
  cubeView_->animateMove(m);
  if (scrambled_ && cube_.isSolved()) {
    scrambled_ = false;
    statusLabel_->setText(tr("Solved!"));
  }
}

bool VirtualPage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  if (e->isAutoRepeat()) {
    return key == Qt::Key_Space;
  }
  if (!press) {
    return key == Qt::Key_Space;
  }
  if (key == Qt::Key_Space) {
    scramble();
    return true;
  }
  if (key == Qt::Key_Escape) {
    reset();
    return true;
  }
  if (auto m = settings_.keymap.lookup(e)) {
    turn(*m);
    return true;
  }
  return false;
}

} // namespace vcube
