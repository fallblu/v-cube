#include "app/cube_painter.hpp"

#include <QCache>
#include <QDataStream>
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

bool StickerMarks::hasLabels() const {
  return std::any_of(labels.begin(), labels.end(),
                     [](const QString &l) { return !l.isEmpty(); });
}

bool StickerMarks::ringed(int facelet) const {
  return std::find(rings.begin(), rings.end(), facelet) != rings.end();
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

// Black or white, whichever reads better on `background`.
QColor labelColour(const QColor &background) {
  double luma = 0.299 * background.redF() + 0.587 * background.greenF() +
                0.114 * background.blueF();
  return luma > 0.55 ? QColor(0x10, 0x10, 0x10) : QColor(0xf4, 0xf4, 0xf4);
}

// A sticker's ring and label, drawn over the sticker `shape` (its outline
// as drawn) of size `size` (about a sticker's width).
void drawMarks(QPainter &p, const QPolygonF &shape, const QColor &sticker,
               double size, const StickerMarks &marks, int facelet) {
  if (facelet < 0) {
    return;
  }
  QPointF centre;
  for (const auto &pt : shape) {
    centre += pt;
  }
  centre /= static_cast<qreal>(shape.size());
  const QString &label = marks.labels[static_cast<std::size_t>(facelet)];
  if (!label.isEmpty()) {
    QFont font = p.font();
    font.setPixelSize(std::max(7, static_cast<int>(size * 0.48)));
    font.setBold(true);
    p.setFont(font);
    p.setPen(labelColour(sticker));
    QRectF box(centre.x() - size, centre.y() - size / 2, 2 * size, size);
    p.drawText(box, Qt::AlignCenter, label);
  }
  if (marks.ringed(facelet)) {
    QPolygonF ring;
    for (const auto &pt : shape) {
      ring << centre + (pt - centre) * 0.9;
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x10, 0x10, 0x10), std::max(2.0, size * 0.2),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolygon(ring);
    p.setPen(QPen(marks.ringColour, std::max(1.5, size * 0.11), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawPolygon(ring);
  }
}

QPolygonF rectPolygon(const QRectF &r) {
  return QPolygonF(
      {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
}

// Where paintNet() draws each face's 3x3 block, and the cell size.
struct NetGeometry {
  double cell = 0;
  QPointF origin;
  double gap = 1;

  explicit NetGeometry(const QRectF &area) {
    cell = std::min(area.width() / 12.0, area.height() / 9.0);
    origin =
        QPointF(area.center().x() - cell * 6.0, area.center().y() - cell * 4.5);
    gap = std::max(1.0, cell * 0.08);
  }

  // Face placement in the net, in 3x3 blocks: U R F D L B.
  static constexpr std::array<std::array<int, 2>, 6> kBlock = {
      {{1, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 1}, {3, 1}}};

  QRectF sticker(int facelet) const {
    int f = facelet / 9, i = facelet % 9;
    int r = i / 3, c = i % 3;
    const auto &block = kBlock[static_cast<std::size_t>(f)];
    return QRectF(origin.x() + (block[0] * 3 + c) * cell + gap / 2,
                  origin.y() + (block[1] * 3 + r) * cell + gap / 2, cell - gap,
                  cell - gap);
  }
};

} // namespace

void paint3D(QPainter &p, const QRectF &area, const FaceletCube &cube,
             const CubeLook &look, const std::optional<TurnAnimation> &turn,
             const StickerMarks *marks) {
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

  // Fit the cube's outline as seen (its eight corners, projected), not a
  // sphere around it, so that it fills the area. A layer mid-turn may
  // reach a little past it.
  double left = 0, right = 0, top = 0, bottom = 0;
  bool firstCorner = true;
  for (float cx : {-1.5f, 1.5f}) {
    for (float cy : {-1.5f, 1.5f}) {
      for (float cz : {-1.5f, 1.5f}) {
        QVector3D v = view.map(QVector3D(cx, cy, cz));
        double k = kEyeDistance / (kEyeDistance - v.z());
        double px = v.x() * k, py = -v.y() * k;
        if (firstCorner) {
          left = right = px;
          top = bottom = py;
          firstCorner = false;
        }
        left = std::min(left, px);
        right = std::max(right, px);
        top = std::min(top, py);
        bottom = std::max(bottom, py);
      }
    }
  }
  double scale =
      std::min(area.width() / (right - left), area.height() / (bottom - top)) *
      0.95;
  QPointF centre = area.center() - QPointF((left + right) / 2.0 * scale,
                                           (top + bottom) / 2.0 * scale);
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
          QColor colour = shade(look.stickerColour(cube[f]), lit);
          drawSticker(p, poly, colour, scale);
          if (marks) {
            QPolygonF inner;
            QPointF mid = (poly[0] + poly[1] + poly[2] + poly[3]) / 4.0;
            for (const auto &pt : poly) {
              inner << mid + (pt - mid) * 0.82;
            }
            drawMarks(p, inner, colour, scale * 0.85, *marks, f);
          }
        }
      }
    }
  }
  p.restore();
}

void paintNet(QPainter &p, const QRectF &area, const FaceletCube &cube,
              const CubeLook &look, const StickerMarks *marks) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  NetGeometry net(area);
  for (int i = 0; i < kFaceletCount; ++i) {
    QRectF rect = net.sticker(i);
    QColor colour = look.stickerColour(cube[i]);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawRoundedRect(rect, net.cell * 0.12, net.cell * 0.12);
    if (marks) {
      drawMarks(p, rectPolygon(rect), colour, net.cell * 0.8, *marks, i);
    }
  }
  p.restore();
}

void paintFlat(QPainter &p, const QRectF &area, const FaceletCube &cube,
               const CubeLook &look, const StickerMarks *marks) {
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
      int facelet =
          layout[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
      QColor colour = look.stickerColour(cube[facelet]);
      p.setPen(Qt::NoPen);
      p.setBrush(colour);
      p.drawRoundedRect(rect, cell * 0.1, cell * 0.1);
      if (marks) {
        drawMarks(p, rectPolygon(rect), colour, std::min(w, cell) * 0.8, *marks,
                  facelet);
      }
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

double yawFor(DiagramView view, double frontRightYaw) {
  // The other corners' views mirror the front-right one.
  switch (view) {
  case DiagramView::TopLayer:
  case DiagramView::Iso3D:
    return frontRightYaw;
  case DiagramView::Iso3DFrontLeft:
    return -frontRightYaw;
  case DiagramView::Iso3DBackLeft:
    return 180.0 + frontRightYaw;
  case DiagramView::Iso3DBackRight:
    return -180.0 - frontRightYaw;
  }
  return frontRightYaw;
}

QPixmap diagram(const FaceletCube &cube, DiagramView view, int size,
                const CubeLook &look, qreal devicePixelRatio) {
  QPixmap pm(static_cast<int>(size * devicePixelRatio),
             static_cast<int>(size * devicePixelRatio));
  pm.setDevicePixelRatio(devicePixelRatio);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  QRectF area(0, 0, size, size);
  if (view == DiagramView::TopLayer) {
    paintTopLayer(p, area, cube, look);
  } else {
    CubeLook turned = look;
    turned.yaw = yawFor(view, look.yaw);
    paint3D(p, area, cube, turned);
  }
  return pm;
}

QPixmap cachedDiagram(const FaceletCube &cube, DiagramView view, int size,
                      const CubeLook &look, qreal devicePixelRatio) {
  // Enough for several large sets in two sizes and a couple of looks.
  static QCache<QByteArray, QPixmap> cache(6000);
  QByteArray key(reinterpret_cast<const char *>(cube.stickers().data()),
                 kFaceletCount);
  QDataStream out(&key, QIODeviceBase::Append);
  out << static_cast<int>(view) << size << devicePixelRatio << look.yaw
      << look.pitch << look.scheme.masked << look.scheme.body;
  for (std::size_t i = 0; i < 6; ++i) {
    out << look.scheme.faces[i] << look.map[i];
  }
  if (QPixmap *hit = cache.object(key)) {
    return *hit;
  }
  QPixmap pm = diagram(cube, view, size, look, devicePixelRatio);
  cache.insert(key, new QPixmap(pm));
  return pm;
}

} // namespace cube_painter

} // namespace vcube
