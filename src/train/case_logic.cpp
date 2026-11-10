#include "train/case_logic.hpp"

#include "train/reduction.hpp"

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

// A top-level token of an alg's text: one move, or a bracketed group with
// its repetition suffix, as [begin, end) offsets.
struct Token {
  std::size_t begin = 0;
  std::size_t end = 0;
};

struct Tokens {
  std::vector<Token> list;
  bool complete = true; // false if the text has a part that can't be split,
                        // such as a commutator, after the listed tokens
};

Tokens topLevelTokens(std::string_view text) {
  Tokens out;
  std::size_t pos = 0;
  for (;;) {
    while (pos < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[pos])) ||
            text[pos] == '.' || text[pos] == '*')) {
      ++pos;
    }
    if (pos >= text.size()) {
      return out;
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
    if (end == pos) {
      out.complete = false;
      return out;
    }
    out.list.push_back({pos, end});
    pos = end;
  }
}

// Quarter turns of a token made of U turns only, e.g. "(U2)" or "U'".
std::optional<int> uTurnQuarters(std::string_view token) {
  ParseResult parsed = parseAlg(token);
  if (!parsed || parsed.alg.empty()) {
    return std::nullopt;
  }
  int quarters = 0;
  for (const Move &m : parsed.alg) {
    if (m.type != MoveType::U) {
      return std::nullopt;
    }
    quarters += m.amount;
  }
  return quarters;
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

StepMiss stepMiss(const CubieCube &cube, const PieceMask &free) {
  StepMiss miss;
  for (int v : {1, 3, 2}) {
    if (withinMask(cube * uTurn(v), free)) {
      miss.auf = v;
      break;
    }
  }
  for (std::size_t i = 0; i < kCorners; ++i) {
    miss.pieces +=
        (!free.cp[i] && cube.cp[i] != i) || (!free.co[i] && cube.co[i] != 0)
            ? 1
            : 0;
  }
  for (std::size_t i = 0; i < kEdges; ++i) {
    miss.pieces +=
        (!free.ep[i] && cube.ep[i] != i) || (!free.eo[i] && cube.eo[i] != 0)
            ? 1
            : 0;
  }
  return miss;
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
  std::string_view text = alg;
  Tokens tokens = topLevelTokens(text);
  const std::size_t count = tokens.list.size();
  // U turns written at either end of the alg join the AUF on that side,
  // so that "(U2) R U R'" needing a U first reads "(U') R U R'".
  int before = fit.before;
  int after = fit.after;
  std::size_t first = 0; // tokens [first, last) are kept
  std::size_t last = count;
  auto quarters = [&](std::size_t i) {
    const Token &t = tokens.list[i];
    return uTurnQuarters(text.substr(t.begin, t.end - t.begin));
  };
  if (fit.before % 4 != 0) {
    while (first < last) {
      auto q = quarters(first);
      if (!q) {
        break;
      }
      before += *q;
      ++first;
    }
  }
  if (fit.after % 4 != 0 && tokens.complete) {
    while (last > first) {
      auto q = quarters(last - 1);
      if (!q) {
        break;
      }
      after += *q;
      --last;
    }
  }
  std::size_t begin = first == 0 ? 0 : tokens.list[first - 1].end;
  std::size_t end = last == count ? text.size() : tokens.list[last].begin;
  std::string body = trim(text.substr(begin, end - begin));
  if (body.empty()) {
    // Nothing but U turns: keep the alg as written.
    body = trim(text);
    before = fit.before;
    after = fit.after;
  }
  std::string out;
  if (!aufText(before).empty()) {
    out += "(" + aufText(before) + ") ";
  }
  out += body;
  if (!aufText(after).empty()) {
    out += " (" + aufText(after) + ")";
  }
  return out;
}

std::string withAufBefore(const std::string &alg, int before) {
  std::string_view text = alg;
  Tokens tokens = topLevelTokens(text);
  int total = before;
  std::size_t first = 0;
  for (; first < tokens.list.size(); ++first) {
    const Token &t = tokens.list[first];
    auto q = uTurnQuarters(text.substr(t.begin, t.end - t.begin));
    if (!q) {
      break;
    }
    total += *q;
  }
  std::string body =
      trim(text.substr(first == 0 ? 0 : tokens.list[first - 1].end));
  if (body.empty()) {
    return withAufs(alg, AufFit{before, 0}); // nothing but U turns
  }
  std::string auf = aufText(total);
  if (auf.empty()) {
    return body;
  }
  return (body.front() == '(' ? auf : "(" + auf + ")") + " " + body;
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
  if (!check.reducesTo.empty()) {
    return "→ " + check.reducesToName + ", " + moves;
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
  for (const Token &t : topLevelTokens(text).list) {
    if (count == 0) {
      break;
    }
    ParseResult token = parseAlg(text.substr(t.begin, t.end - t.begin));
    if (!token || token.alg.empty() || token.alg.size() > count) {
      return std::nullopt;
    }
    count -= token.alg.size();
    pos = t.end;
  }
  if (count > 0) {
    return std::nullopt;
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

AlgCheck checkCaseAlg(const AlgSet &set, const CaseGroup &group,
                      const AlgCase &c, std::string_view alg) {
  AlgCheck check = checkAlg(c.state, alg, set.specFor(group).free,
                            set.aufBefore, set.aufAfter);
  if (set.reduction && !check.passes() && !check.parseError && !check.empty) {
    // A step: done as written, it stops at another case of the set.
    FaceletCube f = c.state.toFacelets();
    f.apply(parseAlgOrEmpty(alg));
    f.normalizeOrientation();
    auto after = CubieCube::fromFacelets(f);
    auto seen = after ? seenCase(set, *after) : std::nullopt;
    const AlgCase *next = seen ? set.findCase(seen->caseId) : nullptr;
    if (next && next->id != c.id) {
      check.fit = AufFit{};
      check.reducesTo = next->id;
      check.reducesToName = next->name;
    }
  }
  return check;
}

CaseAnalysis analyzeCase(const AlgSet &set, const CaseGroup &group,
                         const AlgCase &c) {
  CaseAnalysis a;
  const PieceMask &free = set.specFor(group).free;
  for (std::size_t i = 0; i < c.algs.size(); ++i) {
    a.checks.push_back(checkCaseAlg(set, group, c, c.algs[i]));
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

namespace {

std::vector<CaseRef> casesSolvedBy(const AlgSet &set, const Alg &alg,
                                   bool firstOnly) {
  std::vector<CaseRef> out;
  for (std::size_t g = 0; g < set.groups.size(); ++g) {
    const auto &group = set.groups[g];
    const PieceMask &free = set.specFor(group).free;
    for (std::size_t i = 0; i < group.cases.size(); ++i) {
      if (fitAlg(group.cases[i].state, alg, free, set.aufBefore,
                 set.aufAfter)) {
        out.push_back(CaseRef{g, i});
        if (firstOnly) {
          return out;
        }
      }
    }
  }
  return out;
}

} // namespace

std::optional<CaseRef> matchCase(const AlgSet &set, const Alg &alg) {
  auto found = casesSolvedBy(set, alg, true);
  return found.empty() ? std::nullopt : std::optional(found.front());
}

std::vector<CaseRef> matchCases(const AlgSet &set, const Alg &alg) {
  return casesSolvedBy(set, alg, false);
}

CubieCube withRandomAufs(const AlgSet &set, const CubieCube &state, Rng &rng) {
  std::uniform_int_distribution<int> quarter(0, 3);
  CubieCube s = state;
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
  return CubieCube::fromRealFacelets(f);
}

namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char &c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// The cases of `set` that a pasted line names, by name or else by id,
// ignoring case. Several cases may share a name, e.g. in groups for
// different slots.
std::vector<AlgCase *> namedCases(AlgSet &set, const std::string &name) {
  std::vector<AlgCase *> out;
  if (name.empty()) {
    return out;
  }
  std::string want = lower(name);
  for (auto &g : set.groups) {
    for (auto &c : g.cases) {
      if (lower(c.name) == want) {
        out.push_back(&c);
      }
    }
  }
  for (auto &g : set.groups) {
    for (auto &c : g.cases) {
      if (out.empty() && lower(c.id) == want) {
        out.push_back(&c);
      }
    }
  }
  return out;
}

std::string namesOf(const AlgSet &set, const std::vector<std::string> &ids) {
  std::string out;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const AlgCase *c = set.findCase(ids[i]);
    out += i == 0 ? "" : i + 1 == ids.size() ? " and " : ", ";
    out += c ? c->name : ids[i];
  }
  return out;
}

std::string unquoted(std::string s) {
  while (!s.empty() && (s.back() == ',' || s.back() == '"')) {
    s.pop_back();
  }
  while (!s.empty() && s.front() == '"') {
    s.erase(s.begin());
  }
  return s;
}

void addPiecesOf(std::vector<PasteItem> &out, int line, const std::string &name,
                 const std::string &text) {
  for (const std::string &piece : split(text, '/')) {
    std::string alg = cleanAlgText(piece);
    if (!alg.empty()) {
      out.push_back({line, name, alg, {}});
    }
  }
}

} // namespace

std::vector<PasteItem> readPaste(const std::string &text) {
  std::vector<PasteItem> out;
  std::istringstream in(text);
  std::string rawLine;
  int number = 0;
  while (std::getline(in, rawLine)) {
    ++number;
    // A "//" comment would otherwise be split at its slashes like algs.
    std::string raw = rawLine.substr(0, rawLine.find("//"));
    if (raw.find('\t') != std::string::npos) {
      // Spreadsheet columns: the case name, then its algs.
      std::vector<std::string> columns;
      for (const std::string &column : split(raw, '\t')) {
        std::string c = unquoted(cleanAlgText(column));
        if (!c.empty()) {
          columns.push_back(c);
        }
      }
      if (columns.size() >= 2 && !columns.front().starts_with("#")) {
        std::string name = columns.front();
        if (name.ends_with(':')) {
          name = trim(std::string_view(name).substr(0, name.size() - 1));
        }
        for (std::size_t i = 1; i < columns.size(); ++i) {
          addPiecesOf(out, number, name, columns[i]);
        }
        continue;
      }
    }
    std::string line = cleanAlgText(raw);
    if (line.empty() || line.starts_with("#")) {
      continue;
    }
    // Drop list numbering such as "12." or "3)".
    std::size_t numerals = 0;
    while (numerals < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[numerals]))) {
      ++numerals;
    }
    if (numerals > 0 && numerals < line.size() &&
        (line[numerals] == '.' || line[numerals] == ')')) {
      line = trim(std::string_view(line).substr(numerals + 1));
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
    addPiecesOf(out, number, name, unquoted(line));
  }
  return out;
}

PastePlan planPaste(const AlgSet &set, const std::vector<PasteItem> &items,
                    bool createCases, std::size_t newCaseGroup) {
  PastePlan plan;
  plan.set = set;
  AlgSet &work = plan.set;
  ImportReport &report = plan.report;
  int newCaseNumber = work.caseCount() + 1;
  auto file = [&](AlgCase &c, const std::string &alg, PasteResult &r) {
    r.caseId = c.id;
    if (std::find(c.algs.begin(), c.algs.end(), alg) != c.algs.end()) {
      r.outcome = PasteOutcome::Duplicate;
      r.reason = "already in " + c.name;
      ++report.duplicates;
    } else {
      r.outcome = PasteOutcome::Added;
      r.reason = c.name;
      c.algs.push_back(alg);
      ++report.added;
    }
  };
  auto checkIn = [&work](const AlgCase &c, const std::string &alg) {
    return checkCaseAlg(work, *work.groupOf(c.id), c, alg);
  };
  for (const PasteItem &item : items) {
    PasteResult &r = plan.results.emplace_back();
    ParseResult parsed = parseAlg(item.alg);
    if (!parsed || parsed.alg.empty()) {
      r.outcome = PasteOutcome::Invalid;
      r.reason = parsed.error ? "can't read at character " +
                                    std::to_string(parsed.error->position + 1)
                              : "no moves";
      report.invalid.push_back(item.alg);
      continue;
    }
    if (!item.caseId.empty()) {
      if (AlgCase *c = work.findCase(item.caseId)) {
        AlgCheck check = checkIn(*c, item.alg);
        file(*c, item.alg, r);
        if (!check.passes() && r.outcome == PasteOutcome::Added) {
          r.reason += ", but it " + describeCheck(check);
        }
        continue;
      }
    }
    auto solvedCases = [&] {
      std::vector<std::string> ids;
      for (const CaseRef &ref : matchCases(work, parsed.alg)) {
        ids.push_back(work.groups[ref.group].cases[ref.index].id);
      }
      return ids;
    };
    if (std::vector<AlgCase *> named = namedCases(work, item.name);
        !named.empty()) {
      auto passing = std::find_if(named.begin(), named.end(), [&](AlgCase *c) {
        return checkIn(*c, item.alg).passes();
      });
      if (passing != named.end()) {
        file(**passing, item.alg, r);
        continue;
      }
      std::vector<std::string> solved = solvedCases();
      for (const AlgCase *c : named) {
        std::erase(solved, c->id);
      }
      r.candidates = solved;
      if (!solved.empty()) {
        r.outcome = PasteOutcome::Mismatch;
        r.reason = "named " + named.front()->name + " but solves " +
                   namesOf(work, solved);
        report.ambiguous.push_back(item.alg);
      } else {
        r.outcome = PasteOutcome::Unmatched;
        r.reason = "doesn't solve " + named.front()->name + ": it " +
                   describeCheck(checkIn(*named.front(), item.alg));
        report.unmatched.push_back(item.alg);
      }
      continue;
    }
    std::vector<std::string> solved = solvedCases();
    if (solved.size() == 1) {
      file(*work.findCase(solved.front()), item.alg, r);
      continue;
    }
    if (solved.size() > 1) {
      r.outcome = PasteOutcome::Ambiguous;
      r.candidates = solved;
      r.reason = "solves " + namesOf(work, solved);
      report.ambiguous.push_back(item.alg);
      continue;
    }
    if (!createCases || work.reduction) {
      r.outcome = PasteOutcome::Unmatched;
      r.reason = work.reduction && createCases
                     ? "solves no case (a set solved a step at a time gets "
                       "no new cases from pasted algs)"
                     : "solves no case";
      report.unmatched.push_back(item.alg);
      continue;
    }
    if (work.groups.empty()) {
      work.groups.push_back({"Cases", std::nullopt, {}});
    }
    CaseGroup &group =
        work.groups[std::min(newCaseGroup, work.groups.size() - 1)];
    AlgCase c;
    c.name =
        item.name.empty() ? "Case " + std::to_string(newCaseNumber) : item.name;
    c.id = work.uniqueCaseId(item.name.empty() ? std::to_string(newCaseNumber)
                                               : item.name);
    ++newCaseNumber;
    c.state = stateSolvedBy(parsed.alg);
    c.algs.push_back(item.alg);
    r.outcome = PasteOutcome::NewCase;
    r.caseId = c.id;
    r.reason = "new case " + c.name + " in " + group.name;
    group.cases.push_back(std::move(c));
    ++report.created;
  }
  return plan;
}

ImportReport importAlgs(AlgSet &set, const std::string &text,
                        bool createCases) {
  PastePlan plan = planPaste(set, readPaste(text), createCases,
                             set.groups.empty() ? 0 : set.groups.size() - 1);
  set = std::move(plan.set);
  return plan.report;
}

} // namespace vcube
