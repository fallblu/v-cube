#pragma once

#include "app/colours.hpp"
#include "cube/facelet.hpp"
#include "train/algset.hpp"

#include <QPainter>
#include <QPixmap>
#include <QRectF>

#include <optional>

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

// Stateless drawing of a facelet cube.
namespace cube_painter {

// Perspective 3D view of U, F and R, optionally mid-turn.
void paint3D(QPainter &p, const QRectF &area, const FaceletCube &cube,
             const CubeLook &look,
             const std::optional<TurnAnimation> &turn = std::nullopt);

// Unfolded net: U above L F R B, D below.
void paintNet(QPainter &p, const QRectF &area, const FaceletCube &cube,
              const CubeLook &look);

// Alg-Trainer's flat view (see flatLayout()): U above F with the adjacent
// L and R stickers, 5 stickers wide and 6 tall.
void paintFlat(QPainter &p, const QRectF &area, const FaceletCube &cube,
               const CubeLook &look);

// Last-layer diagram: the U face from above with the side stickers of the
// top layer around it.
void paintTopLayer(QPainter &p, const QRectF &area, const FaceletCube &cube,
                   const CubeLook &look);

// A square case diagram in the given view, for lists and reveals.
QPixmap diagram(const FaceletCube &cube, DiagramView view, int size,
                const CubeLook &look, qreal devicePixelRatio = 1.0);

} // namespace cube_painter

} // namespace vcube
