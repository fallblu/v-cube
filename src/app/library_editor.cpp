#include "app/library_editor.hpp"

#include "app/ui_state.hpp"
#include "io/algset_json.hpp"

#include <QCoreApplication>
#include <QUndoCommand>
#include <QUndoStack>

#include <utility>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::LibraryPage", text);
}

// An undoable change of the library, made by LibraryEditor::applyEdit.
// The change is already done when the command is pushed.
class SetEditCommand : public QUndoCommand {
public:
  SetEditCommand(LibraryEditor *editor, const QString &text,
                 LibraryEditor::Edit edit)
      : QUndoCommand(text), editor_(editor), edit_(std::move(edit)) {}

  void undo() override {
    editor_->applyEdit(edit_, LibraryEditor::Step::Undo, text());
  }
  void redo() override {
    if (done_) {
      done_ = false; // done before it was pushed
      return;
    }
    editor_->applyEdit(edit_, LibraryEditor::Step::Redo, text());
  }

private:
  LibraryEditor *editor_;
  LibraryEditor::Edit edit_;
  bool done_ = true;
};

} // namespace

std::optional<std::vector<std::string>> algChanges(const AlgSet &a,
                                                   const AlgSet &b) {
  if (a.id != b.id || a.name != b.name || a.description != b.description ||
      a.builtin != b.builtin || !(a.spec == b.spec) ||
      a.aufBefore != b.aufBefore || a.aufAfter != b.aufAfter ||
      a.targetSeconds != b.targetSeconds || a.guided != b.guided ||
      a.families != b.families || a.reduction != b.reduction ||
      a.mirrors != b.mirrors || a.groups.size() != b.groups.size()) {
    return std::nullopt;
  }
  std::vector<std::string> out;
  for (std::size_t g = 0; g < a.groups.size(); ++g) {
    const CaseGroup &ga = a.groups[g];
    const CaseGroup &gb = b.groups[g];
    if (ga.name != gb.name || ga.spec != gb.spec ||
        ga.cases.size() != gb.cases.size()) {
      return std::nullopt;
    }
    for (std::size_t i = 0; i < ga.cases.size(); ++i) {
      const AlgCase &ca = ga.cases[i];
      const AlgCase &cb = gb.cases[i];
      if (ca.id != cb.id || ca.name != cb.name || ca.state != cb.state) {
        return std::nullopt;
      }
      if (ca.algs != cb.algs) {
        out.push_back(ca.id);
      }
    }
  }
  return out;
}

LibraryEditor::LibraryEditor(AlgLibrary &library, QUndoStack *undo,
                             Refresh refresh, Message message)
    : library_(library), undo_(undo), refresh_(std::move(refresh)),
      message_(std::move(message)) {}

AlgSet *LibraryEditor::set() { return library_.find(setId); }

const AlgSet *LibraryEditor::set() const {
  return std::as_const(library_).find(setId);
}

AlgCase *LibraryEditor::currentCase() {
  AlgSet *s = set();
  return s && !caseId.empty() ? s->findCase(caseId) : nullptr;
}

const AlgCase *LibraryEditor::currentCase() const {
  const AlgSet *s = set();
  return s && !caseId.empty() ? s->findCase(caseId) : nullptr;
}

const std::map<std::string, CaseAnalysis> &
LibraryEditor::analysisFor(const AlgSet &s) {
  auto it = analysis_.find(s.id);
  if (it == analysis_.end()) {
    std::map<std::string, CaseAnalysis> all;
    for (const auto &g : s.groups) {
      for (const auto &c : g.cases) {
        all[c.id] = analyzeCase(s, g, c);
      }
    }
    it = analysis_.emplace(s.id, std::move(all)).first;
  }
  return it->second;
}

const CaseAnalysis *LibraryEditor::analysisOf(const std::string &id) {
  const AlgSet *s = set();
  if (!s) {
    return nullptr;
  }
  const auto &all = analysisFor(*s);
  auto it = all.find(id);
  return it == all.end() ? nullptr : &it->second;
}

int LibraryEditor::failingIn(const AlgSet &s) {
  int failing = 0;
  for (const auto &[id, a] : analysisFor(s)) {
    failing += a.failing();
  }
  return failing;
}

std::optional<LibraryEditor::Pending> LibraryEditor::begin() {
  AlgSet *s = set();
  if (!s) {
    return std::nullopt;
  }
  return Pending{*s, caseId};
}

bool LibraryEditor::commit(const QString &text, Pending pending,
                           std::optional<std::vector<std::string>> changedCases,
                           const std::vector<std::string> &forget) {
  AlgSet *s = set();
  if (!s) {
    return false;
  }
  QString error;
  if (!library_.save(*s, &error)) {
    *s = std::move(pending.before);
    caseId = pending.caseId;
    message_(tr("Could not save %1, so the change was undone: %2")
                 .arg(qs(s->name), error));
    changed({s->id, std::nullopt, false, std::nullopt});
    return false;
  }
  Edit edit;
  edit.setId = s->id;
  edit.caseBefore = pending.caseId;
  edit.caseAfter = caseId;
  if (!forget.empty()) {
    SetProgress &p = library_.progress(s->id);
    for (const std::string &id : forget) {
      if (auto it = p.find(id); it != p.end()) {
        edit.forgotten.insert(*it);
      }
    }
    library_.forgetProgress(s->id, forget);
  }
  edit.before = std::move(pending.before);
  edit.after = *s;
  push(text, std::move(edit));
  changed({s->id, std::move(changedCases), false, qs(s->id)});
  return true;
}

void LibraryEditor::added(const AlgSet &s, const QString &text, bool keepCase) {
  setId = s.id;
  if (!keepCase) {
    caseId.clear();
  }
  push(text, Edit{s.id, std::nullopt, s, {}, {}, {}, {}, {}});
  changed({s.id, std::nullopt, true, QString()});
}

void LibraryEditor::removed(Edit edit, const QString &text) {
  // A later set with the same id must start afresh.
  saved::removeSetSettings(edit.setId);
  std::string id = edit.setId;
  push(text, std::move(edit));
  setId = library_.sets().empty() ? std::string() : library_.sets().front().id;
  caseId.clear();
  changed({id, std::nullopt, true, QString()});
}

void LibraryEditor::push(const QString &text, Edit edit) {
  undo_->push(new SetEditCommand(this, text, std::move(edit)));
}

void LibraryEditor::applyEdit(Edit &edit, Step step, const QString &text) {
  const bool undoing = step == Step::Undo;
  const std::optional<AlgSet> &to = undoing ? edit.before : edit.after;
  const std::optional<AlgSet> &from = undoing ? edit.after : edit.before;
  const std::string &id = edit.setId;
  if (reverting_) {
    return; // the stack stepping back over an edit that wasn't applied
  }
  QString error;
  AlgSet *current = library_.find(id);
  if (to && current) {
    AlgSet kept = std::exchange(*current, *to);
    if (!library_.save(*current, &error)) {
      // As commit() does: what is shown stays what is in the file. The
      // undo stack has already moved, so it is stepped back.
      *current = std::move(kept);
      message_((undoing ? tr("Could not save %1, so nothing was undone: %2")
                        : tr("Could not save %1, so nothing was redone: %2"))
                   .arg(qs(to->name), error));
      QMetaObject::invokeMethod(
          undo_,
          [this, undoing] {
            reverting_ = true;
            if (undoing) {
              undo_->redo();
            } else {
              undo_->undo();
            }
            reverting_ = false;
          },
          Qt::QueuedConnection);
      return;
    }
    // Progress the change dropped comes back with undo, and goes again.
    if (!edit.forgotten.empty()) {
      SetProgress &p = library_.progress(id);
      for (const auto &[forgottenId, cp] : edit.forgotten) {
        if (undoing) {
          p.try_emplace(forgottenId, cp);
        } else {
          p.erase(forgottenId);
        }
      }
      library_.saveProgress(id);
    }
  } else if (to) {
    // The set comes back, with its progress and settings.
    if (!library_.add(*to, &error)) {
      message_(tr("Could not add %1 again: %2").arg(qs(to->name), error));
      return;
    }
    if (!edit.progress.empty()) {
      library_.progress(id) = edit.progress;
      library_.saveProgress(id);
    }
    if (!edit.settings.isEmpty()) {
      saved::setSetSettings(id, edit.settings);
    }
  } else if (current) {
    // The set goes; keep what it had so that it can come back.
    edit.progress = library_.progress(id);
    edit.settings = saved::setSettings(id);
    if (!library_.remove(id, &error)) {
      message_(tr("Could not delete %1: %2").arg(qs(from->name), error));
      return;
    }
    saved::removeSetSettings(id);
  }
  // Show what changed.
  std::optional<std::vector<std::string>> cases;
  if (to && from) {
    cases = algChanges(*from, *to);
  }
  bool sameSet = setId == id;
  setId = to ? id
             : (library_.sets().empty() ? std::string()
                                        : library_.sets().front().id);
  caseId = to ? (undoing ? edit.caseBefore : edit.caseAfter) : std::string();
  changed(
      {id, cases, !to || !from || !sameSet, to && from ? qs(id) : QString()});
  message_((undoing ? tr("Undid: %1") : tr("Redid: %1")).arg(text));
}

void LibraryEditor::changed(Change change) {
  AlgSet *s = library_.find(change.setId);
  if (change.reload || !change.cases) {
    analysis_.erase(change.setId);
  } else if (auto it = analysis_.find(change.setId);
             s && it != analysis_.end()) {
    // Only these cases' algs changed: check just them again.
    for (const std::string &id : *change.cases) {
      const AlgCase *c = s->findCase(id);
      const CaseGroup *g = c ? s->groupOf(id) : nullptr;
      if (c && g) {
        it->second[id] = analyzeCase(*s, *g, *c);
      } else {
        it->second.erase(id);
      }
    }
  }
  if (!change.reload && !s) {
    return; // nothing of it to show
  }
  refresh_(change);
}

} // namespace vcube
