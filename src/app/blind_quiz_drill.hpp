#pragma once

#include "app/blind_drill.hpp"

#include <QTimer>

namespace vcube {

// The letter quiz: a sticker of the solved cube is ringed on the net and
// its letter typed, or a letter is shown and its sticker clicked. The next
// letter is weighed towards the slow and missed ones.
class QuizDrill : public BlindDrill {
  Q_OBJECT
public:
  QuizDrill(BlindContext &context, QWidget *parent);

  QWidget *leftPanel() const override;
  std::vector<QWidget *> options() const override;
  Layout layout() const override;
  View view() const override;

  void enter(bool changed) override;
  void leave() override;
  bool handleKey(QKeyEvent *e, bool press) override;
  void activated() override;
  void deactivated() override;
  void pause() override;
  void resume() override;
  void refreshLeft() override;
  void showHint() override;
  void stickerClicked(int facelet) override;

private:
  // Name the ringed sticker's letter, click the letter's sticker, or both
  // in turn.
  enum class Direction { ToLetter, ToSticker, Mixed };

  QuizFlow flow_;
  QTimer nextTimer_; // on to the next letter after a right answer
  BlindStatsPanel *left_;
  QComboBox *piecesBox_;
  QComboBox *directionBox_;
  QWidget *piecesItem_;
  QWidget *directionItem_;

  BlindPieces pieces() const;
  Direction direction() const;
  void apply(const QuizFlow::Effects &e, const QString &feedback);
  // Nothing is asked (or timed) until Space.
  void showReady();
  void nextItem();
  void showItem();
  void finishItem(const LetterAnswer &answer, const QString &feedback);
};

} // namespace vcube
