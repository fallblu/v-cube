#include "app/alglab_common.hpp"
#include "app/ui_state.hpp"

#include "app/app_settings.hpp"
#include "app/colours.hpp"
#include "app/cube_painter.hpp"
#include "io/algset_json.hpp"

#include <QSettings>

#include <algorithm>
#include <array>

namespace vcube {

std::optional<ResolvedCase> resolveLabCase(const AlgLibrary &library,
                                           const AlgLabStore &store,
                                           const LabCaseKey &key) {
  ResolvedCase r;
  r.key = key;
  if (key.isLab()) {
    const LabCase *c = store.findCase(key.caseId);
    if (!c) {
      return std::nullopt;
    }
    r.name = qs(c->name);
    r.where = QObject::tr("Lab cases");
    r.state = labCaseState(*c);
    r.free = labCaseFree(*c);
    r.aufBefore = c->aufBefore;
    r.aufAfter = c->aufAfter;
    r.spec.free = r.free;
    // From above when only the last layer is unsolved.
    PieceMask ll;
    ll.lastLayer(Free::PermAndOri, Free::PermAndOri);
    r.spec.view =
        withinMask(r.state, ll) ? DiagramView::TopLayer : DiagramView::Iso3D;
    return r;
  }
  for (const AlgSet &s : library.sets()) {
    if (s.id != key.setId) {
      continue;
    }
    for (const CaseGroup &g : s.groups) {
      for (const AlgCase &c : g.cases) {
        if (c.id != key.caseId) {
          continue;
        }
        r.name = qs(c.name);
        r.where = qs(s.name) + " / " + qs(g.name);
        r.state = c.state;
        r.spec = s.specFor(g);
        r.free = r.spec.free;
        r.aufBefore = s.aufBefore;
        r.aufAfter = s.aufAfter;
        r.setAlgs = c.algs;
        return r;
      }
    }
  }
  return std::nullopt;
}

QPixmap labDiagram(const ResolvedCase &c, const AppSettings &settings, int size,
                   qreal dpr) {
  CubeLook look;
  look.scheme = settings.scheme;
  look.map = colourMapOf(kWcaHold);
  return cube_painter::diagram(
      maskedFacelets(c.state, c.spec.mask, c.spec.free), c.spec.view, size,
      look, dpr);
}

const CandidateFacts &CandidateCache::of(const ResolvedCase &c,
                                         const std::string &alg) {
  if (c.state != state_ || c.free != free_ || c.aufBefore != aufBefore_ ||
      c.aufAfter != aufAfter_) {
    facts_.clear();
    state_ = c.state;
    free_ = c.free;
    aufBefore_ = c.aufBefore;
    aufAfter_ = c.aufAfter;
  }
  auto found = facts_.find(alg);
  if (found != facts_.end()) {
    return found->second;
  }
  CandidateFacts f;
  f.key = labAlgKey(alg);
  f.moves = parseAlgOrEmpty(f.key);
  f.counts = moveCounts(f.moves);
  f.rotations = std::any_of(f.moves.begin(), f.moves.end(),
                            [](Move m) { return isRotation(m.type); });
  if (!f.moves.empty()) {
    f.fit = fitAlg(c.state, f.moves, c.free, aufsOf(c));
  }
  if (f.fit) {
    f.shown = qs(withAufs(f.key, *f.fit));
  } else {
    AlgCheck check = checkAlg(c.state, f.key, c.free, aufsOf(c));
    f.shown = qs(f.key) + "   (" + qs(describeCheck(check)) + ")";
  }
  return facts_.emplace(alg, std::move(f)).first->second;
}

std::vector<CandidateRow> candidateRows(const ResolvedCase &c,
                                        const AlgLabStore &store,
                                        CandidateCache &cache) {
  std::vector<CandidateRow> rows;
  std::unordered_map<std::string, std::size_t> listed; // by key
  auto add = [&](const std::string &alg, CandidateSource source, bool inSet) {
    const CandidateFacts &f = cache.of(c, alg);
    auto [at, isNew] = listed.try_emplace(f.key, rows.size());
    if (!isNew) {
      rows[at->second].inSet = rows[at->second].inSet || inSet;
      return;
    }
    CandidateRow row;
    row.alg = f.key;
    row.source = source;
    row.inSet = inSet;
    row.moves = f.moves;
    row.counts = f.counts;
    row.rotations = f.rotations;
    row.fit = f.fit;
    row.shown = f.shown;
    rows.push_back(std::move(row));
  };
  for (const auto &a : c.setAlgs) {
    add(a, CandidateSource::Set, true);
  }
  for (const auto &cand : store.candidates(c.key)) {
    add(cand.alg, cand.source, false);
  }
  return rows;
}

SortItem::SortItem(const QString &text, std::optional<double> key)
    : QTableWidgetItem(text) {
  setFlags(flags() & ~Qt::ItemIsEditable);
  setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  setData(Qt::UserRole + 1, key ? *key : 1e300);
}

bool SortItem::operator<(const QTableWidgetItem &other) const {
  QVariant a = data(Qt::UserRole + 1), b = other.data(Qt::UserRole + 1);
  if (a.isValid() && b.isValid()) {
    return a.toDouble() < b.toDouble();
  }
  return QTableWidgetItem::operator<(other);
}

const ErgoProfile *labGuideProfile(const AlgLabStore &store,
                                   const QString &chosen) {
  const ErgoProfile *first = nullptr;
  for (const auto &p : store.profiles()) {
    if (!p.grips) {
      continue;
    }
    if (qs(p.name) == chosen) {
      return &p;
    }
    if (!first) {
      first = &p;
    }
  }
  return first;
}

const ErgoProfile *labGuideProfile(const AlgLabStore &store) {
  return labGuideProfile(store, saved::lab("scoreProfile").get().toString());
}

QString labTime(std::optional<std::int64_t> ms, int decimals) {
  if (!ms) {
    return QStringLiteral("–");
  }
  if (*ms == kDnfTime) {
    return QStringLiteral("DNF");
  }
  return qs(formatTime(*ms, decimals));
}

} // namespace vcube
