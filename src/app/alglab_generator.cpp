#include "app/alglab_page.hpp"
#include "app/alglab_private.hpp"
#include "app/ui_text.hpp"

namespace vcube {

// --- generator --------------------------------------------------------------

SearchOptions AlgLabPage::searchOptions() const {
  SearchOptions o;
  o.moves = genSet_;
  o.maxLength = maxLength_->value();
  o.extra = lengthMode_->currentIndex() == 0 ? extra_->value() : -1;
  o.maxResults = maxResults_->value();
  return o;
}

void AlgLabPage::updateGenControls() {
  bool running = job_ != nullptr;
  runButton_->setEnabled(!running && case_.has_value());
  batchButton_->setEnabled(!running && case_ && !case_->key.isLab());
  stopButton_->setEnabled(running);
  extra_->setEnabled(lengthMode_->currentIndex() == 0);
  genBox_->setEnabled(!running);
}

void AlgLabPage::runSearch(std::vector<ResolvedCase> cases) {
  if (job_ || cases.empty()) {
    return;
  }
  auto job = std::make_shared<Job>();
  job->cases = static_cast<int>(cases.size());
  job_ = job;
  updateGenControls();
  SearchOptions options = searchOptions();
  QString moves = qs(options.moves.display());
  genStatus_->setText(tr("Starting…"));
  jobFuture_ = QtConcurrent::run([this, job, cases, options, moves] {
    int done = 0;
    int unsolvable = 0;
    QString last;
    for (const ResolvedCase &c : cases) {
      if (job->stop) {
        break;
      }
      ++done;
      SearchGoal goal{c.state, c.free, c.aufBefore, c.aufAfter};
      QString prefix =
          cases.size() > 1
              ? tr("%1 of %2, %3: ").arg(done).arg(cases.size()).arg(c.name)
              : QString();
      SearchCallbacks cb;
      LabCaseKey k = c.key;
      cb.result = [this, job, k](const SearchResult &r) {
        std::string text = r.text;
        QMetaObject::invokeMethod(
            this,
            [this, job, k, text] {
              if (std::ranges::find(job->dropped, k) != job->dropped.end()) {
                return;
              }
              if (store_.addCandidate(
                      k, {text, CandidateSource::Generated, unixNow()})) {
                ++job->found;
                job->dirty = true;
                if (!saveTimer_.isActive()) {
                  saveTimer_.start();
                }
                if (case_ && case_->key == k && !refreshTimer_.isActive()) {
                  refreshTimer_.start();
                }
              }
            },
            Qt::QueuedConnection);
      };
      cb.progress = [this, prefix](const std::string &m) {
        QString text = prefix + qs(m);
        QMetaObject::invokeMethod(
            this, [this, text] { genStatus_->setText(text); },
            Qt::QueuedConnection);
      };
      SearchOutcome out = searchAlgs(goal, options, job->stop, cb);
      if (out.end == SearchEnd::Unsolvable) {
        ++unsolvable;
        last = tr("%1 can't solve %2.").arg(moves, c.name);
      } else if (out.found == 0) {
        last = tr("No alg of %1 up to %2 moves.").arg(moves).arg(out.deepest);
      } else {
        last =
            plural(out.found,
                   tr("%n alg, shortest %1 moves, every alg up to %2 moves "
                      "found%3."),
                   tr("%n algs, shortest %1 moves, every alg up to %2 moves "
                      "found%3."))
                .arg(out.shortest)
                .arg(out.deepest)
                .arg(out.end == SearchEnd::Capped ? tr(" (stopped at the cap)")
                     : out.end == SearchEnd::Stopped ? tr(" (stopped)")
                                                     : QString());
      }
    }
    QString summary =
        cases.size() > 1
            ? tr("Searched %1 of %2 cases with %3.%4")
                  .arg(done)
                  .arg(cases.size())
                  .arg(moves)
                  .arg(unsolvable ? tr(" %n can't be solved with it.", nullptr,
                                       unsolvable)
                                  : QString())
            : last;
    QMetaObject::invokeMethod(
        this,
        [this, summary] {
          genStatus_->setText(summary);
          searchFinished();
        },
        Qt::QueuedConnection);
  });
}

void AlgLabPage::runBatch(Batch batch) {
  const bool wholeSet = batch == Batch::WholeSet;
  if (!case_ || case_->key.isLab()) {
    return;
  }
  const AlgSet *set = library_.find(case_->key.setId);
  const CaseGroup *group = set ? set->groupOf(case_->key.caseId) : nullptr;
  if (!group) {
    return;
  }
  std::vector<ResolvedCase> cases;
  for (const CaseGroup &g : set->groups) {
    if (!wholeSet && &g != group) {
      continue;
    }
    for (const AlgCase &c : g.cases) {
      if (auto r = resolveLabCase(library_, store_, {set->id, c.id})) {
        cases.push_back(*r);
      }
    }
  }
  if (QMessageBox::question(
          this, tr("Find algs"),
          plural(static_cast<int>(cases.size()),
                 tr("Search %n case with %1, keeping at most %2 algs? It "
                    "runs in the background; Stop ends it."),
                 tr("Search %n cases with %1, keeping at most %2 algs each? "
                    "It runs in the background; Stop ends it."))
              .arg(qs(genSet_.display()))
              .arg(maxResults_->value())) != QMessageBox::Yes) {
    return;
  }
  runSearch(std::move(cases));
}

void AlgLabPage::stopSearch() {
  if (job_) {
    job_->stop = true;
    genStatus_->setText(tr("Stopping…"));
  }
}

void AlgLabPage::searchFinished() {
  jobFuture_.waitForFinished();
  bool dirty = job_ && job_->dirty;
  int found = job_ ? job_->found : 0;
  job_.reset();
  saveTimer_.stop();
  if (dirty) {
    saveStore();
  }
  updateGenControls();
  fillTable();
  if (found > 0) {
    emit message(plural(found, tr("The generator added %n alg."),
                        tr("The generator added %n algs.")));
  }
}

} // namespace vcube
