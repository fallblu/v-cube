#pragma once

#include "app/colours.hpp"
#include "cube/facelet.hpp"
#include "train/algset.hpp"

#include <QColor>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace vcube {

// A layer turn in progress: `move` is `progress` (0..1) of the way done.
struct TurnAnimation {
  Move move;
  double progress = 0.0;
};

struct CubeLook {
  ColourScheme scheme = ColourScheme::standard();
  ColourMap map = kIdentityColourMap;
  double yaw = -38.0;  // degrees about the vertical axis
  double pitch = 28.0; // degrees of tilt towards the viewer

  QColor stickerColour(std::uint8_t value) const;
};

// Marks drawn over stickers, by facelet index: a ring around some (e.g. the
// sticker a drill asks about) and a short label on others (e.g. a
// lettering scheme's letters).
struct StickerMarks {
  std::vector<int> rings;
  QColor ringColour{0xff, 0x2d, 0xd4};
  std::array<QString, kFaceletCount> labels{};

  bool hasLabels() const;
  bool empty() const { return rings.empty() && !hasLabels(); }
  bool ringed(int facelet) const;
};

// Stateless drawing of a facelet cube.
namespace cube_painter {

// Perspective 3D view of U, F and R, optionally mid-turn.
void paint3D(QPainter &p, const QRectF &area, const FaceletCube &cube,
             const CubeLook &look,
             const std::optional<TurnAnimation> &turn = std::nullopt,
             const StickerMarks *marks = nullptr);

// Unfolded net: U above L F R B, D below.
void paintNet(QPainter &p, const QRectF &area, const FaceletCube &cube,
              const CubeLook &look, const StickerMarks *marks = nullptr);

// Alg-Trainer's flat view (see flatLayout()): U above F with the adjacent
// L and R stickers, 5 stickers wide and 6 tall.
void paintFlat(QPainter &p, const QRectF &area, const FaceletCube &cube,
               const CubeLook &look, const StickerMarks *marks = nullptr);

// Last-layer diagram: the U face from above with the side stickers of the
// top layer around it.
void paintTopLayer(QPainter &p, const QRectF &area, const FaceletCube &cube,
                   const CubeLook &look);

// The yaw that shows the cube from the corner of a 3D diagram view, given
// the front-right yaw; TopLayer gives the front-right yaw too.
double yawFor(DiagramView view, double frontRightYaw);

// A square case diagram in the given view, for lists and reveals.
QPixmap diagram(const FaceletCube &cube, DiagramView view, int size,
                const CubeLook &look, qreal devicePixelRatio = 1.0);
// diagram(), drawn once for each cube, view, look and size and then kept,
// for the case lists, which are rebuilt often.
QPixmap cachedDiagram(const FaceletCube &cube, DiagramView view, int size,
                      const CubeLook &look, qreal devicePixelRatio = 1.0);

} // namespace cube_painter

} // namespace vcube
