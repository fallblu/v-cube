#include "app/alglab_common.hpp"

#include "app/app_settings.hpp"
#include "app/colours.hpp"
#include "app/cube_painter.hpp"
#include "io/algset_json.hpp"

#include <QSettings>

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
    ll.lastLayer(true, true, true);
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

std::vector<CandidateRow> candidateRows(const ResolvedCase &c,
                                        const AlgLabStore &store) {
  std::vector<CandidateRow> rows;
  auto add = [&](const std::string &alg, CandidateSource source, bool inSet) {
    std::string key = labAlgKey(alg);
    for (auto &r : rows) {
      if (r.alg == key) {
        r.inSet = r.inSet || inSet;
        return;
      }
    }
    CandidateRow row;
    row.alg = key;
    row.source = source;
    row.inSet = inSet;
    auto parsed = parseAlg(key);
    if (parsed && !parsed.alg.empty()) {
      row.fit = fitAlg(c.state, parsed.alg, c.free, c.aufBefore, c.aufAfter);
    }
    if (row.fit) {
      row.shown = qs(withAufs(key, *row.fit));
    } else {
      AlgCheck check = checkAlg(c.state, key, c.free, c.aufBefore, c.aufAfter);
      row.shown = qs(key) + "   (" + qs(describeCheck(check)) + ")";
    }
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
  return labGuideProfile(store,
                         QSettings().value(kLabProfileSetting).toString());
}

QColor seriesColour(std::size_t i, bool dark) {
  static constexpr std::array<const char *, 8> light = {
      "#2a78d6", "#eb6834", "#1baf7a", "#eda100",
      "#e87ba4", "#008300", "#6250d6", "#e34948"};
  static constexpr std::array<const char *, 8> darkHex = {
      "#3987e5", "#d95926", "#199e70", "#c98500",
      "#d55181", "#008300", "#9085e9", "#e66767"};
  return QColor(dark ? darkHex[i % 8] : light[i % 8]);
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
