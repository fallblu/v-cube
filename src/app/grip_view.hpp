#pragma once

#include "train/grips.hpp"

#include <QWidget>

#include <optional>

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace vcube {

// How to turn one alg with two hands, from a grip profile: the grip to
// start in, then a column per move with the trick each hand turns it with
// and where a hand regrips. Clicking a move lists every trick that can
// turn it, with what the whole alg costs that way; picking one plans the
// rest round it (until the alg changes or Reset is pressed).
class GripView : public QWidget {
  Q_OBJECT
public:
  explicit GripView(QWidget *parent = nullptr);

  // Shows `alg` turned with `model`; with no model, or text that can't be
  // read, shows `why` instead.
  void setAlg(const QString &alg, const GripModel *model,
              const QString &why = {});
  void clear(const QString &why = {});

protected:
  void changeEvent(QEvent *e) override;

private:
  QLabel *summary_;
  QTableWidget *lanes_;
  QLabel *optionsTitle_;
  QListWidget *options_;
  QPushButton *reset_;
  QWidget *body_;

  QString text_;
  Alg alg_;
  std::optional<GripModel> model_;
  GripChoice choice_;
  GripResult result_;

  void replan();
  void fillLanes();
  void fillOptions();
  void optionPicked();
};

} // namespace vcube
