#include "app/blind_quiz_drill.hpp"

#include "app/session_panel.hpp"
#include "app/ui_state.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"

#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
#include <random>

namespace vcube {

QuizDrill::QuizDrill(BlindContext &context, QWidget *parent)
    : BlindDrill(context, parent) {
  left_ = new BlindStatsPanel(true, parent);
  piecesBox_ = blindPiecesBox(parent);
  directionBox_ = blindComboBox(
      {tr("Sticker → letter"), tr("Letter → sticker"), tr("Mixed")}, parent);
  directionBox_->setToolTip(
      tr("Name the ringed sticker's letter, click the sticker of a letter "
         "on the net, or both in turn"));
  piecesItem_ = labelledBox(tr("Pieces:"), piecesBox_, parent);
  directionItem_ = labelledBox(tr("Ask:"), directionBox_, parent);

  nextTimer_.setSingleShot(true);
  connect(&nextTimer_, &QTimer::timeout, this, [this] {
    nextItem();
    flow_.movedOn(monotonicMs());
  });
  flow_.setFold(
      [](std::string_view s) { return ss(foldedLetter(qs(std::string(s)))); });
  for (QComboBox *box : {piecesBox_, directionBox_}) {
    connect(box, &QComboBox::activated, this, [this] {
      saved::blindQuizPieces().set(piecesBox_->currentIndex());
      saved::blindQuizDirection().set(directionBox_->currentIndex());
      emit restartRequested();
    });
  }
  connect(left_->resetButton(), &QPushButton::clicked, this, [this] {
    auto answer = QMessageBox::question(
        ctx_.page, tr("Reset statistics"),
        tr("Forget the letter quiz statistics of every letter?"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    ctx_.store.quiz() = LetterStats{};
    ctx_.save();
    refreshLeft();
  });
}

QWidget *QuizDrill::leftPanel() const { return left_; }

std::vector<QWidget *> QuizDrill::options() const {
  return {piecesItem_, directionItem_};
}

BlindDrill::Layout QuizDrill::layout() const {
  Layout l;
  l.viewPicker = false; // always the net
  return l;
}

BlindDrill::View QuizDrill::view() const {
  return {View::Kind::Net, {}}; // every sticker can be seen and clicked
}

BlindPieces QuizDrill::pieces() const {
  return static_cast<BlindPieces>(std::clamp(piecesBox_->currentIndex(), 0, 2));
}

QuizDrill::Direction QuizDrill::direction() const {
  return static_cast<Direction>(
      std::clamp(directionBox_->currentIndex(), 0, 2));
}

void QuizDrill::enter(bool changed) {
  (void)changed; // the options are read again either way
  nextTimer_.stop();
  flow_.reset();
  piecesBox_->setCurrentIndex(saved::blindQuizPieces().get());
  directionBox_->setCurrentIndex(saved::blindQuizDirection().get());
  showReady();
}

void QuizDrill::leave() {
  nextTimer_.stop();
  flow_.reset();
}

void QuizDrill::activated() {
  if (!flow_.item()) {
    showReady();
  }
}

void QuizDrill::deactivated() {
  nextTimer_.stop();
  ctx_.cube->finishAnimations();
  // Back on the page, the first letter waits for Space again.
  flow_.reset();
  showReady();
}

void QuizDrill::showReady() {
  ctx_.result->clear();
  ctx_.cube->setState(FaceletCube());
  ctx_.cube->clearMarks();
  ctx_.prompt->setText(
      tr("Letter quiz: each letter is timed from the moment it is shown."));
  ctx_.display->showText("–",
                         ctx_.page->palette().color(QPalette::PlaceholderText));
  showHint();
}

void QuizDrill::pause() { flow_.pause(monotonicMs()); }

void QuizDrill::resume() { flow_.resume(monotonicMs()); }

void QuizDrill::nextItem() {
  nextTimer_.stop();
  BlindPieces p = pieces();
  std::vector<LetterChoice> pool =
      letterPool(p != BlindPieces::Corners, p != BlindPieces::Edges, false);
  std::vector<LetterRecord> records;
  for (const LetterChoice &c : pool) {
    const LetterStat &s = ctx_.store.quiz().of(c.kind, c.sticker);
    records.push_back({s.attempts, s.accuracy(), s.meanMs()});
  }
  LetterChoice c = pool[pickLetter(pool, records, ctx_.lastLetter, ctx_.rng)];
  Direction dir = direction();
  bool toLetter = dir == Direction::ToLetter ||
                  (dir == Direction::Mixed &&
                   std::uniform_int_distribution<int>(0, 1)(ctx_.rng) == 0);
  flow_.show(c, toLetter, monotonicMs(), ctx_.away());
  ctx_.lastLetter = c;
  showItem();
}

void QuizDrill::showItem() {
  if (!flow_.item()) {
    return;
  }
  ctx_.result->clear();
  const LetterItem &it = *flow_.item();
  QString kind = pieceName(it.kind, false).toLower();
  ctx_.cube->setState(FaceletCube());
  StickerMarks marks;
  if (it.toLetter) {
    marks.rings.push_back(bldFacelet(it.kind, it.sticker));
    ctx_.prompt->setText(
        tr("Which letter is the ringed %1 sticker?").arg(kind));
    ctx_.display->showText(
        "?", ctx_.page->palette().color(QPalette::PlaceholderText));
  } else {
    ctx_.prompt->setText(
        tr("Click the %1 sticker with this letter:").arg(kind));
    ctx_.display->showText(letterOf(it.kind, it.sticker));
  }
  ctx_.cube->setMarks(marks);
  showHint();
}

bool QuizDrill::handleKey(QKeyEvent *e, bool press) {
  if (!flow_.item()) {
    // Ready: only Space starts.
    if (e->key() != Qt::Key_Space) {
      return false;
    }
    if (press && !e->isAutoRepeat()) {
      nextItem();
    }
    return true;
  }
  int key = e->key();
  QString text = e->text();
  bool space = key == Qt::Key_Space;
  bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
  bool letter = !text.isEmpty() && text.at(0).isPrint() && !space;
  using K = QuizFlow::Key;
  K k = space                      ? K::Space
        : key == Qt::Key_Backspace ? K::Backspace
        : enter                    ? K::Enter
        : letter                   ? K::Letter
                                   : K::Other;
  QuizFlow::Effects fx = flow_.key(k, ss(text), press, e->isAutoRepeat(),
                                   monotonicMs(), ctx_.scheme.letters);
  QString feedback;
  if (fx.answer) {
    const LetterItem &it = *flow_.item();
    feedback = fx.givenUp ? tr("Given up.")
               : fx.answer->right
                   ? tr("Right: %1.").arg(stickerHtml(it.kind, it.sticker))
                   : tr("“%1” is wrong: it's %2.")
                         .arg(qs(flow_.typed()).toHtmlEscaped(),
                              stickerHtml(it.kind, it.sticker));
  }
  apply(fx, feedback);
  return fx.used;
}

void QuizDrill::stickerClicked(int facelet) {
  if (!flow_.open() || flow_.item()->toLetter) {
    return;
  }
  const LetterItem &it = *flow_.item();
  std::optional<int> clicked = bldSticker(it.kind, facelet);
  QString feedback;
  if (clicked && *clicked == it.sticker) {
    feedback = tr("Right: %1.").arg(stickerHtml(it.kind, it.sticker));
  } else {
    QString what;
    if (clicked) {
      what = tr("that's %1").arg(stickerHtml(it.kind, *clicked));
    } else {
      BldPiece other =
          it.kind == BldPiece::Edge ? BldPiece::Corner : BldPiece::Edge;
      what = bldSticker(other, facelet)
                 ? (other == BldPiece::Edge ? tr("that's an edge sticker")
                                            : tr("that's a corner sticker"))
                 : tr("that's a centre");
    }
    feedback = tr("Wrong, %1. %2 is ringed.")
                   .arg(what, stickerHtml(it.kind, it.sticker));
  }
  apply(flow_.click(clicked, monotonicMs()), feedback);
}

void QuizDrill::apply(const QuizFlow::Effects &e, const QString &feedback) {
  if (e.next) {
    nextItem();
  }
  if (e.answer) {
    finishItem(*e.answer, feedback);
    if (e.autoNext) {
      nextTimer_.start(QuizFlow::kAutoNextMs); // or Space now
    }
  }
  if (e.showTyped) {
    ctx_.display->showText(flow_.typed().empty() ? QString("?")
                                                 : qs(flow_.typed()));
  }
}

void QuizDrill::finishItem(const LetterAnswer &answer,
                           const QString &feedback) {
  const LetterItem &it = *flow_.item();
  ctx_.store.quiz().of(it.kind, it.sticker).record(answer.right, answer.ms);
  ctx_.save();
  refreshLeft();
  QColor colour = answer.right ? kBlindRight : kBlindWrong;
  // The right sticker, ringed and lettered.
  StickerMarks marks;
  int f = bldFacelet(it.kind, it.sticker);
  marks.rings.push_back(f);
  marks.ringColour = colour;
  marks.labels[static_cast<std::size_t>(f)] = letterOf(it.kind, it.sticker);
  ctx_.cube->setMarks(marks);
  ctx_.display->showText(
      it.toLetter ? (flow_.typed().empty() ? QString("–") : qs(flow_.typed()))
                  : letterOf(it.kind, it.sticker),
      colour);
  ctx_.result->setText(colouredHtml(colour, feedback));
  showHint();
}

void QuizDrill::refreshLeft() {
  showLetterStats(left_->panel(), ctx_.store.quiz(), pieces(), false,
                  ctx_.scheme.letters, ctx_.settings.decimals,
                  ctx_.page->palette().color(QPalette::PlaceholderText));
}

void QuizDrill::showHint() {
  const std::optional<LetterItem> &it = flow_.item();
  if (!it) {
    ctx_.hint->setHint(tr("Ready when you are."), {{tr("Space"), tr("start")}});
  } else if (it->answered) {
    ctx_.hint->setHint({}, {{tr("Space"), tr("next letter")}});
  } else if (it && it->toLetter) {
    ctx_.hint->setHint(tr("Type the ringed sticker's letter."),
                       {{tr("Enter"), tr("for a longer label")},
                        {tr("Backspace"), tr("correct")},
                        {tr("Space"), tr("give up")}});
  } else {
    ctx_.hint->setHint(tr("Click the letter's sticker on the net."),
                       {{tr("Space"), tr("give up")}});
  }
}

} // namespace vcube
