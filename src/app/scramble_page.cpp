#include "app/scramble_page.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
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

ScramblePage::ScramblePage(AppSettings &settings, AlgLibrary &library,
                           QWidget *parent)
    : ModePage(parent), settings_(settings), feed_(library) {
  auto *layout = new QVBoxLayout(this);
  typeRow_ = new QWidget(this);
  auto *typeRow = new QHBoxLayout(typeRow_);
  typeRow->setContentsMargins(0, 0, 0, 0);
  typeBox_ = new ScrambleTypeBox(library, typeRow_);
  algs_ = new ScrambleAlgs(library, "scramble/showAlgs", this);
  typeRow->addWidget(new QLabel(tr("Scramble:"), typeRow_));
  typeRow->addWidget(typeBox_, 1);
  typeRow->addWidget(algs_->button());
  layout->addWidget(typeRow_);

  layout->addStretch(1);
  scrambleLabel_ = new QLabel(this);
  scrambleLabel_->setWordWrap(true);
  scrambleLabel_->setAlignment(Qt::AlignCenter);
  scrambleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont sf = scrambleLabel_->font();
  sf.setPointSize(26);
  scrambleLabel_->setFont(sf);
  scrambleLabel_->setMinimumHeight(100);
  layout->addWidget(scrambleLabel_);
  net_ = new NetPreview(this);
  net_->setFixedSize(240, 180);
  layout->addWidget(net_, 0, Qt::AlignHCenter);

  auto *navRow = new QHBoxLayout;
  auto *backButton = button(tr("← Previous"), this);
  auto *copyButton = button(tr("Copy"), this);
  auto *nextButton = button(tr("Next →"), this);
  positionLabel_ = new QLabel(this);
  positionLabel_->setAlignment(Qt::AlignCenter);
  positionLabel_->setMinimumWidth(80);
  navRow->addStretch();
  navRow->addWidget(backButton);
  navRow->addWidget(positionLabel_);
  navRow->addWidget(nextButton);
  navRow->addSpacing(16);
  navRow->addWidget(copyButton);
  navRow->addStretch();
  layout->addLayout(navRow);
  auto *help = help_ =
      new QLabel(tr("Space or → shows the next scramble, Backspace "
                    "or ← goes back. Nothing is timed or saved."),
                 this);
  help->setAlignment(Qt::AlignCenter);
  help->setForegroundRole(QPalette::PlaceholderText);
  layout->addWidget(help);
  layout->addWidget(algs_->label());
  layout->addStretch(2);

  connect(&feed_, &ScrambleFeed::currentChanged, this,
          &ScramblePage::feedChanged);
  connect(typeBox_, &ScrambleTypeBox::typeChanged, this,
          [this](const QString &type) {
            QSettings().setValue("scramble/type", type);
            algs_->setType(type);
            wantNext_ = true; // show one of the new type straight away
            feed_.setType(type);
            show();
          });
  connect(backButton, &QPushButton::clicked, this, &ScramblePage::previous);
  connect(nextButton, &QPushButton::clicked, this, &ScramblePage::next);
  connect(copyButton, &QPushButton::clicked, this, [this] {
    if (index_ < history_.size()) {
      QApplication::clipboard()->setText(history_[index_].text);
      emit message(tr("Scramble copied"));
    }
  });

  applySettings();
  wantNext_ = true;
  typeBox_->setCurrentType(
      QSettings().value("scramble/type", "333").toString());
  algs_->setType(typeBox_->currentType());
  feed_.setType(typeBox_->currentType()); // if the type didn't change
}

void ScramblePage::applySettings() { net_->setScheme(settings_.scheme); }

void ScramblePage::setFocusMode(bool on) {
  typeRow_->setVisible(!on);
  help_->setVisible(!on);
}

void ScramblePage::refreshScrambleTypes() {
  QString before = typeBox_->currentType();
  typeBox_->refresh();
  if (typeBox_->currentType() == before && before.startsWith("set:")) {
    feed_.regenerate();
  }
  algs_->refresh();
}

void ScramblePage::feedChanged() {
  if (wantNext_ && feed_.current()) {
    wantNext_ = false;
    history_.push_back(*feed_.current());
    if (history_.size() > kHistory) {
      history_.pop_front();
    }
    index_ = history_.size() - 1;
    feed_.advance(); // the one after is prepared in the background
  }
  show();
}

void ScramblePage::next() {
  if (index_ + 1 < history_.size()) {
    ++index_;
    show();
    return;
  }
  wantNext_ = true;
  feedChanged();
}

void ScramblePage::previous() {
  if (index_ > 0 && index_ < history_.size()) {
    --index_;
    show();
  }
}

void ScramblePage::show() {
  bool showing = index_ < history_.size() && !wantNext_;
  if (!showing) {
    scrambleLabel_->setText(tr("Generating scramble…"));
    net_->setCube(std::nullopt);
    algs_->setScramble(nullptr);
  } else {
    scrambleLabel_->setText(history_[index_].text);
    net_->setScramble(history_[index_].text);
    algs_->setScramble(&history_[index_]);
  }
  positionLabel_->setText(
      history_.empty()
          ? QString()
          : QString("%1 / %2").arg(index_ + 1).arg(history_.size()));
}

bool ScramblePage::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  if (key == Qt::Key_Tab && press && algs_->toggle()) {
    return true;
  }
  bool forward = key == Qt::Key_Space || key == Qt::Key_Right;
  bool back = key == Qt::Key_Backspace || key == Qt::Key_Left;
  if (!forward && !back) {
    return false;
  }
  if (press) {
    if (forward) {
      next();
    } else {
      previous();
    }
  }
  return true;
}

} // namespace vcube
