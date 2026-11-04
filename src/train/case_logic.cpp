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
                             const PieceMask &free) {
  if (alg.empty()) {
    return std::nullopt;
  }
  FaceletCube start = state.toFacelets();
  for (int u : kAufOrder) {
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
  case 1: return "U";
  case 2: return "U2";
  case 3: return "U'";
  default: return "";
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

CubieCube stateSolvedBy(const Alg &alg) { return cubieOf(invert(alg)); }

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
      c.id = set.uniqueCaseId(name.empty() ? std::to_string(newCaseNumber) : name);
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
