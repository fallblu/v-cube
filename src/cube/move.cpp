#include "cube/move.hpp"

#include <array>
#include <cctype>

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
  ParseError error_;

  bool fail(std::string message) {
    error_ = {std::move(message), pos_};
    return false;
  }

  bool atEnd() const { return pos_ >= text_.size(); }
  char peek() const { return atEnd() ? '\0' : text_[pos_]; }

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
      } else {
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
    // U+2019 RIGHT SINGLE QUOTATION MARK and U+2032 PRIME in UTF-8.
    static constexpr std::array<std::string_view, 2> kPrimes = {"\xE2\x80\x99",
                                                                "\xE2\x80\xB2"};
    for (auto p : kPrimes) {
      if (text_.substr(pos_, p.size()) == p) {
        pos_ += p.size();
        return true;
      }
    }
    return false;
  }

  // Optional repetition count and prime after a move or group. Returns the
  // signed count (e.g. 2' -> -2).
  int readAmount() {
    int count = 0;
    bool any = false;
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
      count = count * 10 + (peek() - '0');
      any = true;
      ++pos_;
      if (count > 1000) {
        count = 1000;
      }
    }
    if (!any) {
      count = 1;
    }
    if (readPrime()) {
      count = -count;
    }
    return count;
  }

  static void appendRepeated(Alg &out, const Alg &body, int count) {
    if (count < 0) {
      Alg inv = invert(body);
      for (int i = 0; i < -count; ++i) {
        out.insert(out.end(), inv.begin(), inv.end());
      }
    } else {
      for (int i = 0; i < count; ++i) {
        out.insert(out.end(), body.begin(), body.end());
      }
    }
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
      return fail(std::string("unexpected character '") + c + "'");
    }
    ++pos_;
    if (!wideByCase && peek() == 'w' && isFaceTurn(*type)) {
      ++pos_;
      type = static_cast<MoveType>(static_cast<int>(*type) + 6);
    }
    int count = readAmount();
    int amount = ((count % 4) + 4) % 4;
    if (amount != 0) {
      out.push_back({*type, static_cast<std::uint8_t>(amount)});
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
      if (c == '(') {
        ++pos_;
        Alg body;
        if (!parseSequence(body, ")")) {
          return false;
        }
        ++pos_; // ')'
        appendRepeated(out, body, readAmount());
      } else if (c == '[') {
        ++pos_;
        if (!parseBracket(out)) {
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
    appendRepeated(out, body, readAmount());
    return true;
  }
};

} // namespace

int moveAxis(MoveType t) { return kAxis[static_cast<std::size_t>(t)]; }

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

Alg parseAlgOrEmpty(std::string_view text) {
  ParseResult r = parseAlg(text);
  return r ? std::move(r.alg) : Alg{};
}

Alg invert(const Alg &alg) {
  Alg out;
  out.reserve(alg.size());
  for (auto it = alg.rbegin(); it != alg.rend(); ++it) {
    out.push_back({it->type, negate(it->amount)});
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
      out.push_back({swapped, amount});
    } else {
      out.push_back({m.type, negate(m.amount)});
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
          out[i].amount = amount;
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

int moveCount(const Alg &alg) {
  int n = 0;
  for (Move m : alg) {
    if (!isRotation(m.type)) {
      ++n;
    }
  }
  return n;
}

} // namespace vcube
