#include "app/blind_tracing_drill.hpp"

#include "app/session_panel.hpp"
#include "app/widgets.hpp"
#include "io/algset_json.hpp"

#include "cube/cubie.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>

#include <array>
#include <cmath>

namespace vcube {

TracingDrill::TracingDrill(BlindContext &context, QWidget *parent)
    : BlindDrill(context, parent) {
  left_ = new BlindStatsPanel(false, parent);
  row_ = new QWidget(parent);
  auto *layout = new QHBoxLayout(row_);
  layout->setContentsMargins(0, 0, 0, 0);
  edgesEdit_ = new QLineEdit(row_);
  cornersEdit_ = new QLineEdit(row_);
  edgesEdit_->setPlaceholderText(tr("Edge letters, e.g. AB CD E"));
  cornersEdit_->setPlaceholderText(tr("Corner letters"));
  for (QLineEdit *e : {edgesEdit_, cornersEdit_}) {
    e->setFocusPolicy(Qt::ClickFocus);
    e->installEventFilter(this);
    QFont f = e->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    e->setFont(f);
  }
  layout->addWidget(new QLabel(tr("Edges:"), row_));
  layout->addWidget(edgesEdit_, 1);
  layout->addSpacing(12);
  layout->addWidget(new QLabel(tr("Corners:"), row_));
  layout->addWidget(cornersEdit_, 1);

  connect(&tick_, &QTimer::timeout, this, &TracingDrill::onTick);
  connect(left_->panel(), &SessionPanel::attemptMenuRequested, this,
          &TracingDrill::showTraceMenu);
}

QWidget *TracingDrill::leftPanel() const { return left_; }

QWidget *TracingDrill::inputRow() const { return row_; }

BlindDrill::View TracingDrill::view() const {
  if (flow_.phase() == TraceFlow::Phase::Ready) {
    return {View::Kind::Hidden, tr("Press Space for a scramble")};
  }
  return {};
}

void TracingDrill::enter(bool changed) {
  (void)changed;
  leave();
  ctx_.cube->setState(FaceletCube());
  ctx_.display->showText("–",
                         ctx_.page->palette().color(QPalette::PlaceholderText));
}

void TracingDrill::leave() {
  flow_.reset();
  tick_.stop();
  for (QLineEdit *e : {edgesEdit_, cornersEdit_}) {
    e->clear();
    e->clearFocus();
  }
}

void TracingDrill::deactivated() {
  ctx_.cube->finishAnimations();
  edgesEdit_->clearFocus();
  cornersEdit_->clearFocus();
}

void TracingDrill::pause() { flow_.pause(monotonicMs()); }

void TracingDrill::resume() { flow_.resume(monotonicMs()); }

bool TracingDrill::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::KeyPress &&
      (obj == edgesEdit_ || obj == cornersEdit_)) {
    int key = static_cast<QKeyEvent *>(event)->key();
    bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (key == Qt::Key_Escape) {
      static_cast<QWidget *>(obj)->clearFocus();
      return true;
    }
    if (enter && obj == edgesEdit_) {
      cornersEdit_->setFocus();
      return true;
    }
    if (enter && obj == cornersEdit_) {
      checkTrace();
      return true;
    }
  }
  return BlindDrill::eventFilter(obj, event);
}

bool TracingDrill::handleKey(QKeyEvent *e, bool press) {
  int key = e->key();
  std::optional<Move> move = ctx_.settings.keymap.lookup(e);
  using K = TraceFlow::Key;
  K k = key == Qt::Key_Space                            ? K::Space
        : key == Qt::Key_Return || key == Qt::Key_Enter ? K::Enter
        : key == Qt::Key_Escape                         ? K::Escape
        : key == Qt::Key_Tab                            ? K::Tab
        : move                                          ? K::Move
                                                        : K::Other;
  TraceFlow::Effects fx =
      flow_.key(k, move && isRotation(move->type), press, e->isAutoRepeat(),
                ctx_.feed.current().has_value(), monotonicMs());
  apply(fx, move);
  return fx.used;
}

void TracingDrill::apply(const TraceFlow::Effects &e,
                         std::optional<Move> move) {
  if (e.waiting) {
    ctx_.result->setText(tr("Generating scramble…"));
  }
  if (e.start) {
    startTracing(); // the feed moved on when the last one was shown
  }
  if (e.look && move) {
    look(*move);
  }
  if (e.giveUp) {
    giveUp();
  }
  if (e.focusFields) {
    focusTrace();
  }
  if (e.rotate && move) {
    traceCube_.apply(*move);
    ctx_.cube->animateMove(*move);
  }
  if (e.onlyRotations) {
    emit message(tr("Only rotations while tracing"));
  }
}

void TracingDrill::scrambleChanged() {
  if (ctx_.feed.current()) {
    apply(flow_.scrambleReady(monotonicMs()), std::nullopt);
  }
}

void TracingDrill::startTracing() {
  used_ = *ctx_.feed.current();
  ctx_.feed.advance(); // seen now, so never traced or solved again
  CubieCube scrambled = cubieOf(parseAlgOrEmpty(ss(used_->text)));
  heldCubie_ = solverState(scrambled, ctx_.scheme.hold);
  heldStart_ = heldCubie_.toFacelets();
  traceCube_ = heldStart_;
  ctx_.cube->clearMarks();
  ctx_.cube->setState(traceCube_);
  edgesEdit_->clear();
  cornersEdit_->clear();
  ctx_.result->clear();
  ctx_.prompt->setText(tr("Trace the memo: edges from UR, corners from UBL."));
  ctx_.display->showText(blindTimeText(0, 1));
  emit viewChanged();
  updateTick();
  emit busyChanged();
  showHint();
}

void TracingDrill::giveUp() {
  ctx_.cube->clearMarks();
  ctx_.display->showText("–",
                         ctx_.page->palette().color(QPalette::PlaceholderText));
  ctx_.result->setText(tr("Scramble given up; nothing was recorded."));
  emit viewChanged();
  updateTick();
  emit busyChanged();
  showHint();
}

void TracingDrill::focusTrace() {
  if (flow_.phase() != TraceFlow::Phase::Memo) {
    return;
  }
  (edgesEdit_->text().isEmpty() || !cornersEdit_->text().isEmpty()
       ? edgesEdit_
       : cornersEdit_)
      ->setFocus();
}

void TracingDrill::checkTrace() {
  if (flow_.phase() != TraceFlow::Phase::Memo) {
    return;
  }
  const BlindScheme &scheme = ctx_.scheme;
  // Letters that can't be read are a typing slip, not a wrong memo: they
  // are shown, and nothing is graded until they are fixed.
  QStringList slips;
  QLineEdit *slipEdit = nullptr;
  for (BldPiece kind : kBlindKinds) {
    QLineEdit *edit = kind == BldPiece::Edge ? edgesEdit_ : cornersEdit_;
    LetterScheme::Parsed parsed = scheme.letters.parse(kind, ss(edit->text()));
    if (parsed.errorAt) {
      slips << tr("%1: can't read “%2” at character %3.")
                   .arg(pieceName(kind), edit->text().toHtmlEscaped())
                   .arg(*parsed.errorAt + 1);
      if (!slipEdit) {
        slipEdit = edit;
      }
    }
  }
  if (slipEdit) {
    ctx_.result->setText(
        colouredHtml(kBlindWrong, slips.join("<br>") + "<br>" +
                                      tr("Fix the letters and press Enter "
                                         "to check.")));
    slipEdit->setFocus();
    return;
  }
  std::int64_t ms = *flow_.finish(monotonicMs());
  BldMemo mine = traceMemo(heldCubie_);
  QString html;
  std::optional<std::pair<BldPiece, std::vector<int>>> ringed;
  std::array<bool, 2> right{};
  for (BldPiece kind : kBlindKinds) {
    QLineEdit *edit = kind == BldPiece::Edge ? edgesEdit_ : cornersEdit_;
    QString name = pieceName(kind);
    LetterScheme::Parsed parsed = scheme.letters.parse(kind, ss(edit->text()));
    bool ok = false;
    QString line;
    if (parsed.errorAt) {
      line = tr("%1: can't read “%2” at character %3.")
                 .arg(name, edit->text().toHtmlEscaped())
                 .arg(*parsed.errorAt + 1);
    } else {
      MemoCheck check = checkMemo(heldCubie_, kind, parsed.stickers);
      ok = check.solves && !check.firstWrong;
      // The typed letters in pairs, the first wrong one marked.
      QStringList pairs;
      QString pair;
      for (std::size_t i = 0; i < parsed.stickers.size(); ++i) {
        QString l = letterOf(kind, parsed.stickers[i]).toHtmlEscaped();
        if (check.firstWrong == i) {
          l = "<span style='color:" + kBlindWrong.name() + "'><u>" + l +
              "</u></span>";
        }
        pair += l;
        if (i % 2 == 1) {
          pairs << pair;
          pair.clear();
        }
      }
      if (!pair.isEmpty()) {
        pairs << pair;
      }
      QString typed = pairs.isEmpty() ? tr("(none)") : pairs.join(' ');
      auto lettersOf = [&](const std::vector<int> &stickers) {
        QStringList out;
        for (std::size_t i = 0; i < stickers.size() && i < 8; ++i) {
          out << letterOf(kind, stickers[i]).toHtmlEscaped();
        }
        if (stickers.size() > 8) {
          out << "…";
        }
        return out.join(", ");
      };
      if (ok) {
        line = tr("%1 ✓ %2").arg(name, typed);
      } else if (check.firstWrong) {
        std::size_t at = *check.firstWrong;
        QString expected =
            check.expected.empty()
                ? tr("nothing: the pieces were all solved by then")
            : check.expected.size() == 1
                ? tr("%1").arg(lettersOf(check.expected))
                : tr("a new cycle, any of %1").arg(lettersOf(check.expected));
        line = tr("%1 ✗ %2 — letter %3 is wrong; there it should be %4.")
                   .arg(name, typed)
                   .arg(at + 1)
                   .arg(expected);
        if (!ringed && !check.expected.empty()) {
          ringed = {kind, check.expected};
        }
      } else {
        line = tr("%1 ✗ %2 — pieces are left; next would be %3.")
                   .arg(name, typed, lettersOf(check.expected));
        if (!ringed && !check.expected.empty()) {
          ringed = {kind, check.expected};
        }
      }
    }
    right[kind == BldPiece::Edge ? 0 : 1] = ok;
    html += colouredHtml(ok ? kBlindRight : kBlindWrong, line);
  }
  html += "<p>" +
          tr("Traced here: edges %1, corners %2.")
              .arg(qs(scheme.letters.format(BldPiece::Edge, mine.edges))
                       .toHtmlEscaped(),
                   qs(scheme.letters.format(BldPiece::Corner, mine.corners))
                       .toHtmlEscaped()) +
          " " +
          (mine.parity()
               ? tr("Parity: an odd number of edge targets, so the parity "
                    "alg goes between the edges and the corners.")
               : tr("No parity.")) +
          "</p>";
  ctx_.result->setText(html);
  ctx_.store.addTrace({ms, right[0], right[1], unixNow()});
  ctx_.save();
  refreshLeft();
  ctx_.display->showText(blindTimeText(ms, ctx_.settings.decimals),
                         right[0] && right[1] ? kBlindRight : kBlindWrong);
  // Back to the way it was held, with the stickers that were due ringed.
  edgesEdit_->clearFocus();
  cornersEdit_->clearFocus();
  ctx_.cube->setState(heldStart_);
  StickerMarks marks;
  if (ringed) {
    for (int s : ringed->second) {
      marks.rings.push_back(bldFacelet(ringed->first, s));
    }
  }
  ctx_.cube->setMarks(marks);
  emit viewChanged();
  updateTick();
  emit busyChanged();
  showHint();
}

void TracingDrill::updateTick() {
  // The time only changes during a trace, so the display is only redrawn
  // then.
  bool ticking = flow_.phase() == TraceFlow::Phase::Memo;
  if (ticking != tick_.isActive()) {
    if (ticking) {
      tick_.start(30);
    } else {
      tick_.stop();
    }
  }
}

void TracingDrill::onTick() {
  if (flow_.phase() != TraceFlow::Phase::Memo) {
    updateTick();
    return;
  }
  ctx_.display->showText(blindTimeText(flow_.elapsed(monotonicMs()), 1));
}

void TracingDrill::showTraceMenu(int index, const QPoint &globalPos) {
  if (index < 0 ||
      static_cast<std::size_t>(index) >= ctx_.store.traces().size()) {
    return;
  }
  ActionMenu menu(ctx_.page);
  menu.add(tr("Delete"), [this, index] {
    ctx_.store.removeTrace(static_cast<std::size_t>(index));
    ctx_.save();
    refreshLeft();
  });
  menu.run(globalPos);
}

void TracingDrill::refreshLeft() {
  const auto &traces = ctx_.store.traces();
  int d = ctx_.settings.decimals;
  std::size_t shown = std::min<std::size_t>(traces.size(), 100);
  int edges = 0, corners = 0, both = 0;
  std::int64_t bothMs = 0;
  StatValue best;
  for (const auto &t : traces) {
    edges += t.edgesRight;
    corners += t.cornersRight;
    if (t.edgesRight && t.cornersRight) {
      ++both;
      bothMs += t.timeMs;
      best = best ? std::min(*best, t.timeMs) : t.timeMs;
    }
  }
  std::vector<AttemptRow> rows;
  for (std::size_t r = 0; r < shown; ++r) {
    std::size_t i = traces.size() - 1 - r;
    const TraceAttempt &t = traces[i];
    AttemptRow row;
    row.index = static_cast<int>(i);
    row.cells.push_back({QString::number(i + 1), {}, {}, false});
    row.cells.push_back({blindTimeText(t.timeMs, d), {}, {}, true});
    row.cells.push_back({t.edgesRight ? "✓" : "✗",
                         t.edgesRight ? kBlindRight : kBlindWrong,
                         {},
                         true});
    row.cells.push_back({t.cornersRight ? "✓" : "✗",
                         t.cornersRight ? kBlindRight : kBlindWrong,
                         {},
                         true});
    rows.push_back(std::move(row));
  }
  SessionPanel *panel = left_->panel();
  panel->setColumns({blindColumn("number", "#"),
                     blindColumn("time", tr("Time")),
                     blindColumn("edges", tr("Edges")),
                     blindColumn("corners", tr("Corners"))});
  panel->setRows(std::move(rows));
  int n = static_cast<int>(traces.size());
  auto pct = [n](int k) {
    return n ? QString("%1%").arg(std::lround(100.0 * k / n)) : QString("-");
  };
  panel->setStatsHtml(
      tr("<p><b>Memo tracing</b>: %1 traced. Edges right %2, "
         "corners right %3, both %4.</p><p>Mean time (both "
         "right): %5 &nbsp; Best: %6</p>")
          .arg(n)
          .arg(pct(edges), pct(corners), pct(both),
               both ? blindTimeText(bothMs / both, d) : QString("-"),
               blindTimeText(best, d)));
}

void TracingDrill::showHint() {
  TraceFlow::Phase phase = flow_.phase();
  ctx_.hint->setText(
      phase == TraceFlow::Phase::Ready
          ? tr("Space: a new scramble, and the timer starts.")
      : phase == TraceFlow::Phase::Done
          ? tr("Space: next scramble. Rotations turn the cube to look "
               "at it.")
          : tr("Turn the cube as a whole to look at it. Enter: type "
               "the letters (Enter after the edges, Enter again to "
               "check; Esc returns the keys to the cube). Esc: give "
               "up this scramble."));
}

} // namespace vcube
