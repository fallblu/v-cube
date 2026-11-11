#include "train/ergonomics.hpp"

#include <array>

namespace vcube {

namespace {

// Costs of a layer's turns: clockwise, counter-clockwise, half.
struct Turns {
  const char *layer;
  double cw, ccw, half;
};

ErgoProfile makeProfile(std::string name, const std::vector<Turns> &table,
                        double other, double axis,
                        std::vector<ErgoRule> rules) {
  ErgoProfile p;
  p.name = std::move(name);
  for (const auto &t : table) {
    std::string l = t.layer;
    p.moves[l] = t.cw;
    p.moves[l + "'"] = t.ccw;
    p.moves[l + "2"] = t.half;
  }
  p.otherMove = other;
  p.axisChange = axis;
  p.rules = std::move(rules);
  return p;
}

} // namespace

const std::vector<ErgoProfile> &builtinErgoProfiles() {
  static const std::vector<ErgoProfile> profiles = {
      makeProfile("Right-handed 2H",
                  {
                      {"U", 1.0, 1.1, 1.6},
                      {"R", 1.0, 1.0, 1.5},
                      {"F", 1.5, 1.6, 2.3},
                      {"D", 1.7, 1.8, 2.5},
                      {"L", 1.4, 1.4, 2.0},
                      {"B", 2.6, 2.6, 3.4},
                      {"u", 2.2, 2.2, 3.0},
                      {"r", 1.2, 1.2, 1.8},
                      {"f", 2.0, 2.0, 2.8},
                      {"d", 2.4, 2.4, 3.2},
                      {"l", 1.6, 1.6, 2.3},
                      {"b", 3.0, 3.0, 3.8},
                      {"M", 1.6, 1.4, 2.0},
                      {"E", 2.6, 2.6, 3.4},
                      {"S", 2.6, 2.6, 3.4},
                      {"x", 1.8, 1.8, 2.6},
                      {"y", 1.5, 1.5, 2.2},
                      {"z", 2.2, 2.2, 3.0},
                  },
                  2.5, 0.15,
                  {
                      // After R2 or R the right hand is off its home grip.
                      {"R2", "F*", 0.6},
                      {"R2", "B*", 0.6},
                      {"R", "F*", 0.4},
                      {"R2", "D*", 0.3},
                      // F after B or the other way needs both hands moved.
                      {"F*", "B*", 0.8},
                      {"B*", "F*", 0.8},
                      // L after R2 / R2 after L: a regrip of the left hand.
                      {"L2", "R*", 0.3},
                      {"R2", "L*", 0.3},
                  }),
      makeProfile("One-handed (right)",
                  {
                      {"U", 1.0, 1.0, 1.8},
                      {"R", 1.0, 1.0, 1.6},
                      {"F", 2.0, 2.2, 3.4},
                      {"D", 1.8, 1.8, 3.0},
                      {"L", 4.0, 4.0, 5.5},
                      {"B", 3.5, 3.5, 5.0},
                      {"u", 3.5, 3.5, 5.0},
                      {"r", 1.6, 1.6, 2.6},
                      {"f", 3.0, 3.0, 4.2},
                      {"d", 3.5, 3.5, 5.0},
                      {"l", 4.0, 4.0, 5.5},
                      {"b", 4.5, 4.5, 6.0},
                      {"M", 3.0, 3.0, 4.5},
                      {"E", 4.0, 4.0, 5.5},
                      {"S", 4.0, 4.0, 5.5},
                      {"x", 1.6, 1.6, 2.6},
                      {"y", 1.2, 1.2, 2.0},
                      {"z", 1.8, 1.8, 3.0},
                  },
                  3.5, 0.3,
                  {
                      {"R2", "F*", 0.8},
                      {"F*", "R2", 0.5},
                  }),
  };
  return profiles;
}

bool ergoMatches(std::string_view pattern, Move m) {
  if (pattern == "*") {
    return true;
  }
  if (pattern.size() >= 2 && pattern.back() == '*') {
    return moveTypeName(m.type) == pattern.substr(0, pattern.size() - 1);
  }
  return pattern == toString(m);
}

ErgoScore ergoScore(const Alg &alg, const ErgoProfile &profile) {
  ErgoScore s;
  int lastAxis = -1;
  const Move *prev = nullptr;
  for (const Move &m : alg) {
    std::string written = toString(m);
    auto it = profile.moves.find(written);
    s.moves += it == profile.moves.end() ? profile.otherMove : it->second;
    if (!isRotation(m.type)) {
      int axis = moveAxis(m.type);
      if (lastAxis >= 0 && axis != lastAxis) {
        s.transitions += profile.axisChange;
      }
      lastAxis = axis;
    }
    if (prev) {
      for (const auto &r : profile.rules) {
        if (ergoMatches(r.first, *prev) && ergoMatches(r.second, m)) {
          s.transitions += r.cost;
        }
      }
    }
    prev = &m;
  }
  s.total = s.moves + s.transitions;
  return s;
}

const std::vector<std::string> &ergoMoveTokens() {
  static const std::vector<std::string> tokens = [] {
    std::vector<std::string> out;
    for (const char *l : {"U", "R", "F", "D", "L", "B", "u", "r", "f", "d", "l",
                          "b", "M", "E", "S", "x", "y", "z"}) {
      std::string s = l;
      out.push_back(s);
      out.push_back(s + "'");
      out.push_back(s + "2");
    }
    return out;
  }();
  return tokens;
}

} // namespace vcube
