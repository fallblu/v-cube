#include "app/cube_painter.hpp"

#include <QMatrix4x4>
#include <QPainterPath>
#include <QPolygonF>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vcube {

QColor CubeLook::stickerColour(std::uint8_t value) const {
  if (value >= 6) {
    return scheme.masked;
  }
  return scheme.colourOfSlot(map[value]);
}

namespace cube_painter {

namespace {

constexpr double kEyeDistance = 9.0;

struct Cubie {
  Vec3i pos;
  std::array<int, 6> facelet; // per direction (+x,-x,+y,-y,+z,-z), or -1
};

constexpr std::array<Vec3i, 6> kDirections = {Vec3i{1, 0, 0}, Vec3i{-1, 0, 0},
                                              Vec3i{0, 1, 0}, Vec3i{0, -1, 0},
                                              Vec3i{0, 0, 1}, Vec3i{0, 0, -1}};

const std::vector<Cubie> &cubies() {
  static const std::vector<Cubie> list = [] {
    std::vector<Cubie> out;
    for (int x = -1; x <= 1; ++x) {
      for (int y = -1; y <= 1; ++y) {
        for (int z = -1; z <= 1; ++z) {
          if (x == 0 && y == 0 && z == 0) {
            continue;
          }
          Cubie c{{x, y, z}, {-1, -1, -1, -1, -1, -1}};
          for (int i = 0; i < kFaceletCount; ++i) {
            if (faceletPosition(i) == c.pos) {
              for (std::size_t d = 0; d < 6; ++d) {
                if (faceletNormal(i) == kDirections[d]) {
                  c.facelet[d] = i;
                }
              }
            }
          }
          out.push_back(c);
        }
      }
    }
    return out;
  }();
  return list;
}

QVector3D vec(Vec3i v) {
  return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y),
                   static_cast<float>(v.z));
}

QVector3D axisVector(int axis) {
  return axis == 0   ? QVector3D(1, 0, 0)
         : axis == 1 ? QVector3D(0, 1, 0)
                     : QVector3D(0, 0, 1);
}

int coord(Vec3i v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

// Corners of the unit face of cubie `p` facing direction `d`.
std::array<QVector3D, 4> faceCorners(Vec3i p, std::size_t d) {
  Vec3i n = kDirections[d];
  QVector3D centre = vec(p) + 0.5f * vec(n);
  QVector3D u, v;
  if (n.x != 0) {
    u = {0, 1, 0};
    v = {0, 0, 1};
  } else if (n.y != 0) {
    u = {1, 0, 0};
    v = {0, 0, 1};
  } else {
    u = {1, 0, 0};
    v = {0, 1, 0};
  }
  return {centre - 0.5f * u - 0.5f * v, centre + 0.5f * u - 0.5f * v,
          centre + 0.5f * u + 0.5f * v, centre - 0.5f * u + 0.5f * v};
}

QColor shade(const QColor &c, float factor) {
  return QColor::fromRgbF(std::clamp(c.redF() * factor, 0.0f, 1.0f),
                          std::clamp(c.greenF() * factor, 0.0f, 1.0f),
                          std::clamp(c.blueF() * factor, 0.0f, 1.0f));
}

void drawSticker(QPainter &p, const QPolygonF &poly, const QColor &colour,
                 double scale) {
  QPointF centre;
  for (const auto &pt : poly) {
    centre += pt;
  }
  centre /= static_cast<qreal>(poly.size());
  QPolygonF inner;
  for (const auto &pt : poly) {
    inner << centre + (pt - centre) * 0.82;
  }
  QPen pen(colour, std::max(1.0, scale * 0.09), Qt::SolidLine, Qt::RoundCap,
           Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(colour);
  p.drawPolygon(inner);
}

} // namespace

void paint3D(QPainter &p, const QRectF &area, const FaceletCube &cube,
             const CubeLook &look, const std::optional<TurnAnimation> &turn) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);

  QMatrix4x4 view;
  view.rotate(static_cast<float>(look.pitch), 1, 0, 0);
  view.rotate(static_cast<float>(look.yaw), 0, 1, 0);

  QMatrix4x4 turnMatrix;
  int turnAxis = -1;
  if (turn) {
    turnAxis = moveAxis(turn->move.type);
    int quarters = turn->move.amount == 3 ? -1 : turn->move.amount;
    double angle = moveRotationSign(turn->move.type) * 90.0 * quarters *
                   std::clamp(turn->progress, 0.0, 1.0);
    turnMatrix.rotate(static_cast<float>(angle), axisVector(turnAxis));
  }

  double radius = 1.5 * std::sqrt(3.0);
  double fit = std::min(area.width(), area.height()) / 2.0;
  double scale = fit / (radius * kEyeDistance / (kEyeDistance - radius)) * 0.98;
  QPointF centre = area.center();
  QVector3D eye(0, 0, static_cast<float>(kEyeDistance));
  QVector3D light = QVector3D(-0.35f, 0.85f, 0.55f).normalized();

  auto project = [&](const QVector3D &v) {
    double k = kEyeDistance / (kEyeDistance - v.z());
    return QPointF(centre.x() + v.x() * k * scale,
                   centre.y() - v.y() * k * scale);
  };

  // Group cubies into slabs along the turning axis so that painting slabs
  // back to front, then cubies within a slab back to front, stays correct
  // while a layer is rotated.
  const auto &all = cubies();
  std::array<std::vector<const Cubie *>, 3> slabs;
  for (const auto &c : all) {
    int s = turnAxis >= 0 ? coord(c.pos, turnAxis) + 1 : 1;
    slabs[static_cast<std::size_t>(s)].push_back(&c);
  }
  std::array<int, 3> slabOrder{0, 1, 2};
  if (turnAxis >= 0) {
    float dz = view.mapVector(axisVector(turnAxis)).z();
    std::sort(slabOrder.begin(), slabOrder.end(), [dz](int a, int b) {
      return static_cast<float>(a - 1) * dz < static_cast<float>(b - 1) * dz;
    });
  }

  for (int s : slabOrder) {
    auto &slab = slabs[static_cast<std::size_t>(s)];
    struct Placed {
      const Cubie *cubie;
      QMatrix4x4 m;
      float depth;
    };
    std::vector<Placed> placed;
    for (const Cubie *c : slab) {
      QMatrix4x4 m = view;
      if (turn && moveTurnsCubie(turn->move.type, c->pos)) {
        m = view * turnMatrix;
      }
      placed.push_back({c, m, (m.map(vec(c->pos))).z()});
    }
    std::sort(
        placed.begin(), placed.end(),
        [](const Placed &a, const Placed &b) { return a.depth < b.depth; });
    for (const auto &pl : placed) {
      for (std::size_t d = 0; d < 6; ++d) {
        auto corners = faceCorners(pl.cubie->pos, d);
        std::array<QVector3D, 4> t{};
        QVector3D faceCentre;
        for (std::size_t i = 0; i < 4; ++i) {
          t[i] = pl.m.map(corners[i]);
          faceCentre += t[i];
        }
        faceCentre /= 4.0f;
        QVector3D normal = pl.m.mapVector(vec(kDirections[d])).normalized();
        if (QVector3D::dotProduct(normal, eye - faceCentre) <= 0) {
          continue;
        }
        QPolygonF poly;
        for (const auto &v : t) {
          poly << project(v);
        }
        p.setPen(QPen(look.scheme.body, 1.0));
        p.setBrush(look.scheme.body);
        p.drawPolygon(poly);
        int f = pl.cubie->facelet[d];
        if (f >= 0) {
          float lit =
              0.70f +
              0.30f * std::max(0.0f, QVector3D::dotProduct(normal, light));
          drawSticker(p, poly, shade(look.stickerColour(cube[f]), lit), scale);
        }
      }
    }
  }
  p.restore();
}

void paintNet(QPainter &p, const QRectF &area, const FaceletCube &cube,
              const CubeLook &look) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  double cell = std::min(area.width() / 12.0, area.height() / 9.0);
  QPointF origin(area.center().x() - cell * 6.0,
                 area.center().y() - cell * 4.5);
  // Face placement in the net, in 3x3 blocks: U R F D L B.
  constexpr std::array<std::array<int, 2>, 6> kBlock = {
      {{1, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 1}, {3, 1}}};
  double gap = std::max(1.0, cell * 0.08);
  for (int f = 0; f < 6; ++f) {
    for (int i = 0; i < 9; ++i) {
      int r = i / 3, c = i % 3;
      QRectF rect(
          origin.x() + (kBlock[static_cast<std::size_t>(f)][0] * 3 + c) * cell +
              gap / 2,
          origin.y() + (kBlock[static_cast<std::size_t>(f)][1] * 3 + r) * cell +
              gap / 2,
          cell - gap, cell - gap);
      p.setPen(Qt::NoPen);
      p.setBrush(look.stickerColour(cube[f * 9 + i]));
      p.drawRoundedRect(rect, cell * 0.12, cell * 0.12);
    }
  }
  p.restore();
}

void paintFlat(QPainter &p, const QRectF &area, const FaceletCube &cube,
               const CubeLook &look) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  // The side columns are narrower, and a small gap separates the U and F
  // halves, as in Alg-Trainer.
  constexpr double kSide = 0.55;
  constexpr double kSpacing = 0.12;
  double units = 3.0 + 2.0 * (kSide + kSpacing);
  double cell =
      std::min(area.width() / units, area.height() / (6.0 + kSpacing));
  double gap = std::max(1.0, cell * 0.07);
  double width = units * cell;
  double height = (6.0 + kSpacing) * cell;
  QPointF origin(area.center().x() - width / 2, area.center().y() - height / 2);
  const auto &layout = flatLayout();
  for (int r = 0; r < kFlatRows; ++r) {
    double y = origin.y() + r * cell + (r >= 3 ? kSpacing * cell : 0.0);
    for (int c = 0; c < kFlatColumns; ++c) {
      double x, w;
      if (c == 0) {
        x = origin.x();
        w = kSide * cell;
      } else if (c == kFlatColumns - 1) {
        x = origin.x() + width - kSide * cell;
        w = kSide * cell;
      } else {
        x = origin.x() + (kSide + kSpacing + (c - 1)) * cell;
        w = cell;
      }
      QRectF rect(x + gap / 2, y + gap / 2, w - gap, cell - gap);
      p.setPen(Qt::NoPen);
      p.setBrush(look.stickerColour(cube[layout[static_cast<std::size_t>(r)]
                                               [static_cast<std::size_t>(c)]]));
      p.drawRoundedRect(rect, cell * 0.1, cell * 0.1);
    }
  }
  p.restore();
}

void paintTopLayer(QPainter &p, const QRectF &area, const FaceletCube &cube,
                   const CubeLook &look) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  // 3 cells plus a thin side strip on each side.
  constexpr double kStrip = 0.32;
  constexpr double kSpacing = 0.10;
  double units = 3.0 + 2.0 * (kStrip + kSpacing);
  double cell = std::min(area.width(), area.height()) / units;
  double gap = std::max(1.0, cell * 0.07);
  QPointF topLeft(area.center().x() - 1.5 * cell,
                  area.center().y() - 1.5 * cell);
  auto fill = [&](const QRectF &r, std::uint8_t v) {
    p.setPen(Qt::NoPen);
    p.setBrush(look.stickerColour(v));
    p.drawRoundedRect(r.adjusted(gap / 2, gap / 2, -gap / 2, -gap / 2),
                      cell * 0.1, cell * 0.1);
  };
  for (int i = 0; i < 9; ++i) {
    fill(QRectF(topLeft.x() + (i % 3) * cell, topLeft.y() + (i / 3) * cell,
                cell, cell),
         cube[i]);
  }
  double strip = kStrip * cell;
  double off = kSpacing * cell;
  for (int k = 0; k < 3; ++k) {
    // Back (above): B's top row runs right to left as seen from above.
    fill(QRectF(topLeft.x() + k * cell, topLeft.y() - off - strip, cell, strip),
         cube[47 - k]);
    // Front (below): F's top row, left to right.
    fill(QRectF(topLeft.x() + k * cell, topLeft.y() + 3 * cell + off, cell,
                strip),
         cube[18 + k]);
    // Left: L's top row runs back to front.
    fill(QRectF(topLeft.x() - off - strip, topLeft.y() + k * cell, strip, cell),
         cube[36 + k]);
    // Right: R's top row runs front to back.
    fill(QRectF(topLeft.x() + 3 * cell + off, topLeft.y() + k * cell, strip,
                cell),
         cube[11 - k]);
  }
  p.restore();
}

QPixmap diagram(const FaceletCube &cube, DiagramView view, int size,
                const CubeLook &look, qreal devicePixelRatio) {
  QPixmap pm(static_cast<int>(size * devicePixelRatio),
             static_cast<int>(size * devicePixelRatio));
  pm.setDevicePixelRatio(devicePixelRatio);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  QRectF area(0, 0, size, size);
  if (view == DiagramView::Iso3D) {
    paint3D(p, area, cube, look);
  } else {
    paintTopLayer(p, area, cube, look);
  }
  return pm;
}

} // namespace cube_painter

} // namespace vcube
