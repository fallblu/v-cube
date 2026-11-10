#include "cube/move.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <span>

namespace vcube {

namespace {

constexpr std::array<std::string_view, kMoveTypeCount> kNames = {
    "U", "R", "F", "D", "L", "B", "u", "r", "f",
    "d", "l", "b", "M", "E", "S", "x", "y", "z"};

constexpr std::array<int, kMoveTypeCount> kAxis = {1, 0, 2, 1, 0, 2, 1, 0, 2,
                                                   1, 0, 2, 0, 1, 2, 0, 1, 2};

std::uint8_t negate(std::uint8_t amount) {
  return static_cast<std::uint8_t>((4 - amount) % 4);
}

// Limits that keep a typo such as "((R U)1000)1000" from using up memory or
// the stack.
constexpr std::size_t kMaxMoves = 10000;
constexpr int kMaxDepth = 64;
constexpr int kMaxCount = 1000;

// UTF-8 spellings of the spaces and prime marks that text copied from web
// pages and documents uses: no-break and thin spaces, zero-width spaces and
// the byte order mark; curly quotes, acute accents and other primes.
constexpr std::array<std::string_view, 17> kSpaces = {
    "\xC2\xA0",     "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82",
    "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85", "\xE2\x80\x86",
    "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A",
    "\xE2\x80\x8B", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80",
    "\xEF\xBB\xBF"};
constexpr std::array<std::string_view, 7> kPrimes = {
    "\xE2\x80\x99", // right single quotation mark
    "\xE2\x80\x98", // left single quotation mark
    "\xE2\x80\xB2", // prime
    "\xC2\xB4",     // acute accent
    "\xCA\xB9",     // modifier letter prime
    "\xCA\xBC",     // modifier letter apostrophe
    "\xEF\xBC\x87", // fullwidth apostrophe
};
constexpr std::string_view kTimes = "\xC3\x97"; // multiplication sign

// Bytes in the UTF-8 sequence that starts with `lead`.
std::size_t utf8Length(unsigned char lead) {
  return lead < 0xC0 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  ParseResult run() {
    ParseResult result;
    if (!parseSequence(result.alg, "")) {
      result.error = error_;
      result.alg.clear();
    }
    return result;
  }

private:
  std::string_view text_;
  std::size_t pos_ = 0;
  int depth_ = 0;
  ParseError error_;

  // Fails with `message` at `at` (a byte offset), reported in characters.
  bool fail(std::string message, std::optional<std::size_t> at = {}) {
    std::size_t byte = std::min(at.value_or(pos_), text_.size());
    std::size_t chars = 0;
    for (std::size_t i = 0; i < byte; ++i) {
      chars += (static_cast<unsigned char>(text_[i]) & 0xC0) != 0x80 ? 1u : 0u;
    }
    error_ = {std::move(message), chars};
    return false;
  }

  bool atEnd() const { return pos_ >= text_.size(); }
  char peek() const { return atEnd() ? '\0' : text_[pos_]; }
  bool isDigitAt(std::size_t i) const {
    return i < text_.size() &&
           std::isdigit(static_cast<unsigned char>(text_[i]));
  }

  // Skips over `token` if the text continues with it.
  bool skip(std::string_view token) {
    if (text_.substr(pos_, token.size()) == token) {
      pos_ += token.size();
      return true;
    }
    return false;
  }

  bool skipAny(std::span<const std::string_view> tokens) {
    return std::any_of(tokens.begin(), tokens.end(),
                       [this](std::string_view t) { return skip(t); });
  }

  // Skips whitespace, ignorable punctuation and // comments.
  void skipSpace() {
    while (!atEnd()) {
      char c = text_[pos_];
      if (std::isspace(static_cast<unsigned char>(c)) || c == '.' || c == '*') {
        ++pos_;
      } else if (c == '/' && pos_ + 1 < text_.size() &&
                 text_[pos_ + 1] == '/') {
        while (!atEnd() && text_[pos_] != '\n') {
          ++pos_;
        }
      } else if (!skipAny(kSpaces)) {
        return;
      }
    }
  }

  // Reads a prime mark (ASCII or Unicode) if present.
  bool readPrime() {
    if (peek() == '\'' || peek() == '`') {
      ++pos_;
      return true;
    }
    return skipAny(kPrimes);
  }

  // Optional repetition count and prime after a move or group, as the
  // signed count (2' and '2 give -2), or nullopt after an error.
  std::optional<int> readAmount() {
    bool prime = readPrime();
    std::size_t start = pos_;
    int count = 0;
    bool any = false;
    while (isDigitAt(pos_)) {
      count = count * 10 + (peek() - '0');
      any = true;
      ++pos_;
      if (count > kMaxCount) {
        fail("a count can be at most " + std::to_string(kMaxCount), start);
        return std::nullopt;
      }
    }
    if (!any) {
      count = 1;
    }
    if (!prime && readPrime()) {
      prime = true;
    }
    return prime ? -count : count;
  }

  // The repetition after a group: as after a move, or written x2, *2 or
  // ×2 (x straight after the bracket, so that "(R U) x2" stays a rotation).
  std::optional<int> readGroupAmount() {
    std::size_t start = pos_;
    if ((peek() == 'x' || peek() == 'X') && isDigitAt(pos_ + 1)) {
      ++pos_;
      return readAmount();
    }
    while (peek() == ' ') {
      ++pos_;
    }
    bool star = peek() == '*';
    if (star || skip(kTimes)) {
      pos_ += star ? 1 : 0;
      while (peek() == ' ') {
        ++pos_;
      }
      if (isDigitAt(pos_)) {
        return readAmount();
      }
    }
    pos_ = start; // a lone '*' is ignored, as anywhere else
    return readAmount();
  }

  bool append(Alg &out, const Alg &body, int count) {
    std::size_t times = static_cast<std::size_t>(count < 0 ? -count : count);
    if (out.size() + body.size() * times > kMaxMoves) {
      return fail("the alg is longer than " + std::to_string(kMaxMoves) +
                  " moves");
    }
    const Alg &part = count < 0 ? invert(body) : body;
    for (std::size_t i = 0; i < times; ++i) {
      out.insert(out.end(), part.begin(), part.end());
    }
    return true;
  }

  bool parseMove(Alg &out) {
    char c = peek();
    std::optional<MoveType> type;
    bool wideByCase = false;
    switch (c) {
    case 'U':
      type = MoveType::U;
      break;
    case 'R':
      type = MoveType::R;
      break;
    case 'F':
      type = MoveType::F;
      break;
    case 'D':
      type = MoveType::D;
      break;
    case 'L':
      type = MoveType::L;
      break;
    case 'B':
      type = MoveType::B;
      break;
    case 'u':
      type = MoveType::Uw;
      wideByCase = true;
      break;
    case 'r':
      type = MoveType::Rw;
      wideByCase = true;
      break;
    case 'f':
      type = MoveType::Fw;
      wideByCase = true;
      break;
    case 'd':
      type = MoveType::Dw;
      wideByCase = true;
      break;
    case 'l':
      type = MoveType::Lw;
      wideByCase = true;
      break;
    case 'b':
      type = MoveType::Bw;
      wideByCase = true;
      break;
    case 'M':
      type = MoveType::M;
      break;
    case 'E':
      type = MoveType::E;
      break;
    case 'S':
      type = MoveType::S;
      break;
    case 'x':
    case 'X':
      type = MoveType::X;
      break;
    case 'y':
    case 'Y':
      type = MoveType::Y;
      break;
    case 'z':
    case 'Z':
      type = MoveType::Z;
      break;
    default:
      return fail("unexpected character '" +
                  std::string(text_.substr(
                      pos_, utf8Length(static_cast<unsigned char>(c)))) +
                  "'");
    }
    ++pos_;
    if (!wideByCase && peek() == 'w' && isFaceTurn(*type)) {
      ++pos_;
      type = static_cast<MoveType>(static_cast<int>(*type) + 6);
    }
    std::optional<int> count = readAmount();
    if (!count) {
      return false;
    }
    int amount = ((*count % 4) + 4) % 4;
    if (amount != 0) {
      if (out.size() >= kMaxMoves) {
        return fail("the alg is longer than " + std::to_string(kMaxMoves) +
                    " moves");
      }
      // R3 keeps its three quarter turns; R' and R2' have the fewest.
      int quarters = std::abs(*count);
      int fewest = amount == 2 ? 2 : 1;
      out.push_back(
          {*type, static_cast<std::uint8_t>(amount),
           static_cast<std::uint16_t>(quarters > fewest ? quarters : 0)});
    }
    return true;
  }

  // Parses items until one of `closers` (not consumed) or, when `closers`
  // is empty, the end of input.
  bool parseSequence(Alg &out, std::string_view closers) {
    for (;;) {
      skipSpace();
      if (atEnd()) {
        if (!closers.empty()) {
          return fail(std::string("missing '") + closers.back() + "'");
        }
        return true;
      }
      char c = peek();
      if (closers.find(c) != std::string_view::npos) {
        return true;
      }
      if ((c == '(' || c == '[') && depth_ >= kMaxDepth) {
        return fail("brackets nested more than " + std::to_string(kMaxDepth) +
                    " deep");
      }
      if (c == '(') {
        ++pos_;
        Alg body;
        ++depth_;
        bool ok = parseSequence(body, ")");
        --depth_;
        if (!ok) {
          return false;
        }
        ++pos_; // ')'
        std::optional<int> count = readGroupAmount();
        if (!count || !append(out, body, *count)) {
          return false;
        }
      } else if (c == '[') {
        ++pos_;
        ++depth_;
        bool ok = parseBracket(out);
        --depth_;
        if (!ok) {
          return false;
        }
      } else if (c == ')' || c == ']' || c == ',' || c == ':') {
        return fail(std::string("unexpected '") + c + "'");
      } else if (!parseMove(out)) {
        return false;
      }
    }
  }

  // After '[': commutator [A, B] or conjugate [A: B], with optional amount.
  bool parseBracket(Alg &out) {
    Alg a;
    if (!parseSequence(a, ",:]")) {
      return false;
    }
    char sep = peek();
    if (sep == ']') {
      return fail("expected ',' or ':' in brackets");
    }
    ++pos_;
    Alg b;
    if (!parseSequence(b, "]")) {
      return false;
    }
    ++pos_; // ']'
    Alg body = a;
    body.insert(body.end(), b.begin(), b.end());
    Alg ai = invert(a);
    body.insert(body.end(), ai.begin(), ai.end());
    if (sep == ',') {
      Alg bi = invert(b);
      body.insert(body.end(), bi.begin(), bi.end());
    }
    std::optional<int> count = readGroupAmount();
    return count && append(out, body, *count);
  }
};

} // namespace

int moveAxis(MoveType t) { return kAxis[static_cast<std::size_t>(t)]; }

int quarterTurns(Move m) {
  if (m.quarters != 0) {
    return m.quarters;
  }
  return m.amount == 2 ? 2 : 1;
}

std::string_view moveTypeName(MoveType t) {
  return kNames[static_cast<std::size_t>(t)];
}

std::string toString(Move m) {
  std::string s(moveTypeName(m.type));
  if (m.amount == 2) {
    s += '2';
  } else if (m.amount == 3) {
    s += '\'';
  }
  return s;
}

std::string toString(const Alg &alg) {
  std::string s;
  for (const Move &m : alg) {
    if (!s.empty()) {
      s += ' ';
    }
    s += toString(m);
  }
  return s;
}

ParseResult parseAlg(std::string_view text) { return Parser(text).run(); }

std::string cleanAlgText(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    auto at = [&](std::string_view token) {
      return text.substr(i, token.size()) == token;
    };
    auto space = std::find_if(kSpaces.begin(), kSpaces.end(), at);
    auto prime = std::find_if(kPrimes.begin(), kPrimes.end(), at);
    if (space != kSpaces.end()) {
      out += ' ';
      i += space->size();
    } else if (prime != kPrimes.end()) {
      out += '\'';
      i += prime->size();
    } else {
      out += text[i++];
    }
  }
  auto b = out.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return {};
  }
  return out.substr(b, out.find_last_not_of(" \t\r\n") - b + 1);
}

Alg parseAlgOrEmpty(std::string_view text) {
  ParseResult r = parseAlg(text);
  return r ? std::move(r.alg) : Alg{};
}

Alg invert(const Alg &alg) {
  Alg out;
  out.reserve(alg.size());
  for (auto it = alg.rbegin(); it != alg.rend(); ++it) {
    out.push_back({it->type, negate(it->amount), it->quarters});
  }
  return out;
}

namespace {

// Mirror across a plane perpendicular to `axis`: layers on that axis swap
// sides but keep their absolute sense; every other layer reverses sense.
Alg mirror(const Alg &alg, int axis) {
  using T = MoveType;
  auto swapSide = [](T t) -> T {
    switch (t) {
    case T::R:
      return T::L;
    case T::L:
      return T::R;
    case T::Rw:
      return T::Lw;
    case T::Lw:
      return T::Rw;
    case T::F:
      return T::B;
    case T::B:
      return T::F;
    case T::Fw:
      return T::Bw;
    case T::Bw:
      return T::Fw;
    case T::U:
      return T::D;
    case T::D:
      return T::U;
    case T::Uw:
      return T::Dw;
    case T::Dw:
      return T::Uw;
    default:
      return t;
    }
  };
  Alg out;
  out.reserve(alg.size());
  for (Move m : alg) {
    if (moveAxis(m.type) == axis) {
      T swapped = swapSide(m.type);
      // A layer turning clockwise seen from one side turns counter-clockwise
      // seen from the other, so a side swap flips the written direction.
      // Slices and rotations keep their side and their absolute sense.
      std::uint8_t amount = swapped != m.type ? negate(m.amount) : m.amount;
      out.push_back({swapped, amount, m.quarters});
    } else {
      out.push_back({m.type, negate(m.amount), m.quarters});
    }
  }
  return out;
}

} // namespace

Alg mirrorM(const Alg &alg) { return mirror(alg, 0); }
Alg mirrorS(const Alg &alg) { return mirror(alg, 2); }

Alg simplify(const Alg &alg) {
  Alg out;
  for (Move m : alg) {
    bool merged = false;
    for (std::size_t i = out.size(); i-- > 0;) {
      if (moveAxis(out[i].type) != moveAxis(m.type)) {
        break;
      }
      if (out[i].type == m.type) {
        auto amount = static_cast<std::uint8_t>((out[i].amount + m.amount) % 4);
        if (amount == 0) {
          out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
          out[i] = {m.type, amount};
        }
        merged = true;
        break;
      }
    }
    if (!merged) {
      out.push_back(m);
    }
  }
  return out;
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

} // namespace vcube
