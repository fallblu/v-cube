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

// By enum value.
constexpr std::array<StepInfo, 9> kSteps = {{
    {SolveStep::EO, "eo", "EO"},
    {SolveStep::EOPlus1, "eo+1", "EO+1"},
    {SolveStep::EOPlus2, "eo+2", "EO+2"},
    {SolveStep::EOCross, "eocross", "EO Cross"},
    {SolveStep::ZZF2L, "zzf2l", "ZZ F2L"},
    {SolveStep::EOLine, "eoline", "EOLine"},
    {SolveStep::Cross, "cross", "Cross"},
    {SolveStep::XEOCross, "xeocross", "XEOCross"},
    {SolveStep::CFOPF2L, "cfopf2l", "CFOP F2L"},
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

int solvedSlots(const CubieCube &c) {
  int n = 0;
  n += cornerSolved(c, DFR) && edgeSolved(c, FR);
  n += cornerSolved(c, DLF) && edgeSolved(c, FL);
  n += cornerSolved(c, DBL) && edgeSolved(c, BL);
  n += cornerSolved(c, DRB) && edgeSolved(c, BR);
  return n;
}

// The step done with the cube held as it is (cross on D, EO for F/B).
bool reachedAsHeld(const CubieCube &c, SolveStep step) {
  // Kociemba's edge orientation is the ZZ one: F and B quarter turns flip
  // edges, the other turns don't.
  if (stepNeedsEo(step) &&
      std::any_of(c.eo.begin(), c.eo.end(), [](auto o) { return o != 0; })) {
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
  case SolveStep::EOLine:
    return edgeSolved(c, DF) && edgeSolved(c, DB);
  case SolveStep::EOCross:
  case SolveStep::Cross:
    return cross == 4;
  case SolveStep::XEOCross:
    return cross == 4 && solvedSlots(c) >= 1;
  case SolveStep::ZZF2L:
  case SolveStep::CFOPF2L:
    return cross == 4 && solvedSlots(c) == 4;
  }
  return false;
}

} // namespace

const std::vector<SolveStep> &solveSteps() {
  static const std::vector<SolveStep> steps = {
      SolveStep::EO,      SolveStep::EOPlus1,  SolveStep::EOPlus2,
      SolveStep::EOLine,  SolveStep::EOCross,  SolveStep::XEOCross,
      SolveStep::ZZF2L,   SolveStep::Cross,    SolveStep::CFOPF2L};
  return steps;
}

bool stepNeedsEo(SolveStep step) {
  return step != SolveStep::Cross && step != SolveStep::CFOPF2L;
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
  return step == SolveStep::ZZF2L     ? "eocross"
         : step == SolveStep::CFOPF2L ? "f2l"
                                      : "333";
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

std::vector<Hold> stepReachedHolds(const FaceletCube &cube, SolveStep step) {
  std::vector<Hold> out;
  for (Hold h : allHolds()) {
    if (auto c = heldCubie(cube, h); c && reachedAsHeld(*c, step)) {
      out.push_back(h);
    }
  }
  return out; // allHolds() is sorted
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

int solutionHtm(const Alg &solution, bool simplified) {
  return htmCount(simplified ? simplify(solution) : solution);
}

void VirtualAttempt::load(const Alg &scramble) {
  FaceletCube start;
  start.apply(scramble);
  load(start);
}

void VirtualAttempt::load(const FaceletCube &start) {
  start_ = start;
  restart();
}

void VirtualAttempt::restart() {
  cube_ = start_;
  moves_.clear();
  times_.clear();
  started_ = false;
  free_ = false;
}

bool VirtualAttempt::turn(Move m) { return turn(m, startMs_); }

bool VirtualAttempt::turn(Move m, std::int64_t nowMs) {
  if (!started_ && !free_ && !isRotation(m.type)) {
    return false;
  }
  cube_.apply(m);
  moves_.push_back(m);
  times_.push_back(started_ ? std::max<std::int64_t>(0, nowMs - startMs_) : 0);
  return true;
}

std::vector<std::int64_t> VirtualAttempt::solutionTimes() const {
  // Joined as solution() joins the moves.
  Alg out;
  std::vector<std::int64_t> times;
  for (std::size_t i = 0; i < moves_.size(); ++i) {
    Move m = moves_[i];
    std::int64_t t = i < times_.size() ? times_[i] : 0;
    if (!out.empty() && out.back().type == m.type) {
      int q = (out.back().amount + m.amount) % 4;
      if (q == 0) {
        out.pop_back();
        times.pop_back();
      } else {
        out.back() = Move{m.type, static_cast<std::uint8_t>(q), 0};
        times.back() = t;
      }
    } else {
      out.push_back(Move{m.type, m.amount, 0});
      times.push_back(t);
    }
  }
  return times;
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
