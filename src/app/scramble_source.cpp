#include "app/scramble_source.hpp"

#include "app/app_settings.hpp"
#include "app/case_page.hpp"
#include "app/cube_painter.hpp"
#include "train/case_logic.hpp"

#include <QPainter>
#include <QPushButton>
#include <QSettings>

namespace vcube {

std::function<ScrambleSetup(Rng &)>
scrambleStateMaker(const AlgLibrary &library, const QString &type) {
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
        std::string caseId;
      };
      std::vector<Entry> pool;
      auto fill = [&](bool tickedOnly) {
        for (const auto &g : set->groups) {
          for (const auto &c : g.cases) {
            if (!tickedOnly || !ticked || ticked->contains(qs(c.id))) {
              // The case as Drill sets it up: oriented for its main alg.
              pool.push_back({analyzeCase(*set, g, c).display,
                              set->specFor(g).free, c.id});
            }
          }
        }
      };
      fill(true);
      if (pool.empty()) {
        fill(false);
      }
      bool before = set->aufBefore, after = set->aufAfter;
      return [pool, before, after, id](Rng &rng) {
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
        if (s.isSolved()) {
          return ScrambleSetup{randomState(PieceMask::all(), rng), {}, {}};
        }
        return ScrambleSetup{s, id, e.caseId};
      };
    }
  }
  const ScrambleType *t = findScrambleType(ss(type));
  ScrambleType st = t ? *t : scrambleTypes().front();
  return
      [st](Rng &rng) { return ScrambleSetup{randomStateOf(st, rng), {}, {}}; };
}

// --- ScrambleAlgs -----------------------------------------------------------

ScrambleAlgs::ScrambleAlgs(AlgLibrary &library, const ColourScheme &scheme,
                           QString settingsKey, QWidget *parent)
    : QObject(parent), library_(library), scheme_(scheme),
      settingsKey_(std::move(settingsKey)) {
  button_ = new QPushButton(tr("Algs (Tab)"), parent);
  button_->setFocusPolicy(Qt::NoFocus);
  button_->setCheckable(true);
  button_->setChecked(QSettings().value(settingsKey_, false).toBool());
  button_->setToolTip(
      tr("Show the algs for the alg set case the scramble sets up"));
  label_ = new QLabel(parent);
  label_->setTextFormat(Qt::RichText);
  label_->setWordWrap(true);
  label_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont f = label_->font();
  f.setPointSize(f.pointSize() + 3);
  label_->setFont(f);
  label_->setMinimumHeight(110);
  connect(button_, &QPushButton::toggled, this, [this](bool on) {
    QSettings().setValue(settingsKey_, on);
    update();
  });
  setType({});
}

void ScrambleAlgs::setType(const QString &type) {
  offered_ = type.startsWith("set:");
  update();
}

void ScrambleAlgs::setScramble(const GeneratedScramble *scramble) {
  if (scramble) {
    scramble_ = *scramble;
  } else {
    scramble_.reset();
  }
  update();
}

bool ScrambleAlgs::toggle() {
  if (!offered_) {
    return false;
  }
  button_->toggle();
  return true;
}

void ScrambleAlgs::update() {
  button_->setVisible(offered_);
  label_->setVisible(offered_);
  const AlgSet *set = nullptr;
  if (offered_ && button_->isChecked() && scramble_ &&
      !scramble_->caseId.empty()) {
    set = library_.find(scramble_->setId);
  }
  QString html;
  if (set) {
    html = caseAlgsHtml(*set, scramble_->caseId, scramble_->state);
    if (scramble_->neutral) {
      html += "<p style='color:gray'>" +
              tr("Held with %1").arg(holdHtml(scramble_->hold, scheme_)) +
              "</p>";
    }
  }
  label_->setText(html);
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

void ScrambleFeed::setNeutrality(const ColourNeutrality &neutrality) {
  if (neutrality == neutrality_) {
    return;
  }
  neutrality_ = neutrality;
  if (started_) {
    regenerate();
  }
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
  pendingNeutrality_ = neutrality_;
  watcher_.setFuture(generateScramble(
      scrambleStateMaker(library_, pendingType_), neutrality_));
}

void ScrambleFeed::ready() {
  GeneratedScramble result = watcher_.result();
  // A result for a type that is no longer wanted is dropped.
  if (pendingType_ == type_ && pendingNeutrality_ == neutrality_) {
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
