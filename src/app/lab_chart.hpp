#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <vector>

namespace vcube {

// Alg Lab's comparison chart of the algs timed: each alg's times as a
// strip of dots with its median and middle half marked (Spread), or each
// alg's times in the order they were done, as lines on one time axis
// (Over attempts). A legend names the algs; hovering a dot shows its time.
class LabChart : public QWidget {
  Q_OBJECT
public:
  enum class Mode { Spread, Trend };

  struct Series {
    QString name;
    QColor colour;
    std::vector<std::int64_t> times; // ms, oldest first, DNFs left out
  };

  explicit LabChart(QWidget *parent = nullptr);
  void setSeries(std::vector<Series> series);
  void setMode(Mode m);
  Mode mode() const { return mode_; }
  void setDecimals(int d);
  void setEmptyText(const QString &text);
  QSize sizeHint() const override { return {420, 220}; }
  QSize minimumSizeHint() const override { return {200, 120}; }

protected:
  void paintEvent(QPaintEvent *) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void leaveEvent(QEvent *e) override;

private:
  struct Dot {
    QPointF at;
    std::size_t series = 0;
    std::int64_t ms = 0;
  };
  std::vector<Series> series_;
  std::vector<Dot> dots_; // as last painted, for hovering
  Mode mode_ = Mode::Spread;
  int decimals_ = 2;
  QString emptyText_;
  int hover_ = -1;
};

} // namespace vcube
