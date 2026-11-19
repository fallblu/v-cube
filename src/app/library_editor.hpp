#pragma once

#include "io/library.hpp"
#include "train/case_logic.hpp"

#include <QString>
#include <QVariantMap>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class QUndoStack;

namespace vcube {

// The Library's edits of the alg sets, apart from its widgets: the set and
// case selected, every change saved at once and made undoable, and each
// set's cases checked under its step settings (kept up to date edit by
// edit). Whatever an edit, an undo or a redo changes is handed to the
// page as one Change, which it refreshes from.
class LibraryEditor {
public:
  // One undoable change of a set: its state before and after (nullopt
  // when it doesn't exist, i.e. it was added or deleted).
  struct Edit {
    std::string setId;
    std::optional<AlgSet> before;
    std::optional<AlgSet> after;
    SetProgress forgotten;  // progress the change dropped, put back by undo
    SetProgress progress;   // a removed set's progress
    QVariantMap settings;   // and its per-set settings
    std::string caseBefore; // the selected case before and after
    std::string caseAfter;
  };

  // What a change touched.
  struct Change {
    std::string setId; // the set that changed
    // Only these cases' algs changed; nullopt: anything may have.
    std::optional<std::vector<std::string>> cases;
    // Sets were added or removed, or the selection moved to another set:
    // everything is shown afresh.
    bool reload = false;
    // What to tell the other modes: the set whose contents changed, or
    // empty when sets were added, removed or renamed; nullopt: nothing.
    std::optional<QString> announce;
  };

  using Refresh = std::function<void(const Change &)>;
  using Message = std::function<void(const QString &)>;

  // `undo` keeps the edits; `refresh` shows each change, `message` says
  // what went wrong or was undone.
  LibraryEditor(AlgLibrary &library, QUndoStack *undo, Refresh refresh,
                Message message);

  // The selection.
  std::string setId;
  std::string caseId;
  AlgSet *set();
  const AlgSet *set() const;
  AlgCase *currentCase();
  const AlgCase *currentCase() const;

  // Every case of a set, checked under its step settings; worked out when
  // first needed.
  const std::map<std::string, CaseAnalysis> &analysisFor(const AlgSet &s);
  const CaseAnalysis *analysisOf(const std::string &caseId);
  // The algs of the set that don't solve their cases.
  int failingIn(const AlgSet &s);

  // An edit of the current set: take begin() before changing it and
  // commit() after. commit() saves it and makes it undoable (or puts the
  // set back if it can't be saved); `changedCases` names the only cases
  // whose algs changed, or is nullopt when anything else may have;
  // `forget` the cases whose progress goes with them.
  struct Pending {
    AlgSet before;
    std::string caseId;
  };
  std::optional<Pending> begin();
  bool
  commit(const QString &text, Pending pending,
         std::optional<std::vector<std::string>> changedCases = std::nullopt,
         const std::vector<std::string> &forget = {});

  // A set was added (new, duplicated, imported or restored): it is
  // selected (with no case unless `keepCase`) and its adding can be undone.
  void added(const AlgSet &set, const QString &text, bool keepCase = false);
  // The set of `edit` (with its progress and settings) was removed from
  // the library: its settings go too, the first set is selected, and the
  // removal can be undone.
  void removed(Edit edit, const QString &text);

  // Undoes or redoes an edit (see the undo stack's commands).
  enum class Step { Undo, Redo };
  void applyEdit(Edit &edit, Step step, const QString &text);

private:
  AlgLibrary &library_;
  QUndoStack *undo_;
  Refresh refresh_;
  Message message_;
  bool reverting_ = false; // see applyEdit()
  std::map<std::string, std::map<std::string, CaseAnalysis>> analysis_;

  void push(const QString &text, Edit edit);
  // Brings the checks up to date and shows the change.
  void changed(Change change);
};

// The cases whose algs differ between two states of a set, or nullopt if
// anything else differs too.
std::optional<std::vector<std::string>> algChanges(const AlgSet &a,
                                                   const AlgSet &b);

} // namespace vcube
