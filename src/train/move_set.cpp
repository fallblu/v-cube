#include "train/move_set.hpp"

#include <cctype>

namespace vcube {

namespace {

// Names of the layer types as toString() writes them.
constexpr std::array<std::string_view, kLayerTypes> kLayerNames = {
    "U", "R", "F", "D", "L", "B", "u", "r", "f", "d", "l", "b", "M", "E", "S"};

std::optional<MoveType> layerOf(std::string_view name) {
  for (std::size_t i = 0; i < kLayerNames.size(); ++i) {
    if (kLayerNames[i] == name) {
      return static_cast<MoveType>(i);
    }
  }
  // Rw, Uw ...
  if (name.size() == 2 && name[1] == 'w') {
    for (std::size_t i = 0; i < 6; ++i) {
      if (kLayerNames[i][0] == name[0]) {
        return static_cast<MoveType>(i + 6);
      }
    }
  }
  return std::nullopt;
}

constexpr LayerTurns kBoth{true, true};

} // namespace

MoveSet MoveSet::allFaces() {
  MoveSet s;
  for (std::size_t i = 0; i < 6; ++i) {
    s.layers[i] = kBoth;
  }
  return s;
}

bool MoveSet::empty() const {
  for (const auto &l : layers) {
    if (l.any()) {
      return false;
    }
  }
  return true;
}

std::vector<Move> MoveSet::moves() const {
  std::vector<Move> out;
  for (std::size_t i = 0; i < layers.size(); ++i) {
    auto t = static_cast<MoveType>(i);
    if (layers[i].quarter) {
      out.push_back(Move{t, 1, 0});
    }
    if (layers[i].half) {
      out.push_back(Move{t, 2, 0});
    }
    if (layers[i].quarter) {
      out.push_back(Move{t, 3, 0});
    }
  }
  return out;
}

bool MoveSet::contains(const Alg &alg, bool rotations) const {
  for (Move m : alg) {
    if (isRotation(m.type)) {
      if (!rotations) {
        return false;
      }
      continue;
    }
    const LayerTurns &l = layers[static_cast<std::size_t>(m.type)];
    if (!(m.amount == 2 ? l.half : l.quarter)) {
      return false;
    }
  }
  return true;
}

std::string MoveSet::toString() const {
  std::string out;
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (!layers[i].any()) {
      continue;
    }
    if (!out.empty()) {
      out += ", ";
    }
    out += kLayerNames[i];
    if (!layers[i].quarter) {
      out += '2';
    } else if (!layers[i].half) {
      out += '1';
    }
  }
  return out;
}

std::string MoveSet::display() const { return "<" + toString() + ">"; }

std::optional<MoveSet> MoveSet::parse(std::string_view text) {
  MoveSet s;
  std::size_t i = 0;
  bool any = false;
  auto skip = [&] {
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == ',' || text[i] == '<' ||
            text[i] == '>' || text[i] == '\t')) {
      ++i;
    }
  };
  skip();
  while (i < text.size()) {
    std::size_t start = i;
    while (i < text.size() &&
           std::isalpha(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    auto type = layerOf(text.substr(start, i - start));
    if (!type) {
      return std::nullopt;
    }
    LayerTurns turns = kBoth;
    if (i < text.size() && text[i] == '2') {
      turns = {false, true};
      ++i;
    } else if (i < text.size() && text[i] == '1') {
      turns = {true, false};
      ++i;
    }
    if (i < text.size() && text[i] != ' ' && text[i] != ',' && text[i] != '>' &&
        text[i] != '\t') {
      return std::nullopt;
    }
    auto &l = s.layers[static_cast<std::size_t>(*type)];
    l.quarter = l.quarter || turns.quarter;
    l.half = l.half || turns.half;
    any = true;
    skip();
  }
  if (!any) {
    return std::nullopt;
  }
  return s;
}

const std::vector<MoveSetPreset> &builtinMoveSets() {
  static const std::vector<MoveSetPreset> presets = [] {
    std::vector<MoveSetPreset> out;
    for (const char *text :
         {"U R", "U R F", "U R D", "U R L", "U R F D", "U R D F2", "U R r",
          "U R M", "U R F r", "U R L F B", "U R F D L B"}) {
      MoveSet s = *MoveSet::parse(text);
      out.push_back({s.display(), s});
    }
    out.back().name = "All faces";
    return out;
  }();
  return presets;
}

MoveCounts moveCounts(const Alg &alg) {
  MoveCounts c;
  for (Move m : alg) {
    ++c.etm;
    if (isRotation(m.type)) {
      continue;
    }
    bool slice =
        m.type == MoveType::M || m.type == MoveType::E || m.type == MoveType::S;
    int quarters = m.amount == 2 ? 2 : 1;
    c.htm += slice ? 2 : 1;
    c.stm += 1;
    c.qtm += slice ? 2 * quarters : quarters;
  }
  return c;
}

} // namespace vcube
