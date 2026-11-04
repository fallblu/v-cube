#include "train/case_logic.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace vcube {

namespace {

constexpr int kAufOrder[4] = {0, 1, 3, 2};

std::string trim(std::string_view s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string_view::npos) {
    return {};
  }
  auto e = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(b, e - b + 1));
}

std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

} // namespace

std::optional<AufFit> fitAlg(const CubieCube &state, const Alg &alg,
                             const PieceMask &free, bool aufBefore,
                             bool aufAfter) {
  if (alg.empty()) {
    return std::nullopt;
  }
  FaceletCube start = state.toFacelets();
  for (int u : kAufOrder) {
    if (u != 0 && !aufBefore) {
      continue;
    }
    FaceletCube f = start;
    if (u != 0) {
      f.apply(Move{MoveType::U, static_cast<std::uint8_t>(u)});
    }
    f.apply(alg);
    f.normalizeOrientation();
    auto after = CubieCube::fromFacelets(f);
    if (!after) {
      continue;
    }
    for (int v : kAufOrder) {
      if (v != 0 && !aufAfter) {
        continue;
      }
      if (withinMask(*after * uTurn(v), free)) {
        return AufFit{u, v};
      }
    }
  }
  return std::nullopt;
}

bool stepSolved(const CubieCube &cube, const PieceMask &free) {
  for (int w = 0; w < 4; ++w) {
    CubieCube c = uTurn(w) * cube;
    for (int v = 0; v < 4; ++v) {
      if (withinMask(c * uTurn(v), free)) {
        return true;
      }
    }
  }
  return false;
}

std::string aufText(int quarters) {
  switch (((quarters % 4) + 4) % 4) {
  case 1:
    return "U";
  case 2:
    return "U2";
  case 3:
    return "U'";
  default:
    return "";
  }
}

std::string withAufs(const std::string &alg, AufFit fit) {
  std::string out;
  if (fit.before != 0) {
    out += "(" + aufText(fit.before) + ") ";
  }
  out += trim(alg);
  if (fit.after != 0) {
    out += " (" + aufText(fit.after) + ")";
  }
  return out;
}

int Unsolved::corners() const {
  int n = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    n += cornersMisplaced[i] + cornersTwisted[i];
  }
  return n;
}

int Unsolved::edges() const {
  int n = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    n += edgesMisplaced[i] + edgesFlipped[i];
  }
  return n;
}

Unsolved unsolvedPieces(const CubieCube &cube, const PieceMask &free) {
  Unsolved u;
  for (std::size_t i = 0; i < kCorners; ++i) {
    std::size_t layer = i < 4 ? 0 : 2;
    if (cube.cp[i] != i && !free.cp[i]) {
      ++u.cornersMisplaced[layer];
    } else if (cube.co[i] != 0 && !free.co[i]) {
      ++u.cornersTwisted[layer];
    }
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    std::size_t layer = i < 4 ? 0 : i < 8 ? 2 : 1;
    if (cube.ep[i] != i && !free.ep[i]) {
      ++u.edgesMisplaced[layer];
    } else if (cube.eo[i] != 0 && !free.eo[i]) {
      ++u.edgesFlipped[layer];
    }
  }
  return u;
}

std::string describeUnsolved(const Unsolved &u) {
  if (u.none()) {
    return "solves the step";
  }
  auto count = [](int n, const char *what) {
    return std::to_string(n) + " " + what + (n == 1 ? "" : "s");
  };
  int c = u.corners(), e = u.edges();
  bool onlyOrientation = true;
  for (std::size_t i = 0; i < 3; ++i) {
    onlyOrientation = onlyOrientation && u.cornersMisplaced[i] == 0 &&
                      u.edgesMisplaced[i] == 0;
  }
  std::string text = "leaves ";
  if (onlyOrientation) {
    if (c) {
      text += count(c, "corner") + " twisted";
    }
    if (c && e) {
      text += " and ";
    }
    if (e) {
      text += count(e, "edge") + " flipped";
    }
  } else {
    if (c) {
      text += count(c, "corner");
    }
    if (c && e) {
      text += " and ";
    }
    if (e) {
      text += count(e, "edge");
    }
    text += " unsolved";
  }
  static const char *names[] = {"U", "middle", "D"};
  std::vector<std::string> layers;
  for (std::size_t i = 0; i < 3; ++i) {
    if (u.cornersMisplaced[i] + u.cornersTwisted[i] + u.edgesMisplaced[i] +
            u.edgesFlipped[i] >
        0) {
      layers.push_back(names[i]);
    }
  }
  text += " (";
  for (std::size_t i = 0; i < layers.size(); ++i) {
    text += i == 0 ? "" : i + 1 == layers.size() ? " and " : ", ";
    text += layers[i];
  }
  text += layers.size() == 1 ? " layer)" : " layers)";
  return text;
}

AlgCheck checkAlg(const CubieCube &state, std::string_view alg,
                  const PieceMask &free, bool aufBefore, bool aufAfter) {
  AlgCheck r;
  ParseResult parsed = parseAlg(alg);
  if (!parsed) {
    r.parseError = parsed.error;
    return r;
  }
  if (parsed.alg.empty()) {
    r.empty = true;
    return r;
  }
  r.moves = moveCount(parsed.alg);
  r.fit = fitAlg(state, parsed.alg, free, aufBefore, aufAfter);
  if (r.fit) {
    return r;
  }
  // Describe the closest result over the AUFs the set allows.
  FaceletCube start = state.toFacelets();
  std::optional<Unsolved> best;
  for (int u = 0; u < (aufBefore ? 4 : 1); ++u) {
    FaceletCube f = start;
    if (u != 0) {
      f.apply(Move{MoveType::U, static_cast<std::uint8_t>(u)});
    }
    f.apply(parsed.alg);
    f.normalizeOrientation();
    auto after = CubieCube::fromFacelets(f);
    if (!after) {
      continue;
    }
    for (int v = 0; v < (aufAfter ? 4 : 1); ++v) {
      Unsolved left = unsolvedPieces(*after * uTurn(v), free);
      if (!best ||
          left.corners() + left.edges() < best->corners() + best->edges()) {
        best = left;
      }
    }
  }
  r.unsolved = best.value_or(Unsolved{});
  return r;
}

std::string describeCheck(const AlgCheck &check) {
  if (check.parseError) {
    return "can't read at character " +
           std::to_string(check.parseError->position + 1);
  }
  if (check.empty) {
    return "empty";
  }
  std::string moves =
      std::to_string(check.moves) + (check.moves == 1 ? " move" : " moves");
  if (!check.fit) {
    return describeUnsolved(check.unsolved);
  }
  if (check.fit->before == 0 && check.fit->after == 0) {
    return moves;
  }
  std::string shape;
  if (check.fit->before != 0) {
    shape += "(" + aufText(check.fit->before) + ") ";
  }
  shape += "…";
  if (check.fit->after != 0) {
    shape += " (" + aufText(check.fit->after) + ")";
  }
  return shape + ", " + moves;
}

namespace {

// The cube with its stickers recoloured so that its centres are in the
// standard places: the same pattern, seen as if it were held in the
// standard orientation.
std::optional<CubieCube> recolouredToCentres(const FaceletCube &f) {
  std::array<std::uint8_t, 6> colourToFace{};
  for (int face = 0; face < 6; ++face) {
    std::uint8_t centre = f[face * 9 + 4];
    if (centre >= 6) {
      return std::nullopt;
    }
    colourToFace[centre] = static_cast<std::uint8_t>(face);
  }
  FaceletCube out = f;
  for (int i = 0; i < kFaceletCount; ++i) {
    if (out[i] < 6) {
      out[i] = colourToFace[out[i]];
    }
  }
  return CubieCube::fromFacelets(out);
}

// `text` without its first `count` moves, keeping the rest as written, or
// nullopt if the moves can't be told apart in the text (e.g. inside a
// commutator).
std::optional<std::string> withoutLeadingMoves(std::string_view text,
                                               std::size_t count) {
  std::size_t pos = 0;
  while (count > 0) {
    while (pos < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[pos])) ||
            text[pos] == '.' || text[pos] == '*')) {
      ++pos;
    }
    if (pos >= text.size()) {
      return std::nullopt;
    }
    std::size_t end = pos;
    if (text[pos] == '(') {
      int depth = 0;
      for (; end < text.size(); ++end) {
        depth += text[end] == '(' ? 1 : text[end] == ')' ? -1 : 0;
        if (depth == 0) {
          ++end;
          break;
        }
      }
    }
    // The token (or group's repetition suffix) runs to the next space or
    // bracket.
    while (end < text.size() &&
           !std::isspace(static_cast<unsigned char>(text[end])) &&
           text[end] != '(' && text[end] != '[') {
      ++end;
    }
    ParseResult token = parseAlg(text.substr(pos, end - pos));
    if (!token || token.alg.empty() || token.alg.size() > count) {
      return std::nullopt;
    }
    count -= token.alg.size();
    pos = end;
  }
  return trim(text.substr(pos));
}

} // namespace

std::optional<DisplayOrientation>
displayOrientation(const CubieCube &state, const std::string &mainAlg,
                   const PieceMask &free, bool aufBefore, bool aufAfter) {
  ParseResult parsed = parseAlg(mainAlg);
  if (!parsed) {
    return std::nullopt;
  }
  auto fit = fitAlg(state, parsed.alg, free, aufBefore, aufAfter);
  if (!fit) {
    return std::nullopt;
  }
  // The prefix is the longest run of leading moves that only turns the case
  // about the U-D axis: rotations that end with U back on top (so y turns,
  // but not an x or z that tilts the last layer off the top) and, when the
  // set allows one, U turns done while the cube is held that way up.
  std::size_t prefix = 0;
  FaceletCube held;
  auto upright = [&held] { return held[4] == 0; }; // U centre on top
  for (std::size_t i = 0; i < parsed.alg.size(); ++i) {
    const Move &m = parsed.alg[i];
    if (isRotation(m.type)) {
      held.apply(m);
    } else if (!(aufBefore && m.type == MoveType::U && upright())) {
      break;
    }
    if (upright()) {
      prefix = i + 1;
    }
  }
  DisplayOrientation canonical{state, trim(mainAlg)};
  if (prefix == 0 && fit->before == 0) {
    return canonical;
  }
  Alg rest(parsed.alg.begin() + static_cast<std::ptrdiff_t>(prefix),
           parsed.alg.end());
  if (rest.empty()) {
    return canonical; // nothing but a setup: keep the case as it is
  }
  FaceletCube f = state.toFacelets();
  if (fit->before != 0) {
    f.apply(Move{MoveType::U, static_cast<std::uint8_t>(fit->before)});
  }
  f.apply(Alg(parsed.alg.begin(),
              parsed.alg.begin() + static_cast<std::ptrdiff_t>(prefix)));
  auto shown = recolouredToCentres(f);
  if (!shown) {
    return canonical;
  }
  std::string restText;
  if (auto text = withoutLeadingMoves(mainAlg, prefix);
      text && parseAlgOrEmpty(*text) == rest) {
    restText = *text;
  } else {
    restText = toString(rest);
  }
  // By construction the rest solves the case as shown without a U turn
  // first; fall back to the stored case if it somehow doesn't.
  auto check = fitAlg(*shown, rest, free, false, aufAfter);
  if (!check) {
    return canonical;
  }
  return DisplayOrientation{*shown, restText};
}

CaseAnalysis analyzeCase(const AlgSet &set, const CaseGroup &group,
                         const AlgCase &c) {
  CaseAnalysis a;
  const PieceMask &free = set.specFor(group).free;
  for (std::size_t i = 0; i < c.algs.size(); ++i) {
    a.checks.push_back(
        checkAlg(c.state, c.algs[i], free, set.aufBefore, set.aufAfter));
    if (a.checks.back().passes()) {
      a.passing.push_back(i);
    }
  }
  a.display = c.state;
  if (!a.passing.empty()) {
    a.main = a.passing.front();
    a.mainAlg = trim(c.algs[*a.main]);
    if (auto d = displayOrientation(c.state, c.algs[*a.main], free,
                                    set.aufBefore, set.aufAfter)) {
      a.display = d->state;
      a.mainAlg = d->alg;
    }
  }
  return a;
}

std::optional<CaseRef> matchCase(const AlgSet &set, const Alg &alg) {
  for (std::size_t g = 0; g < set.groups.size(); ++g) {
    const auto &group = set.groups[g];
    const PieceMask &free = set.specFor(group).free;
    for (std::size_t i = 0; i < group.cases.size(); ++i) {
      if (fitAlg(group.cases[i].state, alg, free)) {
        return CaseRef{g, i};
      }
    }
  }
  return std::nullopt;
}

CubieCube drillState(const AlgSet &set, const CaseGroup &group,
                     const AlgCase &c, Rng &rng) {
  std::uniform_int_distribution<int> quarter(0, 3);
  CubieCube rest = randomState(set.specFor(group).free, rng);
  CubieCube s = rest * c.state;
  if (set.aufBefore) {
    s = s * uTurn(quarter(rng));
  }
  if (set.aufAfter) {
    s = uTurn(quarter(rng)) * s;
  }
  return s;
}

CubieCube stateSolvedBy(const Alg &alg) {
  // Done from the case, the alg leaves the cube solved but turned by its
  // net rotation (e.g. "x R U R' ..." ends with x applied), so its inverse
  // has to start from a solved cube held that way.
  FaceletCube end;
  end.apply(alg);
  Alg back = end.normalizeOrientation();
  FaceletCube f;
  f.apply(invert(back));
  f.apply(invert(alg));
  f.normalizeOrientation(); // a no-op unless the alg is malformed
  return CubieCube::fromFacelets(f).value_or(CubieCube{});
}

ImportReport importAlgs(AlgSet &set, const std::string &text,
                        bool createCases) {
  ImportReport report;
  std::istringstream in(text);
  std::string rawLine;
  int newCaseNumber = set.caseCount() + 1;
  while (std::getline(in, rawLine)) {
    std::string line = trim(rawLine);
    if (line.empty() || line.starts_with("#")) {
      continue;
    }
    // Drop list numbering such as "12." or "3)".
    std::size_t digits = 0;
    while (digits < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[digits]))) {
      ++digits;
    }
    if (digits > 0 && digits < line.size() &&
        (line[digits] == '.' || line[digits] == ')')) {
      line = trim(std::string_view(line).substr(digits + 1));
    }
    // Optional "name:" prefix, but not a conjugate inside brackets.
    std::string name;
    auto colon = line.find(':');
    auto bracket = line.find('[');
    if (colon != std::string::npos &&
        (bracket == std::string::npos || colon < bracket)) {
      name = trim(std::string_view(line).substr(0, colon));
      line = trim(std::string_view(line).substr(colon + 1));
    }
    // Strip surrounding quotes and trailing commas from pasted lists.
    while (!line.empty() && (line.back() == ',' || line.back() == '"')) {
      line.pop_back();
    }
    while (!line.empty() && line.front() == '"') {
      line.erase(line.begin());
    }
    for (const std::string &piece : split(line, '/')) {
      std::string algText = trim(piece);
      if (algText.empty()) {
        continue;
      }
      ParseResult parsed = parseAlg(algText);
      if (!parsed || parsed.alg.empty()) {
        report.invalid.push_back(algText);
        continue;
      }
      if (auto ref = matchCase(set, parsed.alg)) {
        AlgCase &c = set.groups[ref->group].cases[ref->index];
        if (std::find(c.algs.begin(), c.algs.end(), algText) != c.algs.end()) {
          ++report.duplicates;
        } else {
          c.algs.push_back(algText);
          ++report.added;
        }
        continue;
      }
      if (!createCases) {
        report.unmatched.push_back(algText);
        continue;
      }
      if (set.groups.empty()) {
        set.groups.push_back({"Cases", std::nullopt, {}});
      }
      AlgCase c;
      c.name = name.empty() ? "Case " + std::to_string(newCaseNumber) : name;
      c.id =
          set.uniqueCaseId(name.empty() ? std::to_string(newCaseNumber) : name);
      ++newCaseNumber;
      c.state = stateSolvedBy(parsed.alg);
      c.algs.push_back(algText);
      set.groups.back().cases.push_back(std::move(c));
      ++report.created;
    }
  }
  return report;
}

} // namespace vcube
