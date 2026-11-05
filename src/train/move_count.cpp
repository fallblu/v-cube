#include "train/move_count.hpp"

#include <algorithm>

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
  std::string moves = std::to_string(a.moves);
  if (!a.dnf) {
    return moves;
  }
  return a.moves > 0 ? "DNF(" + moves + ")" : "DNF";
}

} // namespace vcube
