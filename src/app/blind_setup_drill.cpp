#include "app/blind_setup_drill.hpp"

#include "app/session_panel.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

#include <algorithm>

namespace vcube {

SetupDrill::SetupDrill(BlindContext &context, QWidget *parent)
    : BlindDrill(context, parent) {
  left_ = new BlindStatsPanel(true, parent);
  piecesBox_ = blindPiecesBox(parent);
  modeBox_ = blindComboBox({tr("Do it on the cube"), tr("Type it")}, parent);
  modeBox_->setToolTip(
      tr("Do each setup on the virtual cube, or type it to be checked"));
  piecesItem_ = labelledBox(tr("Pieces:"), piecesBox_, parent);
  modeItem_ = labelledBox(tr("Setups:"), modeBox_, parent);

  row_ = new QWidget(parent);
  auto *layout = new QHBoxLayout(row_);
  layout->setContentsMargins(0, 0, 0, 0);
  edit_ = new QLineEdit(row_);
  edit_->setPlaceholderText(tr("Type the setup and press Enter"));
  edit_->setFocusPolicy(Qt::ClickFocus);
  edit_->installEventFilter(this);
  layout->addWidget(new QLabel(tr("Setup:"), row_));
  layout->addWidget(edit_, 1);

  for (QComboBox *box : {piecesBox_, modeBox_}) {
    connect(box, &QComboBox::activated, this, [this] {
      QSettings q;
      q.setValue("blind/setupPieces", piecesBox_->currentIndex());
      q.setValue("blind/setupMode", modeBox_->currentIndex());
      emit restartRequested();
    });
  }
  connect(left_->resetButton(), &QPushButton::clicked, this, [this] {
    auto answer = QMessageBox::question(
        ctx_.page, tr("Reset statistics"),
        tr("Forget the setup move statistics of every letter?"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    ctx_.store.setups() = LetterStats{};
    ctx_.save();
    refreshLeft();
  });
}

QWidget *SetupDrill::leftPanel() const { return left_; }

std::vector<QWidget *> SetupDrill::options() const {
  return {piecesItem_, modeItem_};
}

QWidget *SetupDrill::inputRow() const { return row_; }

BlindPieces SetupDrill::pieces() const {
  return static_cast<BlindPieces>(std::clamp(piecesBox_->currentIndex(), 0, 2));
}

bool SetupDrill::onCube() const { return modeBox_->currentIndex() == 0; }

void SetupDrill::enter(bool changed) {
  (void)changed; // the options are read again either way
  flow_.reset();
  edit_->clear();
  edit_->clearFocus();
  QSettings q;
  piecesBox_->setCurrentIndex(q.value("blind/setupPieces", 0).toInt());
  modeBox_->setCurrentIndex(q.value("blind/setupMode", 0).toInt());
  flow_.setTyped(!onCube());
  row_->setVisible(!onCube());
  nextItem();
}

void SetupDrill::leave() {
  flow_.reset();
  edit_->clear();
  edit_->clearFocus();
}

void SetupDrill::activated() {
  if (!flow_.item()) {
    nextItem();
  }
}

void SetupDrill::deactivated() {
  ctx_.cube->finishAnimations();
  edit_->clearFocus();
}

void SetupDrill::pause() { flow_.pause(monotonicMs()); }

void SetupDrill::resume() { flow_.resume(monotonicMs()); }

void SetupDrill::nextItem() {
  BlindPieces p = pieces();
  std::vector<LetterChoice> pool =
      letterPool(p != BlindPieces::Corners, p != BlindPieces::Edges, true);
  std::vector<LetterRecord> records;
  for (const LetterChoice &c : pool) {
    const LetterStat &s = ctx_.store.setups().of(c.kind, c.sticker);
    records.push_back({s.attempts, s.accuracy(), s.meanMs()});
  }
  LetterChoice c = pool[pickLetter(pool, records, ctx_.lastLetter, ctx_.rng)];
  flow_.show(c, monotonicMs(), ctx_.away());
  ctx_.lastLetter = c;
  showItem();
}

void SetupDrill::showItem() {
  if (!flow_.item()) {
    return;
  }
  ctx_.result->clear();
  const LetterItem &it = *flow_.item();
  QString kind = pieceName(it.kind, false).toLower();
  ctx_.prompt->setText(
      tr("Set up %1 %2: bring it to %3 (◎)")
          .arg(kind, stickerHtml(it.kind, it.sticker),
               qs(bldStickerName(it.kind, op::swapSpot(it.kind)))));
  ctx_.display->showText(letterOf(it.kind, it.sticker));
  resetCube();
  if (!onCube()) {
    edit_->clear();
    edit_->setEnabled(true);
    edit_->setFocus();
  }
  showHint();
}

void SetupDrill::resetCube() {
  flow_.resetCube();
  ctx_.cube->setState(flow_.cube());
  showCube();
}

void SetupDrill::showCube() {
  const std::optional<LetterItem> &it = flow_.item();
  if (!it) {
    return;
  }
  StickerMarks marks;
  int swap = bldFacelet(it->kind, op::swapSpot(it->kind));
  marks.labels[static_cast<std::size_t>(swap)] = "◎";
  marks.rings.push_back(flow_.target());
  if (it->answered) {
    marks.ringColour = it->right ? kBlindRight : kBlindWrong;
  }
  ctx_.cube->setMarks(marks);
}

bool SetupDrill::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::KeyPress && obj == edit_) {
    int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Escape) {
      edit_->clearFocus();
      return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      checkTyped();
      return true;
    }
  }
  return BlindDrill::eventFilter(obj, event);
}

bool SetupDrill::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  std::optional<Move> move = ctx_.settings.keymap.lookup(e);
  using K = SetupFlow::Key;
  K k = key == Qt::Key_Space                            ? K::Space
        : key == Qt::Key_Return || key == Qt::Key_Enter ? K::Enter
        : key == Qt::Key_Escape                         ? K::Escape
        : move                                          ? K::Move
                                                        : K::Other;
  SetupFlow::Effects fx =
      flow_.key(k, move, press, e->isAutoRepeat(), monotonicMs());
  apply(fx);
  return fx.used;
}

void SetupDrill::apply(const SetupFlow::Effects &e) {
  if (e.next) {
    nextItem();
  }
  if (e.again) {
    showItem();
  }
  if (e.focusField) {
    edit_->setFocus();
  }
  if (e.resetCube) {
    resetCube();
  }
  if (e.onlyLayers) {
    emit message(tr("Rotations aren't part of a setup: turn layers only"));
  }
  const std::optional<LetterItem> &it = flow_.item();
  if (e.skipped && e.answer) {
    std::optional<Alg> setup = ctx_.scheme.setup(it->kind, it->sticker);
    finishItem(*e.answer, tr("Skipped. The setup: %1")
                              .arg(setup ? qs(toString(*setup)).toHtmlEscaped()
                                         : QString()));
    return;
  }
  if (!e.turned) {
    return;
  }
  ctx_.cube->animateMove(*e.turned);
  showCube();
  if (e.answer) {
    std::optional<Alg> scheme = ctx_.scheme.setup(it->kind, it->sticker);
    Alg done = simplify(flow_.moves());
    int moves = htmCount(done);
    finishItem(
        *e.answer,
        tr("Set up in %1: %2. The setup in your scheme: %3")
            .arg(setupMovesText(moves), qs(toString(done)).toHtmlEscaped(),
                 scheme ? qs(toString(*scheme)).toHtmlEscaped() : QString()));
    showCube();
  } else if (e.notValid) {
    ctx_.result->setText(colouredHtml(
        statusColour(Status::Warning),
        tr("It's at the swap spot, but this setup %1. Esc starts "
           "again.")
            .arg(setupProblem(it->kind, it->sticker, toString(flow_.moves()))
                     .toHtmlEscaped())));
  } else {
    ctx_.result->clear();
  }
}

void SetupDrill::finishItem(const LetterAnswer &answer,
                            const QString &feedback) {
  const LetterItem &it = *flow_.item();
  ctx_.store.setups().of(it.kind, it.sticker).record(answer.right, answer.ms);
  ctx_.save();
  refreshLeft();
  QColor colour = answer.right ? kBlindRight : kBlindWrong;
  ctx_.display->showText(answer.right
                             ? blindTimeText(answer.ms, ctx_.settings.decimals)
                             : letterOf(it.kind, it.sticker),
                         colour);
  edit_->clearFocus();
  ctx_.result->setText(colouredHtml(colour, feedback));
  showHint();
}

void SetupDrill::checkTyped() {
  if (!flow_.open()) {
    return;
  }
  std::string text = cleanAlgText(ss(edit_->text()));
  if (text.empty()) {
    // Not an answer: nothing is recorded.
    ctx_.result->setText(
        colouredHtml(kBlindWrong, tr("Type the setup first (Esc outside the "
                                     "field gives the letter up)")));
    return;
  }
  ParseResult parsed = parseAlg(text);
  if (!parsed) {
    ctx_.result->setText(colouredHtml(
        kBlindWrong, tr("Can't read this at character %1: %2")
                         .arg(parsed.error->position + 1)
                         .arg(qs(parsed.error->message).toHtmlEscaped())));
    return; // keep the field to fix it
  }
  const LetterItem &it = *flow_.item();
  std::optional<LetterAnswer> answer =
      flow_.typedSetup(parsed.alg, monotonicMs());
  ctx_.cube->setState(flow_.cube());
  std::optional<Alg> scheme = ctx_.scheme.setup(it.kind, it.sticker);
  QString schemeText =
      scheme ? qs(toString(*scheme)).toHtmlEscaped() : QString();
  if (answer->right) {
    finishItem(*answer,
               tr("Right. The setup in your scheme: %1").arg(schemeText));
  } else {
    finishItem(*answer,
               tr("That setup %1. The setup in your scheme: %2")
                   .arg(setupProblem(it.kind, it.sticker, text).toHtmlEscaped(),
                        schemeText));
  }
  showCube();
}

void SetupDrill::refreshLeft() {
  showLetterStats(left_->panel(), ctx_.store.setups(), pieces(), true,
                  ctx_.scheme.letters, ctx_.settings.decimals,
                  ctx_.page->palette().color(QPalette::PlaceholderText));
}

void SetupDrill::showHint() {
  const std::optional<LetterItem> &it = flow_.item();
  QString text;
  if (it && it->answered) {
    text = tr("Space: next letter. Esc: try it again.");
  } else if (onCube()) {
    text = tr("Turn the cube to bring the ringed sticker to ◎ without "
              "moving the buffer. Esc starts again, Space skips.");
  } else {
    text = tr("Type the setup and press Enter. Esc leaves the field; Esc "
              "again gives the letter up.");
  }
  ctx_.hint->setText(text);
}

} // namespace vcube
