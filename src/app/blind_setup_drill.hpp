#pragma once

#include "app/blind_drill.hpp"

class QLineEdit;

namespace vcube {

// Setup moves: the setup for a letter is done on the virtual cube (the
// ringed sticker must reach the swap spot without moving the buffer or the
// swap alg's other pieces) or typed. The next letter is weighed towards
// the slow and missed ones.
class SetupDrill : public BlindDrill {
  Q_OBJECT
public:
  SetupDrill(BlindContext &context, QWidget *parent);

  QWidget *leftPanel() const override;
  std::vector<QWidget *> options() const override;
  QWidget *inputRow() const override;

  void enter(bool changed) override;
  void leave() override;
  bool handleKey(QKeyEvent *e, bool press) override;
  void activated() override;
  void deactivated() override;
  void pause() override;
  void resume() override;
  void refreshLeft() override;
  void showHint() override;

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  SetupFlow flow_;
  BlindStatsPanel *left_;
  QComboBox *piecesBox_;
  QComboBox *modeBox_;
  QWidget *piecesItem_;
  QWidget *modeItem_;
  QWidget *row_;
  QLineEdit *edit_;

  BlindPieces pieces() const;
  bool onCube() const;
  void apply(const SetupFlow::Effects &e);
  void nextItem();
  void showItem();
  void finishItem(const LetterAnswer &answer, const QString &feedback);
  void resetCube();
  void showCube();
  void checkTyped();
};

} // namespace vcube
