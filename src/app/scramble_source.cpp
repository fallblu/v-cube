#include "app/scramble_source.hpp"

#include "app/app_settings.hpp"
#include "app/cube_painter.hpp"
#include "train/case_logic.hpp"

#include <QPainter>

namespace vcube {

std::function<CubieCube(Rng &)> scrambleStateMaker(const AlgLibrary &library,
                                                   const QString &type) {
  if (type.startsWith("set:")) {
    std::string id = ss(type.mid(4));
    const AlgSet *set = nullptr;
    for (const auto &s : library.sets()) {
      if (s.id == id) {
        set = &s;
      }
    }
    if (set && set->caseCount() > 0) {
      // The cases ticked in Learn and Drill (or all of them).
      std::optional<QStringList> ticked = tickedCases(id);
      struct Entry {
        CubieCube state;
        PieceMask free;
      };
      std::vector<Entry> pool;
      for (const auto &g : set->groups) {
        for (const auto &c : g.cases) {
          if (!ticked || ticked->contains(qs(c.id))) {
            // The case as Drill sets it up: oriented for its main alg.
            pool.push_back(
                {analyzeCase(*set, g, c).display, set->specFor(g).free});
          }
        }
      }
      if (pool.empty()) {
        for (const auto &g : set->groups) {
          for (const auto &c : g.cases) {
            pool.push_back({c.state, set->specFor(g).free});
          }
        }
      }
      bool before = set->aufBefore, after = set->aufAfter;
      return [pool, before, after](Rng &rng) {
        std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
        std::uniform_int_distribution<int> quarter(0, 3);
        const Entry &e = pool[pick(rng)];
        CubieCube s = randomState(e.free, rng) * e.state;
        if (before) {
          s = s * uTurn(quarter(rng));
        }
        if (after) {
          s = uTurn(quarter(rng)) * s;
        }
        return s.isSolved() ? randomState(PieceMask::all(), rng) : s;
      };
    }
  }
  const ScrambleType *t = findScrambleType(ss(type));
  ScrambleType st = t ? *t : scrambleTypes().front();
  return [st](Rng &rng) { return randomStateOf(st, rng); };
}

// --- ScrambleTypeBox
// ----------------------------------------------------------

ScrambleTypeBox::ScrambleTypeBox(const AlgLibrary &library, QWidget *parent)
    : QComboBox(parent), library_(library) {
  setFocusPolicy(Qt::NoFocus);
  setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  setMinimumContentsLength(14);
  refresh();
  connect(this, &QComboBox::currentIndexChanged, this,
          &ScrambleTypeBox::emitIfChanged);
}

QString ScrambleTypeBox::currentType() const {
  QString t = currentData().toString();
  return t.isEmpty() ? QString("333") : t;
}

void ScrambleTypeBox::setCurrentType(const QString &type) {
  int i = findData(type);
  setCurrentIndex(i >= 0 ? i : 0);
  emitIfChanged();
}

void ScrambleTypeBox::refresh() {
  QString previous = count() ? currentType() : last_;
  {
    QSignalBlocker block(this);
    clear();
    for (const auto &t : scrambleTypes()) {
      addItem(qs(t.name), qs(t.id));
    }
    if (!library_.sets().empty()) {
      insertSeparator(count());
      for (const auto &set : library_.sets()) {
        if (set.caseCount() > 0) {
          addItem(tr("Alg set: %1").arg(qs(set.name)), "set:" + qs(set.id));
        }
      }
    }
    int i = findData(previous);
    setCurrentIndex(i >= 0 ? i : 0);
  }
  emitIfChanged();
}

void ScrambleTypeBox::emitIfChanged() {
  QString t = currentType();
  if (t != last_) {
    last_ = t;
    emit typeChanged(t);
  }
}

// --- ScrambleFeed -----------------------------------------------------------

ScrambleFeed::ScrambleFeed(const AlgLibrary &library, QObject *parent)
    : QObject(parent), library_(library) {
  connect(&watcher_, &QFutureWatcher<GeneratedScramble>::finished, this,
          &ScrambleFeed::ready);
}

void ScrambleFeed::setType(const QString &type) {
  if (started_ && type == type_) {
    return;
  }
  type_ = type;
  regenerate();
}

void ScrambleFeed::regenerate() {
  started_ = true;
  current_.reset();
  prefetched_.reset();
  emit currentChanged();
  request();
}

void ScrambleFeed::advance() {
  if (prefetched_) {
    current_ = std::move(prefetched_);
    prefetched_.reset();
  } else {
    current_.reset();
  }
  emit currentChanged();
  request();
}

void ScrambleFeed::request() {
  if (watcher_.isRunning()) {
    wantMore_ = true; // ready() asks again
    return;
  }
  wantMore_ = false;
  pendingType_ = type_;
  watcher_.setFuture(
      generateScramble(scrambleStateMaker(library_, pendingType_)));
}

void ScrambleFeed::ready() {
  GeneratedScramble result = watcher_.result();
  // A result for a type that is no longer wanted is dropped.
  if (pendingType_ == type_) {
    if (!current_) {
      current_ = std::move(result);
      emit currentChanged();
    } else if (!prefetched_) {
      prefetched_ = std::move(result);
    }
  }
  if (wantMore_ || !current_ || !prefetched_) {
    request();
  }
}

// --- NetPreview -------------------------------------------------------------

NetPreview::NetPreview(QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFixedSize(sizeHint());
  setToolTip(tr("The scrambled cube, white on top and green in front"));
}

void NetPreview::setScheme(const ColourScheme &scheme) {
  scheme_ = scheme;
  update();
}

void NetPreview::setCube(const std::optional<FaceletCube> &cube) {
  cube_ = cube;
  update();
}

void NetPreview::setScramble(const QString &scramble) {
  ParseResult parsed = parseAlg(ss(scramble));
  if (!parsed || scramble.trimmed().isEmpty()) {
    setCube(std::nullopt);
    return;
  }
  FaceletCube f;
  f.apply(parsed.alg);
  setCube(f);
}

void NetPreview::paintEvent(QPaintEvent *) {
  if (!cube_) {
    return;
  }
  QPainter p(this);
  CubeLook look;
  look.scheme = scheme_;
  cube_painter::paintNet(p, QRectF(rect()), *cube_, look);
}

} // namespace vcube
