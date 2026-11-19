#include "app/alg_html.hpp"

#include "app/colours.hpp"
#include "io/algset_json.hpp"
#include "train/case_logic.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QStringList>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::LearnPage", text);
}

} // namespace

// The moves as done, as rich text, with two quarter turns of a layer the
// same way written as a half turn. The move at `mark` (an index into
// `moves`) is highlighted and kept on its own.
QString movesHtml(const Alg &moves, std::optional<std::size_t> mark) {
  struct Done {
    Move move;
    bool marked;
  };
  std::vector<Done> merged;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    Move m = moves[i];
    bool marked = mark == i;
    if (!merged.empty() && !marked && !merged.back().marked &&
        merged.back().move == m && m.amount != 2) {
      merged.back().move.amount = 2;
    } else {
      merged.push_back({m, marked});
    }
  }
  QStringList parts;
  for (const Done &d : merged) {
    QString text = qs(toString(d.move)).toHtmlEscaped();
    parts << (d.marked ? "<b style='color:" + statusColour(Status::Bad).name() +
                             "'><u>" + text + "</u></b>"
                       : text);
  }
  return parts.join(' ');
}

namespace {

QString caseHeaderHtml(const AlgCase &c, const CaseGroup &group) {
  return QString("<p style='font-size:large'><b>%1</b> <span "
                 "style='color:" +
                 roleColour(Role::Muted).name() + "'>· %2</span></p>")
      .arg(qs(c.name).toHtmlEscaped(), qs(group.name).toHtmlEscaped());
}

QString greyHtml(const QString &text) {
  return "<span style='color:" + roleColour(Role::Muted).name() + "'>" + text +
         "</span>";
}

// " → Sune" after a step that stops at that case.
QString reachedHtml(const AlgSet &set, const std::string &caseId) {
  const AlgCase *next = set.findCase(caseId);
  return next ? " " + greyHtml("→ " + qs(next->name).toHtmlEscaped())
              : QString();
}

} // namespace

QString viewAlgsHtml(const AlgSet &set, const CaseView &view,
                     const CubieCube &state, Mirror world) {
  const AlgCase *c = set.findCase(view.caseId);
  const CaseGroup *g = set.groupOf(view.caseId);
  if (!c || !g) {
    return {};
  }
  QString html = caseHeaderHtml(*c, *g);
  std::vector<ViewAlg> algs =
      algsFor(set, *g, *c, analyzeCase(set, *g, *c), view, state, world);
  if (algs.empty()) {
    return html + "<p style='color:" + roleColour(Role::Muted).name() + "'>" +
           QObject::tr("No alg solves this case yet. Add one in the Library.")
               .toHtmlEscaped() +
           "</p>";
  }
  bool first = true;
  for (const ViewAlg &alg : algs) {
    QString line = qs(alg.text).toHtmlEscaped();
    line = first ? "<b>" + line + "</b>" : greyHtml(line);
    html += "<p>" + line + reachedHtml(set, alg.reducesTo) + "</p>";
    first = false;
  }
  return html;
}

QString chainHtml(const AlgSet &set, const CubieCube &state, Mirror world) {
  Chain chain = solveChain(set, state, world);
  if (chain.steps.empty()) {
    return {};
  }
  if (chain.steps.size() == 1) {
    return viewAlgsHtml(set, chain.steps.front().view, state, world);
  }
  const std::string &firstId = chain.steps.front().view.caseId;
  const AlgCase *c = set.findCase(firstId);
  const CaseGroup *g = set.groupOf(firstId);
  if (!c || !g) {
    return {};
  }
  QStringList lines;
  for (std::size_t i = 0; i < chain.steps.size(); ++i) {
    QString line = "<b>" + qs(chain.steps[i].alg).toHtmlEscaped() + "</b>";
    if (i + 1 < chain.steps.size()) {
      line += reachedHtml(set, chain.steps[i + 1].view.caseId);
    }
    lines << line;
  }
  QString html = caseHeaderHtml(*c, *g) + "<p>" + lines.join("<br>") + "</p>";
  if (!chain.complete) {
    html += "<p style='color:" + roleColour(Role::Muted).name() + "'>" +
            QObject::tr("The set's algs don't reach the end from there.")
                .toHtmlEscaped() +
            "</p>";
  }
  return html;
}

QString caseAlgsHtml(const AlgSet &set, const std::string &caseId,
                     const CubieCube &state) {
  // The scramble may show the case in one of the set's mirror worlds. The
  // case it was made for comes first: other cases of the set can look the
  // same from the cube (up to the pieces the step leaves free), and the
  // first of those in the set isn't the one the scramble sets up.
  std::optional<WorldView> seen;
  const AlgCase *c = set.findCase(caseId);
  const CaseGroup *g = set.groupOf(caseId);
  if (c && g) {
    for (Mirror world : set.worlds()) {
      if (auto view = viewOfCase(set, *g, *c, state, world)) {
        seen = WorldView{world, *view};
        break;
      }
    }
  }
  if (!seen) {
    seen = worldOf(set, state);
  }
  if (!seen) {
    seen = WorldView{Mirror{}, CaseView{caseId, false, 0}};
  }
  return set.reduction ? chainHtml(set, state, seen->world)
                       : viewAlgsHtml(set, seen->view, state, seen->world);
}

const char *gradeName(Grade g) {
  switch (g) {
  case Grade::Again:
    return "Again";
  case Grade::Hard:
    return "Hard";
  case Grade::Good:
    return "Good";
  case Grade::Easy:
    return "Easy";
  }
  return "";
}

QString gradeColour(Grade g) {
  switch (g) {
  case Grade::Again:
    return roleColour(Role::GradeAgain).name();
  case Grade::Hard:
    return roleColour(Role::GradeHard).name();
  case Grade::Good:
    return roleColour(Role::GradeGood).name();
  case Grade::Easy:
    return roleColour(Role::GradeEasy).name();
  }
  return roleColour(Role::GradeNone).name();
}

QString linksHtml(const AlgSet &set, const std::vector<LearnLink> &links,
                  const std::function<QString(const LearnLink &)> &note) {
  const AlgSet *s = &set;
  if (links.empty()) {
    return {};
  }
  auto nameOf = [s](const std::string &id) {
    const AlgCase *c = s->findCase(id);
    return c ? qs(c->name).toHtmlEscaped() : QString();
  };
  auto grey = [](const QString &t) {
    return "<span style='color:" + roleColour(Role::Muted).name() + "'>" + t +
           "</span>";
  };
  const QString good =
      "<span style='color:" + statusColour(Status::Good).name() + "'>✓</span>";
  const QString bad =
      "<span style='color:" + statusColour(Status::Bad).name() + "'>✗</span>";
  const std::string &firstId = links.front().view.caseId;
  const CaseGroup *group = s->groupOf(firstId);
  QString html =
      QString("<p style='font-size:large'><b>%1</b> <span "
              "style='color:" +
              roleColour(Role::Muted).name() + "'>· %2</span></p>")
          .arg(nameOf(firstId), group ? qs(group->name).toHtmlEscaped() : "");
  for (const LearnLink &link : links) {
    if (!link.over) {
      continue;
    }
    QString line = (link.grade == Grade::Again ? bad : good) + " ";
    if (&link != &links.front()) {
      line += grey(nameOf(link.view.caseId) + ":") + " ";
    }
    if (link.algs.empty()) {
      line += grey(tr("no alg"));
    } else {
      const ViewAlg &alg = link.algs.front();
      line += "<b>" + qs(alg.text).toHtmlEscaped() + "</b>";
      if (!alg.reducesTo.empty()) {
        line += " " + grey("→ " + nameOf(alg.reducesTo));
      }
    }
    QStringList notes;
    if (link.gaveUp) {
      notes << tr("gave up");
    } else if (!link.setStep) {
      QString to = link.solved ? tr("done") : nameOf(link.reached);
      notes << tr("you: %1 → %2 (%3)")
                   .arg(movesHtml(link.moves, std::nullopt), to,
                        link.closer ? tr("closer") : tr("not closer"));
    }
    if (QString n = note(link); !n.isEmpty()) {
      notes << n;
    }
    if (!notes.isEmpty()) {
      line += "<br>" + grey(notes.join(" · "));
    }
    html += "<p>" + line + "</p>";
  }
  return html;
}

} // namespace vcube
