#include "train/move_count.hpp"

#include "cube/cubie.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace vcube {

const std::vector<std::string> &moveCountScrambleTypes() {
  static const std::vector<std::string> types = {"333", "eoline", "eocross",
                                                 "f2l"};
  return types;
}

bool isMoveCountScrambleType(const std::string &id) {
  const auto &types = moveCountScrambleTypes();
  return std::find(types.begin(), types.end(), id) != types.end();
}

std::vector<Solve>
attemptsAsSolves(std::span<const MoveCountAttempt> attempts) {
  std::vector<Solve> out;
  out.reserve(attempts.size());
  for (const auto &a : attempts) {
    if (!a.counted) {
      continue;
    }
    Solve s;
    s.timeMs = static_cast<std::int64_t>(a.moves) * kMoveUnit;
    s.penalty = a.dnf ? Penalty::Dnf : Penalty::None;
    out.push_back(s);
  }
  return out;
}

std::string formatMoves(StatValue thousandths, int decimals) {
  if (!thousandths) {
    return "-";
  }
  if (*thousandths == kDnfTime) {
    return "DNF";
  }
  int d = std::clamp(decimals, 0, 2);
  std::int64_t unit = d == 2 ? 10 : d == 1 ? 100 : 1000; // thousandths
  std::int64_t v = (*thousandths + unit / 2) / unit;     // rounded
  std::int64_t perMove = kMoveUnit / unit;
  std::string text = std::to_string(v / perMove);
  if (d > 0) {
    std::string frac = std::to_string(v % perMove);
    text += "." + std::string(static_cast<std::size_t>(d) - frac.size(), '0') +
            frac;
  }
  return text;
}

std::string formatAttempt(const MoveCountAttempt &a) {
  if (!a.counted) {
    return "-";
  }
  std::string moves = std::to_string(a.moves);
  if (!a.dnf) {
    return moves;
  }
  return a.moves > 0 ? "DNF(" + moves + ")" : "DNF";
}

namespace {

struct StepInfo {
  SolveStep step;
  std::string_view id;
  std::string_view name;
};

constexpr std::array<StepInfo, 5> kSteps = {{
    {SolveStep::EO, "eo", "EO"},
    {SolveStep::EOPlus1, "eo+1", "EO+1"},
    {SolveStep::EOPlus2, "eo+2", "EO+2"},
    {SolveStep::EOCross, "eocross", "EO Cross"},
    {SolveStep::ZZF2L, "zzf2l", "ZZ F2L"},
}};

const StepInfo &info(SolveStep step) {
  return kSteps[static_cast<std::size_t>(step)];
}

// Lower case without spaces, dashes or underscores: "EO Cross" -> "eocross".
std::string squashed(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c != ' ' && c != '-' && c != '_') {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

// The cube as seen held so that its centres are in the standard places:
// each sticker takes the colour of the face whose centre has its colour.
std::optional<CubieCube> asHeld(const FaceletCube &f) {
  std::array<std::uint8_t, 6> faceOf{};
  for (int face = 0; face < 6; ++face) {
    std::uint8_t centre = f[face * 9 + 4];
    if (centre >= 6) {
      return std::nullopt;
    }
    faceOf[centre] = static_cast<std::uint8_t>(face);
  }
  FaceletCube::Stickers s{};
  for (int i = 0; i < kFaceletCount; ++i) {
    if (f[i] >= 6) {
      return std::nullopt;
    }
    s[static_cast<std::size_t>(i)] = faceOf[f[i]];
  }
  return CubieCube::fromFacelets(FaceletCube(s));
}

bool edgeSolved(const CubieCube &c, Edge e) {
  return c.ep[e] == e && c.eo[e] == 0;
}

bool cornerSolved(const CubieCube &c, Corner k) {
  return c.cp[k] == k && c.co[k] == 0;
}

// The step done with the cube held as it is (cross on D, EO for F/B).
bool reachedAsHeld(const CubieCube &c, SolveStep step) {
  // Kociemba's edge orientation is the ZZ one: F and B quarter turns flip
  // edges, the other turns don't.
  if (std::any_of(c.eo.begin(), c.eo.end(), [](auto o) { return o != 0; })) {
    return false;
  }
  int cross = 0;
  for (Edge e : {DF, DR, DB, DL}) {
    cross += edgeSolved(c, e) ? 1 : 0;
  }
  switch (step) {
  case SolveStep::EO:
    return true;
  case SolveStep::EOPlus1:
    return cross >= 1;
  case SolveStep::EOPlus2:
    return cross >= 2;
  case SolveStep::EOCross:
    return cross == 4;
  case SolveStep::ZZF2L:
    return cross == 4 && cornerSolved(c, DFR) && cornerSolved(c, DLF) &&
           cornerSolved(c, DBL) && cornerSolved(c, DRB) && edgeSolved(c, FR) &&
           edgeSolved(c, FL) && edgeSolved(c, BL) && edgeSolved(c, BR);
  }
  return false;
}

} // namespace

const std::vector<SolveStep> &solveSteps() {
  static const std::vector<SolveStep> steps = [] {
    std::vector<SolveStep> out;
    for (const auto &s : kSteps) {
      out.push_back(s.step);
    }
    return out;
  }();
  return steps;
}

std::string_view stepId(SolveStep step) { return info(step).id; }

std::string_view stepName(SolveStep step) { return info(step).name; }

std::optional<SolveStep> stepFromId(std::string_view id) {
  for (const auto &s : kSteps) {
    if (s.id == id) {
      return s.step;
    }
  }
  return std::nullopt;
}

std::optional<SolveStep> stepFromName(std::string_view name) {
  std::string key = squashed(name);
  for (const auto &s : kSteps) {
    if (squashed(s.name) == key) {
      return s.step;
    }
  }
  return std::nullopt;
}

std::string defaultScrambleType(SolveStep step) {
  return step == SolveStep::ZZF2L ? "eocross" : "333";
}

bool stepReached(const FaceletCube &cube, SolveStep step) {
  for (const Alg &rotation : allRotations()) {
    FaceletCube held = cube;
    held.apply(rotation);
    if (auto c = asHeld(held); c && reachedAsHeld(*c, step)) {
      return true;
    }
  }
  return false;
}

int htmCount(const Alg &alg) {
  int n = 0;
  for (Move m : alg) {
    if (m.type == MoveType::M || m.type == MoveType::E ||
        m.type == MoveType::S) {
      n += 2;
    } else if (!isRotation(m.type)) {
      n += 1;
    }
  }
  return n;
}

void VirtualAttempt::load(const Alg &scramble) {
  scramble_ = scramble;
  restart();
}

void VirtualAttempt::restart() {
  cube_ = FaceletCube();
  cube_.apply(scramble_);
  moves_.clear();
  started_ = false;
  free_ = false;
}

bool VirtualAttempt::turn(Move m) {
  if (!started_ && !free_ && !isRotation(m.type)) {
    return false;
  }
  cube_.apply(m);
  moves_.push_back(m);
  return true;
}

Alg VirtualAttempt::solution() const {
  // Only consecutive turns of the same layer are joined: simplify() would
  // also join U D U into U2 D, which is a different solution.
  Alg out;
  for (Move m : moves_) {
    if (!out.empty() && out.back().type == m.type) {
      int q = (out.back().amount + m.amount) % 4;
      if (q == 0) {
        out.pop_back();
      } else {
        out.back() = Move{m.type, static_cast<std::uint8_t>(q), 0};
      }
    } else {
      out.push_back(Move{m.type, m.amount, 0});
    }
  }
  return out;
}

} // namespace vcube
